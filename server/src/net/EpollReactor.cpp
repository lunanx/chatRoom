#include "EpollReactor.h"
#include <sys/eventfd.h>
#include <string.h>
#include <unistd.h>
#include "EpollReactor.h"
//------------------------EpollReactor----------------------
EpollReactor::EpollReactor(int sfd)
    : m_sfd(sfd),
      m_subTimer(0)
{
    for (int i = 0; i < subMAXCnt; i++)
    {
        subReactors[i] = new SubReactor();
        if(subReactors[i]->init() == false)
        {
            delete subReactors[i];
            subReactors[i] = nullptr;
        }
    }
}

EpollReactor::~EpollReactor()
{
    for (int i = 0; i < subMAXCnt; ++i)
    {
        //应该先向当前线程发送停止信号，否则delete后停止信号文件描述符就不在了
        uint64_t cnt = 1;
        write(subReactors[i]->getStopFD(), &cnt, sizeof(cnt));
        m_threads[i].join();

        if (subReactors[i] != nullptr)
        {
            delete subReactors[i];    // 释放单个对象
            subReactors[i] = nullptr; // 置空防止悬空指针
        }

    }
}

void EpollReactor::run()
{
    for(int i =0;i < subMAXCnt;i++)
    {
        if(subReactors[i] == nullptr)
        {
            printf("EpollReactor run error!\n");
        }
    }
    if (!m_threads.empty())
    {
        m_threads.clear();
    }
    // 创建线程，将线程和subReactor对应
    for (int i = 0; i < subMAXCnt; i++)
    {
        m_threads.emplace_back(&SubReactor::reactor, subReactors[i], i);
    }
    sockaddr_in cin;
    socklen_t socklen = sizeof(cin);
    int newfd;
    while (1)
    {
        socklen = sizeof(cin);
        newfd = accept(m_sfd, (sockaddr *)&cin, &socklen);
        if (newfd == -1)
        {
            perror("accept error");
            return;
        }
        printf("[%s:%d] accept success\n", inet_ntoa(cin.sin_addr), ntohs(cin.sin_port)); // 测试函数，开发后删除
        // 有新连接来了,轮询sub
        auto sub = subReactors[m_subTimer++ % subMAXCnt];
        // 儿子都在 wait阻塞呢，先将客户端信息放到他的连接队列中
        sub->addQueueConnFD(ConInfo{newfd, cin});

        uint64_t cnt = 1;
        // 唤醒儿子
        write(sub->getWakeFD(), &cnt, sizeof(cnt));
    }
}
//------------------------SubReactor----------------------
SubReactor::SubReactor()
    : m_epfd(-1),
      m_wakeFD(-1),
      m_stopSignalFD(-1),
      m_queueConnFD(),
      m_cliSessionsMap(),
      m_evs{}
{
}

SubReactor::~SubReactor()
{
    for(auto p:m_cliSessionsMap)
    {
        delete p.second;
    }
    m_cliSessionsMap.clear();
    close(m_wakeFD);
    close(m_stopSignalFD);
    close(m_epfd);
}

bool SubReactor::init()
{
    createEpollFD();
    if(m_epfd == -1)
    {
        return false;
    }
    if((m_wakeFD = eventfd(0, EFD_NONBLOCK)) == -1) // 设置一个门铃，老爹会敲门铃，参数二为了不让read函数阻塞
    {
        perror("m_wakeFD init error");
        return false;
    }
    if((m_stopSignalFD = eventfd(0,EFD_NONBLOCK)) == -1)// 设置一个停止信号，当老爹delete的时候，要回收线程
    {
        perror("m_stopSignalFD init error");
        return false;
    }
    if(addToEpoll(m_stopSignalFD, nullptr) == false)       // 将m_stopSignalFD文件描述符加入到epoll中
    {
        perror("m_stopSignalFD add to epoll error");
        return false;
    }
    if(addToEpoll(m_wakeFD, nullptr) == false)       // 将m_wakeFD文件描述符加入到epoll中
    {
        perror("m_wakeFD add to epoll error");
        return false;
    }
}

void SubReactor::addQueueConnFD(const ConInfo &info)
{
    m_queueConnFD.push(info);
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
        perror("epoll create error");
        return;
    }
}

bool SubReactor::addToEpoll(int fd, sockaddr_in *cin)
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
        perror("epoll add error");
        return false;
    }
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
    delete m_cliSessionsMap[fd]; //ClientSession析构函数会close fd，不需要你再次close
    m_cliSessionsMap.erase(fd);
}
// 只被老爹点名一次，之后就是利用eventfd来通知有新客户连接
// 这里的函数参数用于测试，之后要记得删除
void SubReactor::reactor(int reactorId)
{

    while (1)
    {
        int num = epoll_wait(m_epfd, m_evs, subSessionMAXCnt, -1);
        if (num == -1)
        {
            perror("epoll wait error");
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
                read(m_wakeFD, &cnt, sizeof(cnt)); // 消耗事件
                while (cnt-- != 0)
                {
                    ConInfo newConInfo = m_queueConnFD.front();
                    m_queueConnFD.pop();
                    addToEpoll(newConInfo.m_fd, &newConInfo.m_cin);
                    //创建session哈希表
                    m_cliSessionsMap.insert({newConInfo.m_fd,
                                             new ClientSession(newConInfo.m_fd, &newConInfo.m_cin)});
                }
            }
            // 老爹发出了停止信号，该溜溜球了
            else if(newfd == m_stopSignalFD)
            {
                // 读取计数器
                uint64_t cnt;
                read(m_stopSignalFD, &cnt, sizeof(cnt)); // 消耗事件
                return; //直接return
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
                // 下面都是测试用，下个阶段的时候删除，也就是在操作数据库的时候删除
                int res = cli->second->handle_read(); // 读取客户端的消息
                char* buf = cli->second->getbuf(); //得到的是m_buf指针
                if (res == 0)
                {
                    // 对端下线了，将fd从epoll中DEL
                    removeEpollFD(newfd);
                    continue;
                }
                else if(res == -1)
                {
                    continue;
                }
                printf("客户端发送的数据为:%s\n", buf);
            
                if(strlen(buf) >= (size_t)125)//如果strcat不了，就直接发笑脸反正是测试
                {
                    memset(buf,0,sizeof(buf));
                    strcpy(buf,"*_*");
                }
                else 
                {
                    strcat(buf, "*_*");//由于buf是m_buf指针，这个操作修改了buf,
                }
                cli->second->handle_write(buf, strlen(buf));
            }
        }
    }
}
