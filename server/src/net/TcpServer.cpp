#include "TcpServer.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

TcpServer::TcpServer()
    : m_sfd(socket(AF_INET, SOCK_STREAM, 0)),
      m_saddr{AF_INET, htons(SERVER_HOST_PORT), {inet_addr(SERVER_HOST_ADDR)}, {0}},
      m_epollReactor(new EpollReactor(m_sfd))
{
}

TcpServer::~TcpServer()
{
    close(m_sfd);
    delete m_epollReactor;
    m_epollReactor = nullptr;
}

void TcpServer::init()
{
    myBind();
    myListen();
    m_epollReactor->allocate();
}

void TcpServer::myBind()
{
    socklen_t socklen = sizeof(m_saddr);
    if (bind(m_sfd, (sockaddr *)&m_saddr, socklen) == -1)
    {
        perror("bind error");
        return;
    }
    printf("bind success\n");
}

void TcpServer::myListen()
{
    if (listen(m_sfd, 128) == -1)
    {
        perror("listen error");
        return;
    }
    printf("listen success\n");
}
