#include "TcpServer.h"
#include "ServerConfig.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

TcpServer::TcpServer()
    : m_sfd(-1),
      m_isInit(false),
      m_saddr{},
      m_epollReactor(nullptr)
{
}

TcpServer::~TcpServer()
{
    stop(); // 如果已经stop过了，第二次的stop也没事，这样的设计就是让stop具备幂等性
    delete m_epollReactor;
    m_epollReactor = nullptr;
    if (m_sfd != -1)
    {
        close(m_sfd);
    }
}

bool TcpServer::init()
{
    // 服务器监听配置与数据库配置分开管理;环境变量可覆盖默认配置文件路径
    const char *configuredPath = std::getenv("CHATROOM_SERVER_CONFIG");
#ifdef CHATROOM_SERVER_CONFIG_FILE
    const std::string defaultConfigPath = CHATROOM_SERVER_CONFIG_FILE;
#else
    const std::string defaultConfigPath = "config/server.conf";
#endif
    try
    {
        const std::string configPath = configuredPath != nullptr && configuredPath[0] != '\0'?std::string(configuredPath):defaultConfigPath;
        const ServerConfig config = ServerConfig::loadServerConfig(configPath);
        m_saddr = {};
        m_saddr.sin_family = AF_INET;
        m_saddr.sin_port = htons(config.port);
        if(inet_pton(AF_INET,config.host.c_str(),&m_saddr.sin_addr) != 1)
        {
            std::cerr << "SERVER_HOST不是有效的IPv4地址: "<<config.host<<'\n';
            return false;
        }
    }
    catch(const std::exception &e)
    {
        std::cerr<<"加载服务器配置失败: "<<e.what()<<'\n';
        return false;
    }

    m_sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_sfd == -1)
    {
        perror("socket create error");
        return false;
    }

    // 获取m_sfd的状态
    int flags = fcntl(m_sfd, F_GETFL, 0);
    if (flags == -1)
    {
        perror("fcntl F_GETFL error");
        return false;
    }
    // 设置m_sfd为非阻塞
    // 这样的话就不会卡住线程
    if (fcntl(m_sfd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        perror("fcntl F_SETFL O_NONBLOCK error");
        return false;
    }

    int opt = 1;
    if (setsockopt(m_sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
    {
        perror("setsockopt error");
        return false;
    }

    if (myBind() == -1)
    {
        return false;
    }
    if (myListen() == -1)
    {
        return false;
    }

    m_epollReactor = new EpollReactor();

    if (m_epollReactor->initMEpoll(m_sfd) == false)
    {
        return false;
    }

    m_isInit = true;
    return true;
}

void TcpServer::stop()
{
    if (m_isInit)
    {
        uint64_t cnt = 1;
        if (write(m_epollReactor->getMStopFD(), &cnt, sizeof(cnt)) == -1)
        {
            perror("write epoll MainReactor's stopFD error");
            return;
        }
        m_isInit = false;
    }
}

void TcpServer::start()
{
    if (m_isInit)
    {
        m_epollReactor->run();
    }
}

int TcpServer::myBind()
{
    socklen_t socklen = sizeof(m_saddr);
    if (bind(m_sfd, (sockaddr *)&m_saddr, socklen) == -1)
    {
        perror("bind error");
        return -1;
    }
    printf("bind success\n");
    return 0;
}

int TcpServer::myListen()
{
    if (listen(m_sfd, 128) == -1)
    {
        perror("listen error");
        return -1;
    }
    printf("listen success\n");
    return 0;
}
