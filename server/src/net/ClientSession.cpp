#include "ClientSession.h"
#include <string.h>
#include <unistd.h>
ClientSession::ClientSession(int cfd, sockaddr_in *cin)
    : m_cfd(cfd),
      m_caddr(*cin)
{
}

ClientSession::~ClientSession()
{
    if(m_cfd != -1) close(m_cfd);
}

void ClientSession::handle_write(const char *buf, size_t len)
{

    if (send(m_cfd, buf, len, 0) == -1)
    {
        perror("send error");
        return;
    }
}

ssize_t ClientSession::handle_read()
{
    // 每次读取前先清空m_buf
    memset(m_buf, 0, sizeof(m_buf));
    char buf[128] = ""; // 不直接用m_buf防止收到错误数据
    // 目前就设置只读一次吧，之后再考虑循环问题。
    while (1)
    {
        ssize_t res = recv(m_cfd, buf, sizeof(buf), 0);
        if (res > 0)
        {
            // 实际收到字节数
            size_t n = static_cast<size_t>(res);
            if (n <= (size_t)BUFSIZE)
            {
                memcpy(m_buf, buf, n);
            }
            return res;
        }
        else if (res == 0)
        {
            printf("对端已下线\n");
            return 0;
        }
        else
        {
            if (errno == EINTR)
            {
                continue;
            }
            else if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                printf("数据读干净了\n");
                return -1;
            }
            else
            {
                perror("recv error");
                return -1;
            }
        }
    }
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
