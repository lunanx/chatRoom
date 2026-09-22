#include "ClientSession.h"
#include <string.h>
#include <unistd.h>
ClientSession::ClientSession(int cfd, sockaddr_in *cin)
    : m_cfd(cfd),
      m_buf(new char[128]{}),
      m_caddr(*cin)
{
}

ClientSession::~ClientSession()
{
    close(m_cfd);
    delete[] m_buf;
}

void ClientSession::handle_write(const char *buf, ssize_t len)
{
    if (send(m_cfd, buf, len, 0) == -1)
    {
        perror("send error");
        return;
    }
}

int ClientSession::handle_read()
{
    char buf[128] = ""; //不直接用m_buf防止收到错误数据
    int res = recv(m_cfd, buf, sizeof(buf), 0);
    if (res == -1)
    {
        perror("recv error");
        return -1;
    }
    else if (res == 0)
    {
        printf("对端已下线\n");
        return 0;
    }
    strcpy(m_buf,buf);
    return res;
}

int ClientSession::getClientSocket()
{
    return m_cfd;
}

sockaddr_in ClientSession::getAddr()
{
    return m_caddr;
}

char *ClientSession::getbuf()
{
    return m_buf;
}
