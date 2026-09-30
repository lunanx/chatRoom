#include "TcpServer.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

TcpServer::TcpServer()
    : m_sfd(-1),
      m_epollReactor(nullptr),
      m_isInit(false),
      m_saddr{AF_INET, htons(SERVER_HOST_PORT), {inet_addr(SERVER_HOST_ADDR)}}
{
}

TcpServer::~TcpServer()
{

    if (m_sfd != -1)
    {
        close(m_sfd);
    }
    stop();
    delete m_epollReactor;
    m_epollReactor = nullptr;
}

void TcpServer::init()
{

    m_sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_sfd == -1)
    {
        perror("socket create error");
        return;
    }

    int opt = 1;
    if (setsockopt(m_sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
    {
        perror("setsockopt error");
        return;
    }

    if (myBind() == -1)
    {
        return;
    }
    if (myListen() == -1)
    {
        return;
    }

    m_epollReactor = new EpollReactor();
    if (m_epollReactor->initMEpoll(m_sfd) == false)
    {
        return;
    }
    m_isInit = true;
    m_epollReactor->run();
}

void TcpServer::stop()
{
    if (m_isInit)
    {
        uint64_t cnt = 1;
        write(m_epollReactor->getMStopFD(), &cnt, sizeof(cnt));
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
