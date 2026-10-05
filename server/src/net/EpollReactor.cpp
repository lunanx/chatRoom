#include "EpollReactor.h"
#include "protocol/FrameDecoder.h"
#include <sys/eventfd.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

std::mutex mux;
//------------------------EpollReactor----------------------
EpollReactor::EpollReactor()
    : m_sfd(-1),
      m_subTimer(0),
      m_MStopFD(-1),
      m_MEpfd(-1),
      m_MEvs{},
      m_subReactors{},
      m_threads(subMAXCnt)
{
}

EpollReactor::~EpollReactor()
{
    for (int i = 0; i < subMAXCnt; ++i)
    {
        // 应该先向当前线程发送停止信号，否则delete后停止信号文件描述符就不在了
        if (m_subReactors[i] != nullptr)
        {
            // 可能已经创建了线程，那就是需要join()
            if (m_threads[i].joinable())
            {
                uint64_t cnt = 1;
                write(m_subReactors[i]->getStopFD(), &cnt, sizeof(cnt));
                m_threads[i].join();
            }
            // 但不管需不需要join(),都是要delete对象
            delete m_subReactors[i];    // 释放单个对象
            m_subReactors[i] = nullptr; // 置空防止悬空指针
        }
    }
    if (m_MEpfd != -1)
        close(m_MEpfd);
    if (m_MStopFD != -1)
        close(m_MStopFD);
}

bool EpollReactor::initMEpoll(int sfd)
{
    createMEpollFD();
    if (m_MEpfd == -1)
    {
        return false;
    }

    if ((m_MStopFD = eventfd(0, EFD_NONBLOCK)) == -1) // 设置一个停止信号计数器，给控制端停止用
    {
        perror("m_stopSignalFD init error");
        return false;
    }

    if (addToMEpoll(m_MStopFD) == false) // 将m_MStopFD文件描述符加入到epoll中
    {
        printf("m_MStopFD add to epoll error\n");
        return false;
    }

    m_sfd = sfd;

    if (addToMEpoll(m_sfd) == false) // 将m_sfd文件描述符加入到epoll中
    {
        printf("m_sfd add to epoll error\n");
        return false;
    }

    for (int i = 0; i < subMAXCnt; i++)
    {
        m_subReactors[i] = new SubReactor();
        if (m_subReactors[i]->initSub() == false)
        {
            printf("m_subReactors initSub error!\n");
            delete m_subReactors[i];
            m_subReactors[i] = nullptr;
            return false;
        }
    }

    for (int i = 0; i < subMAXCnt; i++)
    {
        if (m_subReactors[i] == nullptr)
        {
            printf("m_subReactors new error!\n");
            return false;
        }
    }

    return true;
}

int EpollReactor::getMStopFD()
{
    return m_MStopFD;
}

void EpollReactor::createMEpollFD()
{
    m_MEpfd = epoll_create(1);
    if (m_MEpfd == -1)
    {
        perror("m_MEpfd create error");
        return;
    }
}

bool EpollReactor::addToMEpoll(int fd)
{
    epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = fd;
    if (epoll_ctl(m_MEpfd, EPOLL_CTL_ADD, fd, &ev) == -1)
    {
        perror("in m_MEpfd, add error");
        return false;
    }
    return true;
}

int EpollReactor::setNonblocking(int fd)
{
    // 获取fd的文件描述符状态
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
    {
        perror("F_GETFL error");
        return -1;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        perror("F_SETFL error");
        return -1;
    }

    return 0;
}

