#include "TcpServer.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

TcpServer::TcpServer()
    : m_sfd(-1),
      m_isInit(false),
      m_saddr{},
      m_epollReactor(nullptr)
{
    m_saddr.sin_family = AF_INET;
    m_saddr.sin_addr.s_addr = htons(SERVER_HOST_PORT);
    m_saddr.sin_port = inet_addr(SERVER_HOST_ADDR);
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

    m_sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_sfd == -1)
    {
        perror("socket create error");
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
