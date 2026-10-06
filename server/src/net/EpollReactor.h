#pragma once

#include "protocol/FrameDecoder.h"
#include "ClientSession.h"
#include "protocol/FrameEncoder.h"
#include <thread>
#include <sys/epoll.h>
#include <unordered_map>
#include <queue>
#include <vector>
#include <mutex>

constexpr int subMAXCnt = 4;
constexpr int subSessionMAXCnt = 64;

extern std::mutex mux;

// 定义新连接客户端信息结构体
struct ConInfo
{
    ConInfo(int fd, sockaddr_in cin)
        : m_fd(fd),
          m_cin(cin)
    {
    }
    int m_fd;
    sockaddr_in m_cin;
};

/*
    每个线程实例化自己的epoll，由主线程进行轮询调用
*/
class SubReactor
{
public:
    /*
        初始化成员变量
    */
    SubReactor();
    /*
        析构函数，用于关闭套接字
    */
    ~SubReactor();
    /*
        给主线程一个API
        唤醒 + 让主线程将新客户端添加至连接队列
    */
    bool addQueueConnFD(const ConInfo &info);
    /*
        给主线程一个获取唤醒文件描述符的API 在唤醒前，起码这个m_wakeFD初始化好了，也就是已经存在文件。
    */
    int getWakeFD(); // 给老爹一个获取你门铃的API

    /*
        给主线程一个发出停止信号的文件描述符的API 在发出信号之前，起码这个m_wakeFD初始化好了，也就是已经存在文件。
    */
    int getStopFD();

    /*
        用于初始化，方便查看哪些创建失败
    */
    bool initSub();

    /*
        如何处理如果不同线程之间的客户端若是一个群聊或私聊要如何通信 ----Q3

        用于外部调用reactor
        内部处理epoll_wait，并分发事件
        实现步骤
        1 创建epoll文件描述符
        2 利用eventfd创建m_wakeFD计数文件器描述符，注意，由于客户端通信用到了read，这里eventfd的flag参数要设置非阻塞
        3 将m_wakeFD加入epoll
        4 循环等待事件发生
            若是m_wakeFD事件发生，说明主线程产生了新连接
                读取里面的计数，根据计数进行获取queue.front和删				 除.pop()，并将其加入到epoll和map里
            若是客户端，则进行通信
    */
    void reactor(int reactorId);

private:
    /*
        实例化epoll文件描述符
    */
    void createEpollFD();
    /*
        将fd加入epoll中
    */
    bool addToEpoll(int fd);
    /*
        将fd从epoll删除
    */
    void removeEpollFD(int fd);

    /*
        修改fd属性
        参数
            int fd: 需要修改的文件描述符
            uint32_t events: 需要修改成什么样的属性
    */
    bool modifyEpollFD(int fd, uint32_t events);

    int m_wakeFD; // 用于老爹唤醒

    int m_epfd; // 每个线程独自的epoll套接字

    int m_stopSignalFD; // 用于老爹调用自己的析构函数，提醒孩子该退出线程了

    std::queue<ConInfo> m_queueConnFD; // 接收老爹的新连接

    std::unordered_map<int, ClientSession *> m_cliSessionsMap; // 这里用哈希表可以快速查询到，也便于插入和删除

    epoll_event m_evs[subSessionMAXCnt]; // 每个线程产生的文件描述符集合

    FrameDecoder m_decoder; // 解包器

    FrameEncoder m_encoder; // 装包器
};

/*
    该类只负责进行accpet，并管理分支线程SubReactor
*/
class EpollReactor
{
public:
    /*      为什么不能在这里初始化线程数组呢-----Q1
    构造函数，初始化m_sfd,m_subTimer,并实例化subReactors对象数组
    */
    EpollReactor();
    /*
        析构函数，delelte subReactors指针数组
        并分离所有线程
        注意这里一定不close(m_sfd)，由Tcp自己close
    */
    ~EpollReactor();
    /*为什么不让主线程进行epoll，然后再每个线程进行通信----Q2

        实现主线程做accept，每个线程实例化自己的epoll
        实现步骤:
        1 初始化线程数组m_threads，将每个subReactor的reactor函数作用于每个线程，线程等待主线程的唤醒
        2 主线程循环等待客户端的连接
        3 有客户端发来连接请求后，轮询通知sub,这里做个简单的m_subTimer++ % subMAXCnt后续有牛逼的再更改
        4 先将新的客户端信息加入 轮询到sub的连接队列里
        5 唤醒sub,也就是eventfd计数器+1.
    */
    void run();
    /*
        初始化主线程MainEpoll，
        这里的epoll用于检测新连接和停止事件
    */
    bool initMEpoll(int sfd);

    /*
        给控制端一个获取m_MStopFD的API接口
    */
    int getMStopFD();

private:
    /*
        创建主线程的epollFD文件描述符
    */
    void createMEpollFD();
    /*
        将fd加入epoll中
    */
    bool addToMEpoll(int fd);

    /*
        将新连接的客户端设置为非阻塞
    */
    int setNonblocking(int fd);

    int m_sfd; // 服务器的套接字由Tcp类创建好

    int m_subTimer; // 主线程的时间片，轮询调用子线程

    int m_MStopFD; // 主线程的停止事件

    int m_MEpfd; // 主线程的epollFD

    epoll_event m_MEvs[2]; // 主线程线程产生的文件描述符集合，目前就两个,之后还有就定义宏

    SubReactor *m_subReactors[subMAXCnt]; // 子线程处理函数

    std::vector<std::thread> m_threads; // 子线程数组
};