void EpollReactor::run()
{
    if (!m_threads.empty())
    {
        m_threads.clear();
    }
    // 创建线程，将线程和subReactor对应
    for (int i = 0; i < subMAXCnt; i++)
    {
        m_threads.emplace_back(&SubReactor::reactor, m_subReactors[i], i);
    }

    sockaddr_in cin;
    socklen_t socklen = sizeof(cin);
    while (1)
    {
        // 等待事件
        int num = epoll_wait(m_MEpfd, m_MEvs, sizeof(m_MEvs) / sizeof(m_MEvs[0]), -1);
        if (num == -1)
        {
            perror("m_MEpfd wait error");
            return;
        }
        for (int i = 0; i < num; i++)
        {
            int newfd = m_MEvs[i].data.fd;
            // 说明有新连接产生
            if (newfd == m_sfd)
            {
                socklen = sizeof(cin);
                newfd = accept(m_sfd, (sockaddr *)&cin, &socklen);
                if (newfd == -1)
                {
                    perror("accept error");
                    return;
                }
                printf("[%s:%d] accept success\n", inet_ntoa(cin.sin_addr), ntohs(cin.sin_port)); // 测试函数，开发后删除
                if (setNonblocking(newfd) == -1)
                {
                    printf("setNonblock error\n");
                    return;
                }
                // 有新连接来了,轮询sub
                auto sub = m_subReactors[m_subTimer++ % subMAXCnt];
                // 儿子都在 wait阻塞呢，先将客户端信息放到他的连接队列中
                sub->addQueueConnFD(ConInfo{newfd, cin});
                uint64_t cnt = 1;
                // 唤醒儿子
                if (write(sub->getWakeFD(), &cnt, sizeof(cnt)) == -1)
                {
                    perror("Sub wake error");
                    return;
                }
            }
            // 控制端发来了停止信号
            else if (newfd == m_MStopFD)
            {
                uint64_t cnt;
                read(newfd, &cnt, sizeof(cnt)); // 消耗事件
                return;                         // 直接返回
            }
        }
    }
}

/*
    int m_wakeFD; // 用于老爹唤醒

    int m_epfd; // 每个线程独自的epoll套接字

    int m_stopSignalFD; // 用于老爹调用自己的析构函数，提醒孩子该退出线程了

    std::queue<ConInfo> m_queueConnFD; // 接收老爹的新连接

    std::unordered_map<int, ClientSession *> m_cliSessionsMap; // 这里用哈希表可以快速查询到，也便于插入和删除

    epoll_event m_evs[subSessionMAXCnt]; // 每个线程产生的文件描述符集合

    FrameDecoder m_decoder; // 解析器
*/

//------------------------SubReactor----------------------
SubReactor::SubReactor()
    : m_wakeFD(-1),
      m_epfd(-1),
      m_stopSignalFD(-1),
      m_queueConnFD(),
      m_cliSessionsMap(),
      m_evs{}
{
}

SubReactor::~SubReactor()
{
    while (!m_queueConnFD.empty())
    {
        auto tmp = m_queueConnFD.front();
        // 可能没创建cliSession对话就已经stop了
        if (m_cliSessionsMap.find(tmp.m_fd) == m_cliSessionsMap.end())
        {
            close(tmp.m_fd);
        }
        // 如果找到了，不用你关，cliSession会自己关
        m_queueConnFD.pop();
    }

    for (auto p : m_cliSessionsMap)
    {
        delete p.second;
    }
    m_cliSessionsMap.clear();

    if (m_wakeFD != -1)
        close(m_wakeFD);
    if (m_stopSignalFD != -1)
        close(m_stopSignalFD);
    if (m_epfd != -1)
        close(m_epfd);
}

bool SubReactor::initSub()
{
    createEpollFD();
    if (m_epfd == -1)
    {
        return false;
    }
    if ((m_wakeFD = eventfd(0, EFD_NONBLOCK)) == -1) // 设置一个门铃，老爹会敲门铃，参数二为了不让read函数阻塞
    {
        perror("m_wakeFD init error");
        return false;
    }
    if ((m_stopSignalFD = eventfd(0, EFD_NONBLOCK)) == -1) // 设置一个停止信号，当老爹delete的时候，要回收线程
    {
        perror("m_stopSignalFD init error");
        return false;
    }
    if (addToEpoll(m_stopSignalFD) == false) // 将m_stopSignalFD文件描述符加入到epoll中
    {
        printf("m_stopSignalFD add to epoll error\n");
        return false;
    }
    if (addToEpoll(m_wakeFD) == false) // 将m_wakeFD文件描述符加入到epoll中
    {
        printf("m_wakeFD add to epoll error\n");
        return false;
    }

    return true;
}

void SubReactor::addQueueConnFD(const ConInfo &info)
{
    mux.lock(); // 获取锁资源
    m_queueConnFD.push(info);
    mux.unlock(); // 释放锁资源
}

int SubReactor::getWakeFD()
{
    return m_wakeFD;
}

int SubReactor::getStopFD()
{
    return m_stopSignalFD;
}

