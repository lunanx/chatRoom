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
      m_threads{}
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
            if ((size_t)i < m_threads.size()) // 确保进一步线程是存在的，否则可能会有未定义行为
            {
                if (m_threads[i].joinable())
                {
                    uint64_t cnt = 1;
                    write(m_subReactors[i]->getStopFD(), &cnt, sizeof(cnt));
                    m_threads[i].join();
                }
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
            if (errno == EINTR)
            {
                continue;
            }
            perror("m_MEpfd wait error");
            return;
        }
        for (int i = 0; i < num; i++)
        {
            int fd = m_MEvs[i].data.fd;
            // 说明有新连接产生
            if (fd == m_sfd)
            {
                socklen = sizeof(cin);
                while (1)
                {
                    socklen = sizeof(cin);
                    int newfd = accept(m_sfd, (sockaddr *)&cin, &socklen);
                    if (newfd == -1)
                    {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                        {
                            // 当前没有新连接
                            break;
                        }

                        if (errno == EINTR)
                        {
                            // 突然中断
                            continue;
                        }

                        perror("accept error");
                        break;
                    }
                    printf("[%s:%d] accept success\n", inet_ntoa(cin.sin_addr), ntohs(cin.sin_port)); // 测试函数，开发后删除
                    if (setNonblocking(newfd) == -1)
                    {
                        printf("setNonblock error\n");
                        close(newfd);
                        break;
                    }
                    // 有新连接来了,轮询sub
                    auto sub = m_subReactors[m_subTimer++ % subMAXCnt];
                    // 儿子都在 wait阻塞呢，先将客户端信息放到他的连接队列中
                    // 唤醒 + 入队
                    bool res = sub->addQueueConnFD(ConInfo{newfd, cin});
                    if (!res)
                    {
                        close(newfd);
                        continue;
                    }
                }
            }
            // 控制端发来了停止信号
            else if (fd == m_MStopFD)
            {
                uint64_t cnt;
                read(fd, &cnt, sizeof(cnt)); // 消耗事件
                return;                      // 直接返回
            }
        }
    }
}

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

