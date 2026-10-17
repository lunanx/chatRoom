#include "DatabaseConnectionPool.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <utility>

namespace
{
std::size_t readMaxConnections()
{
    const char *text = std::getenv("CHATROOM_DB_POOL_MAX");
    if (text == nullptr || *text == '\0')
    {
        return 8;
    }

    try
    {
        std::size_t parsed = 0;
        const unsigned long value = std::stoul(text, &parsed);
        if (parsed != std::string(text).size() || value == 0 ||
            value > static_cast<unsigned long>(std::numeric_limits<std::size_t>::max()))
        {
            return 8;
        }
        return static_cast<std::size_t>(value);
    }
    catch (...)
    {
        return 8;
    }
}

std::string getConfigPath()
{
    const char *environmentPath = std::getenv("CHATROOM_DB_CONFIG");
    if (environmentPath != nullptr && environmentPath[0] != '\0')
    {
        return environmentPath;
    }
#ifdef CHATROOM_DB_CONFIG_FILE
    return CHATROOM_DB_CONFIG_FILE;
#else
    return "config/database.conf";
#endif
}
} // namespace

DatabaseConnectionPool::Lease::Lease(
    DatabaseConnectionPool *owner,
    std::unique_ptr<sql::Connection> connection) noexcept
    : m_owner(owner), m_connection(std::move(connection))
{
}

DatabaseConnectionPool::Lease::~Lease()
{
    reset();
}

DatabaseConnectionPool::Lease::Lease(Lease &&other) noexcept
    : m_owner(other.m_owner), m_connection(std::move(other.m_connection))
{
    other.m_owner = nullptr;
}

DatabaseConnectionPool::Lease &DatabaseConnectionPool::Lease::operator=(Lease &&other) noexcept
{
    if (this != &other)
    {
        reset();
        m_owner = other.m_owner;
        m_connection = std::move(other.m_connection);
        other.m_owner = nullptr;
    }
    return *this;
}

DatabaseConnectionPool::Lease::operator bool() const noexcept
{
    return m_owner != nullptr && m_connection != nullptr;
}

sql::Connection *DatabaseConnectionPool::Lease::get() const noexcept
{
    return m_connection.get();
}

void DatabaseConnectionPool::Lease::discard() noexcept
{
    if (m_owner != nullptr && m_connection != nullptr)
    {
        m_owner->discard(std::move(m_connection));
    }
    m_owner = nullptr;
    m_connection.reset();
}

void DatabaseConnectionPool::Lease::reset() noexcept
{
    if (m_owner != nullptr && m_connection != nullptr)
    {
        m_owner->release(std::move(m_connection));
    }
    m_owner = nullptr;
    m_connection.reset();
}

DatabaseConnectionPool &DatabaseConnectionPool::instance()
{
    static DatabaseConnectionPool pool;
    return pool;
}

DatabaseConnectionPool::DatabaseConnectionPool()
    : m_maxConnections(readMaxConnections())
{
    try
    {
        // 配置文件只在连接池初始化时读取一次，而不是每个 Repository 调用都读取。
        m_config = DatabaseConfig::loadDBConfig(getConfigPath());
        m_configReady = true;
    }
    catch (const std::exception &error)
    {
        m_configError = error.what();
    }
}

DatabaseConnectionPool::~DatabaseConnectionPool()
{
    // 服务应先停止并 join 所有工作线程，再析构连接池。
    std::lock_guard<std::mutex> lock(m_mutex);
    m_idleConnections.clear();
}

std::unique_ptr<sql::Connection> DatabaseConnectionPool::createConnection()
{
    if (!m_configReady)
    {
        std::cerr << "数据库连接池配置不可用: " << m_configError << '\n';
        return nullptr;
    }

    try
    {
        sql::Driver *driver = sql::mariadb::get_driver_instance();
        const sql::SQLString url(
            "jdbc:mariadb://" + m_config.host + ":" +
            std::to_string(m_config.port) + "/" + m_config.database);
        sql::Properties properties({{"user", m_config.username},
                                    {"password", m_config.password}});
        return std::unique_ptr<sql::Connection>(driver->connect(url, properties));
    }
    catch (const sql::SQLException &error)
    {
        std::cerr << "创建 MariaDB 连接失败: " << error.what()
                  << "，错误代码: " << error.getErrorCode() << '\n';
    }
    catch (const std::exception &error)
    {
        std::cerr << "创建 MariaDB 连接时发生异常: " << error.what() << '\n';
    }
    return nullptr;
}

DatabaseConnectionPool::Lease DatabaseConnectionPool::acquire(std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;

    while (true)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        while (!m_idleConnections.empty())
        {
            auto connection = std::move(m_idleConnections.back());
            m_idleConnections.pop_back();

            bool usable = false;
            try
            {
                usable = connection != nullptr && !connection->isClosed();
            }
            catch (...)
            {
                usable = false;
            }

            if (usable)
            {
                return Lease(this, std::move(connection));
            }

            // 失效连接从池中剔除，之后允许创建新的物理连接补位。
            if (m_totalConnections > 0)
            {
                --m_totalConnections;
            }
        }

        if (m_totalConnections < m_maxConnections)
        {
            // 先预留容量，再释放锁建连，避免在慢速网络连接期间阻塞所有借还操作。
            ++m_totalConnections;
            lock.unlock();

            auto connection = createConnection();
            if (connection == nullptr)
            {
                std::lock_guard<std::mutex> relock(m_mutex);
                --m_totalConnections;
                m_available.notify_one();
                return {};
            }
            return Lease(this, std::move(connection));
        }

        if (m_available.wait_until(lock, deadline) == std::cv_status::timeout)
        {
            return {};
        }
    }
}

void DatabaseConnectionPool::release(std::unique_ptr<sql::Connection> connection) noexcept
{
    if (!connection)
    {
        return;
    }

    bool reusable = false;
    try
    {
        if (!connection->isClosed())
        {
            // 防止异常路径把未结束事务带给下一个租用者。
            // 已提交事务再 rollback 不会撤销已提交数据；随后统一恢复 autocommit。
            connection->rollback();
            connection->setAutoCommit(true);
            reusable = !connection->isClosed();
        }
    }
    catch (const std::exception &error)
    {
        std::cerr << "归还数据库连接时重置状态失败，将丢弃连接: " << error.what() << '\n';
    }
    catch (...)
    {
        std::cerr << "归还数据库连接时发生未知错误，将丢弃连接\n";
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (reusable)
        {
            try
            {
                m_idleConnections.emplace_back(std::move(connection));
            }
            catch (...)
            {
                // 归还过程处于 noexcept 路径；容器内存分配失败时丢弃连接，不能 terminate。
                if (m_totalConnections > 0)
                {
                    --m_totalConnections;
                }
            }
        }
        else if (m_totalConnections > 0)
        {
            --m_totalConnections;
        }
    }
    m_available.notify_one();
}


void DatabaseConnectionPool::discard(std::unique_ptr<sql::Connection> connection) noexcept
{
    if (connection)
    {
        try
        {
            connection->close();
        }
        catch (...)
        {
            // 丢弃失效连接时不让关闭异常影响业务异常处理。
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_totalConnections > 0)
        {
            --m_totalConnections;
        }
    }
    m_available.notify_one();
}

std::size_t DatabaseConnectionPool::maxConnections() const noexcept
{
    return m_maxConnections;
}

std::size_t DatabaseConnectionPool::totalConnections() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_totalConnections;
}

std::size_t DatabaseConnectionPool::idleConnections() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_idleConnections.size();
}