void SubReactor::createEpollFD()
{
    m_epfd = epoll_create(1);
    if (m_epfd == -1)
    {
        perror("m_epfd create error");
        return;
    }
}

bool SubReactor::addToEpoll(int fd)
{
    // 已经存在，算添加成功
    if (m_cliSessionsMap.find(fd) != m_cliSessionsMap.end())
    {
        return true;
    }
    epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = fd;
    if (epoll_ctl(m_epfd, EPOLL_CTL_ADD, fd, &ev) == -1)
    {
        perror("in m_epfd, add error");
        return false;
    }

    return true;
}

void SubReactor::removeEpollFD(int fd)
{
    // 不存在，不需要删除
    if (m_cliSessionsMap.find(fd) == m_cliSessionsMap.end())
    {
        return;
    }

    if (epoll_ctl(m_epfd, EPOLL_CTL_DEL, fd, NULL) == -1)
    {
        perror("epoll add error");
        return;
    }
    delete m_cliSessionsMap[fd]; // ClientSession析构函数会close fd，不需要你再次close
    m_cliSessionsMap.erase(fd);
}
// 只被老爹点名一次，之后就是利用eventfd来通知有新客户连接
// 这里的函数参数用于测试，之后要记得删除
void SubReactor::reactor(int reactorId)
{

    while (1)
    {
        // 等待事件
        int num = epoll_wait(m_epfd, m_evs, sizeof(m_evs) / sizeof(m_evs[0]), -1);
        if (num == -1)
        {
            perror("m_epfd wait error");
            return;
        }
        printf("reactorId = %d 被唤醒了\n", reactorId);
        for (int i = 0; i < num; i++)
        {
            int newfd = m_evs[i].data.fd;
            // 说明父进程往m_wakeFD写东西了,也就是队列里有数据了
            if (newfd == m_wakeFD)
            {
                // 读取计数器
                uint64_t cnt;
                read(newfd, &cnt, sizeof(cnt)); // 消耗事件
                while (cnt-- != 0)
                {
                    mux.lock(); // 获取锁资源
                    ConInfo newConInfo = m_queueConnFD.front();
                    m_queueConnFD.pop();
                    mux.unlock(); // 释放锁资源
                    if (addToEpoll(newConInfo.m_fd))
                    {
                        // 创建session哈希表
                        m_cliSessionsMap.insert({newConInfo.m_fd,
                                                 new ClientSession(newConInfo.m_fd, &newConInfo.m_cin)});
                    }
                    else
                    {
                        close(newConInfo.m_fd);
                    }
                }
            }
            // 老爹发出了停止信号，该溜溜球了
            else if (newfd == m_stopSignalFD)
            {
                // 读取计数器
                uint64_t cnt;
                read(m_stopSignalFD, &cnt, sizeof(cnt)); // 消耗事件
                return;                                  // 直接return
            }
            else
            {
                // 如果不存在就下一个
                auto cli = m_cliSessionsMap.find(newfd);
                if (cli == m_cliSessionsMap.end())
                {
                    continue;
                }
                // 注意ClientSession里面封装了读写事件
                int res = cli->second->handle_read(); // 读取客户端的消息
                std::string &buf = cli->second->getRecvBuf();
                if (res == 0) // 对端下线了，将fd从epoll中DEL
                {
                    removeEpollFD(newfd);
                    continue;
                }
                else if (res == -1) // 有错误
                {
                    printf("handle_read error\n");
                    removeEpollFD(newfd);
                    continue;
                }
                while (1)
                {
                    // 将buf传给Decoder解析
                    DecodedFrame outputFrame;
                    DecoderStatus status = m_decoder.parseBufPacket(buf, outputFrame);
                    if (status == DecoderStatus::ProtocolError)
                    {
                        // 客户端发的数据有问题，关闭客户端
                        removeEpollFD(newfd);
                        break;
                    }
                    else if (status == DecoderStatus::NeedMoreData)
                    {
                        break;
                    }
                    else if (status == DecoderStatus::PacketReady)
                    {
                        // 处理frame，但具体还未实现，先写主要架构
                        // 主要实现的时候注意，outputFrame是局部变量

                        // 然后继续解析
                        continue;
                    }
                }
            }
        }
    }
}