bool SubReactor::addQueueConnFD(const ConInfo &info)
{
    mux.lock(); // 获取锁资源
    uint64_t cnt = 1;
    ssize_t res;
    do
    {
        res = write(m_wakeFD, &cnt, sizeof(cnt));
    } while (res == -1 && errno == EINTR);

    if (res == -1)
    {

        perror("m_wakeFD write error");
        mux.unlock(); // 这里一定要释放锁资源，不然就会死锁了
        return false;
    }

    if (res != sizeof(cnt))
    {
        printf("m_wakeFD write size error\n");
        mux.unlock(); // 这里一定要释放锁资源，不然就会死锁了
        return false;
    }

    m_queueConnFD.push(info);

    mux.unlock(); // 释放锁资源

    return true;
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
    ev.events = EPOLLIN | EPOLLRDHUP;
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
    }
    delete m_cliSessionsMap[fd]; // ClientSession析构函数会close fd，不需要你再次close
    m_cliSessionsMap.erase(fd);
}
bool SubReactor::modifyEpollFD(int fd, uint32_t events)
{
    // 不存在，不需要修改,算修改失败
    if (m_cliSessionsMap.find(fd) == m_cliSessionsMap.end())
    {
        return false;
    }

    epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(m_epfd, EPOLL_CTL_MOD, fd, &ev) == -1)
    {
        perror("EPOLL_CTL_MOD error");
        return false;
    }
    return true;
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
            if (errno == EINTR)
            {
                continue;
            }
            perror("m_epfd wait error");
            return;
        }
        printf("reactorId = %d 被唤醒了\n", reactorId);
        for (int i = 0; i < num; i++)
        {
            int newfd = m_evs[i].data.fd;
            uint32_t events = m_evs[i].events;
            // 说明父进程往m_wakeFD写东西了,也就是队列里有数据了
            if (newfd == m_wakeFD)
            {
                // 读取计数器
                uint64_t cnt = 0;
                ssize_t res;
                do
                {
                    res = read(newfd, &cnt, sizeof(cnt)); // 尝试消耗事件
                } while (res == -1 && errno == EINTR);

                // 还是有错误
                if (res == -1)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        // 当前没有待处理的eventfd计数
                        continue;
                    }

                    // eventfd本身出现了异常
                    perror("m_wakeFD read error");
                    return;
                }
                // res大小并不是要的计数
                if (res != sizeof(cnt))
                {
                    printf("m_wakeFD read size error\n");
                    return;
                }

                while (cnt-- != 0)
                {
                    std::unique_lock<std::mutex> lock(mux); // 获取锁资源
                    
                    if (m_queueConnFD.empty())
                    {
                        printf("wakeFD has count ,but connection queue may mismatch\n");\
                        break;// 离开作用域自动释放
                    }
                    ConInfo newConInfo = m_queueConnFD.front();
                    m_queueConnFD.pop();

                    lock.unlock(); // 释放锁资源 这里一定要手动释放
                    if (addToEpoll(newConInfo.m_fd))
                    {
                        // 创建session哈希表
                        m_cliSessionsMap.insert({newConInfo.m_fd,
                                                 new ClientSession(newConInfo.m_fd, newConInfo.m_cin)});
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
                uint64_t cnt = 0;
                ssize_t res;
                do
                {
                    res = read(newfd, &cnt, sizeof(cnt)); // 尝试消耗事件
                } while (res == -1 && errno == EINTR);

                // 还是有错误
                if (res == -1)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        // 当前没有待处理的eventfd计数
                        continue;
                    }

                    // eventfd本身出现了异常
                    perror("m_stopSignalFD read error");
                    return;
                }
                // res大小并不是要的计数
                if (res != sizeof(cnt))
                {
                    printf("m_stopSignalFD read size error\n");
                    return;
                }

                return; // 直接return
            }
            else
            {
                // 如果不存在就下一个
                auto it = m_cliSessionsMap.find(newfd);
                if (it == m_cliSessionsMap.end())
                {
                    continue;
                }
                // 注意ClientSession里面封装了读写事件
                auto cli = it->second;
                if (events & EPOLLERR)
                {
                    int error = 0;
                    socklen_t len = sizeof(error);
                    if (getsockopt(newfd, SOL_SOCKET, SO_ERROR, &error, &len) == -1)
                    {
                        perror("getsockopt error");
                    }
                    else
                    {
                        printf("EPOLLERR: socket error: %s\n", strerror(error));
                    }
                    removeEpollFD(newfd);
                    continue; // 继续下一个epoll
                }

                if (events & EPOLLHUP)
                {
                    printf("EPOLLHUP: connection hang up\n");
                    removeEpollFD(newfd);
                    continue; // 继续下一个epoll
                }

                if (events & (EPOLLIN | EPOLLRDHUP))
                {
                    int res = cli->handle_read(); // 读取客户端的消息
                    std::string &buf = cli->getRecvBuf();
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
                    // 客户端的消息没问题，开始解析
                    size_t offset = 0;
                    bool isRemoveFD = false; // 防止删除了对象，仍然继续使用
                    while (1)
                    {
                        // 将buf传给Decoder解析
                        protocolHeader::PacketFrame outputFrame;
                        DecoderStatus status = m_decoder.parseBufPacket(buf, outputFrame, offset);
                        if (status == DecoderStatus::ProtocolError)
                        {
                            // 客户端发的数据有问题，关闭客户端
                            removeEpollFD(newfd);
                            isRemoveFD = true;
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

                            //-------以下也是测试，是为了搭配测试--------

                            auto responseFrame = m_dispatcher.dispatch(outputFrame);
                            std::string data = m_encoder.buildBufPacket(responseFrame.m_command, responseFrame.m_requestId, nlohmann::json::parse(responseFrame.m_body));
                            res = cli->handle_write(data);
                            if (res == -1) // 有错误
                            {
                                printf("handle_write error\n");
                                removeEpollFD(newfd);
                                isRemoveFD = true;
                                break;
                            }
                            else if (res == 0) // 对端下线了，将fd从epoll中DEL
                            {
                                removeEpollFD(newfd);
                                isRemoveFD = true;
                                break;
                            }
                            else if (res == 1)
                            {
                                if (cli->hasWriteBufPending() == true)
                                {
                                    modifyEpollFD(newfd, EPOLLIN | EPOLLRDHUP | EPOLLOUT);
                                }
                                // 这里发送不出去，那我就直接break，epoll重新判断EPOLLOUT
                                break;
                            }
                            else if (res == 2)
                            {
                                // 虽然已经默认发送完了，但这里还是判断一下
                                if (cli->hasWriteBufPending() == false)
                                {
                                    modifyEpollFD(newfd, EPOLLIN | EPOLLRDHUP);
                                }
                                // 继续等待下一次解析
                                continue;
                            }

                            //-------测试结束

                            // 自己之后要TODO：
                            //  1待将outputFrame发送给`其他地方`
                            //  2继续解析 continue;
                        }
                    }
                    // FD已经被删除了，要跳过当前的epoll event
                    if (isRemoveFD)
                    {
                        continue; // 跳过当前
                    }
                    // 正常解析完的数据可以删除了
                    buf.erase(0, offset);
                }

                if (events & EPOLLOUT)
                {
                    // ---------------以下为测试！！！！！！----------------------

                    // 本来这里应该是吧outputframe给其他类进一步处理的，但因为还没设置其他类，没办法直接加入EPOLLOUT，就只想到这样测试了
                    // 发送空数据继续处理未发送的数据
                    int res = cli->handle_write("");
                    if (res == -1) // 有错误
                    {
                        printf("handle_write error\n");
                        removeEpollFD(newfd);
                        continue; // 继续下一个epoll
                    }
                    else if (res == 0) // 对端下线了，将fd从epoll中DEL
                    {
                        removeEpollFD(newfd);
                        continue; // 继续下一个epoll
                    }
                    else if (res == 2)
                    {
                        // 虽然已经默认发送完了，但这里还是判断一下
                        if (cli->hasWriteBufPending() == false)
                        {
                            modifyEpollFD(newfd, EPOLLIN | EPOLLRDHUP);
                        }
                        continue; // 继续下一个epoll
                    }

                    // res == 1的情况不需要在判断了，因为就是有EPOLLOUT才进入的这里。

                    //----------- 测试结束
                }
            }
        }
    }
}
