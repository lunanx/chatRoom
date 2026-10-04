#include "ClientSession.h"
#include <string.h>
#include <unistd.h>
ClientSession::ClientSession(int cfd, sockaddr_in *cin)
    : m_cfd(cfd),
      m_caddr(*cin)
{
    m_writeBuf.clear();
    m_recvBuf.clear();
}

ClientSession::~ClientSession()
{
    if (m_cfd != -1)
        close(m_cfd);
}

int ClientSession::handle_write(const std::string &data)
{

    // 将新数据写入缓冲，可能还残留上次未发送的数据
    m_writeBuf.append(data);

    // 尝试把整个发送端缓冲区发送出去
    while (!m_writeBuf.empty())
    {
        ssize_t res = send(m_cfd, m_writeBuf.data(), m_writeBuf.size(), 0);
        if (res > 0)
        {
            // 移除发送的部分
            m_writeBuf.erase(0, static_cast<size_t>(res));
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
                // 信号被打断，重新send
                continue;
            }
            else if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // 当前发送不出去
                // 保存剩余数据
                // 等待下次epoll
                return 1;
            }
            else
            {
                perror("send error");
                return -1;
            }
        }
    }
    return 1;
}

int ClientSession::handle_read()
{
    char buf[128] = ""; // 每次读取128字节
    while (1)
    {
        ssize_t res = recv(m_cfd, buf, sizeof(buf), 0);
        if (res > 0)
        {
            // 有多少读多少，不需要考虑其他，其他层会判断
            m_recvBuf.append(buf, static_cast<size_t>(res));
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
                printf("数据读干净了，暂无数据\n");
                return 1;
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

std::string ClientSession::getWriteBuf()
{
    return m_writeBuf;
}

std::string ClientSession::getRecvBuf()
{
    return m_recvBuf;
}
