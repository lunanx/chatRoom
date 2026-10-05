#pragma once

#include <iostream>
#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
/*
    该类用于与客户端通信

    这里留了getter用于经过数据库后服务器向客户端回消息，具体实现后续再说，先进行初步的通信
*/

class ClientSession
{
public:
    /*
        构造函数
    */
    ClientSession(int cfd, sockaddr_in *cin);
    /*
        析构函数，这里用于关闭客户端的套接字
    */
    ~ClientSession();
    /*
        将发送端字节流发送
    */
    int handle_write(const std::string& data);
    /*
        从接收端读取字节流，可能会读取A的一部分+B的一部分，所以每次读取不应该清空。
    */
    int handle_read();
    /*
        用于外部获取客户端套接字
    */
    int getClientSocket(); // 获取m_cfd
    /*
        用于外部获取客户端地址信息
    */
    sockaddr_in getAddr(); // 获取m_addr
    /*
        用于外部获取发送端数据
    */
    std::string getWriteBuf();
    /*
        用于外部获取接收端数据
    */
    std::string& getRecvBuf();


private:
    int m_cfd; // 客户端套接字

    std::string m_writeBuf; // 发送端缓冲区数据

    std::string m_recvBuf; // 接收端缓冲区数据

    struct sockaddr_in m_caddr; // 客户端地址信息
};
