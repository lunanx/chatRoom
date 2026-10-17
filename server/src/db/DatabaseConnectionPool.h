#pragma once

#include "DatabaseConfig.h"
#include <mariadb/conncpp.hpp>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

/*
 * MariaDB 物理连接池。
 *
 * 规则：
 * 1. 一条 Lease 在任意时刻只允许一个业务调用持有，禁止并发共享同一条 Connection。
 * 2. Lease 是移动语义的 RAII 句柄，析构时自动归还连接。
 * 3. 连接池只复用物理连接；一笔事务从 begin 到 commit/rollback 必须始终使用同一 Lease。
 * 4. 当前采用按需创建，最小空闲连接数为 0；最大连接数默认 8，可用
 *    CHATROOM_DB_POOL_MAX 环境变量覆盖。获取连接最多等待 2 秒。
 */
class DatabaseConnectionPool
{
public:
    class Lease
    {
    public:
        Lease() = default;
        ~Lease();

        Lease(const Lease &) = delete;
        Lease &operator=(const Lease &) = delete;
        Lease(Lease &&other) noexcept;
        Lease &operator=(Lease &&other) noexcept;

        explicit operator bool() const noexcept;
        sql::Connection *get() const noexcept;
        // 数据库连接已确认失效时调用：直接丢弃，不再放回空闲池。
        void discard() noexcept;

    private:
        friend class DatabaseConnectionPool;
        Lease(DatabaseConnectionPool *owner, std::unique_ptr<sql::Connection> connection) noexcept;
        void reset() noexcept;

        DatabaseConnectionPool *m_owner{nullptr};
        std::unique_ptr<sql::Connection> m_connection;
    };

    static DatabaseConnectionPool &instance();

    // 超时或连接创建失败时返回空 Lease，不会无限等待。
    Lease acquire(std::chrono::milliseconds timeout = std::chrono::milliseconds(2000));

    std::size_t maxConnections() const noexcept;
    std::size_t totalConnections() const;
    std::size_t idleConnections() const;

    DatabaseConnectionPool(const DatabaseConnectionPool &) = delete;
    DatabaseConnectionPool &operator=(const DatabaseConnectionPool &) = delete;

private:
    DatabaseConnectionPool();
    ~DatabaseConnectionPool();

    std::unique_ptr<sql::Connection> createConnection();
    void release(std::unique_ptr<sql::Connection> connection) noexcept;
    void discard(std::unique_ptr<sql::Connection> connection) noexcept;

    mutable std::mutex m_mutex;
    std::condition_variable m_available;
    std::vector<std::unique_ptr<sql::Connection>> m_idleConnections;
    std::size_t m_totalConnections{0};
    std::size_t m_maxConnections{8};
    DBconfig m_config;
    bool m_configReady{false};
    std::string m_configError;
};
