#pragma once

#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "EpollReactor.h"
#define SERVER_HOST_PORT 8888
#define SERVER_HOST_ADDR "192.168.230.128"

/*
    TcpServer用于初始化服务器
    只做socket()、bind()、listen()
    accept给epollReactor类
    通信给clientSession类
*/

class TcpServer
{
public:
    TcpServer();
    ~TcpServer();
    /*
        初始化服务器
    */
    void init();
    /*
        关闭服务器
    */
    void stop();

private:
    int myBind();

    int myListen();

    int m_sfd; // 服务器套接字

    bool m_isInit; // 看看服务器是否初始化成功

    struct sockaddr_in m_saddr; // 服务器地址信息

    EpollReactor *m_epollReactor; // EpollReactor对象，用于连接客户端
};
