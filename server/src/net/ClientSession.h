#pragma once

#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFSIZE 128

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
    ClientSession(int cfd,sockaddr_in* cin);
    /*
    	析构函数，这里用于关闭客户端的套接字
    */
    ~ClientSession();
    /*
    	服务端向客户端发送len长度的buf数据,目前阶段只做简单测试
    	后续得考虑JSON数据
    */
    void handle_write(const char *buf,size_t len); 
    /*
    	服务端从客户端接收数据，目前阶段只做简单测试
    	后续得考虑JSON数据
    */
    int handle_read();
	/*
		用于外部获取客户端套接字
	*/
    int getClientSocket();//获取m_cfd
	/*
		用于外部获取客户端地址信息
	*/
    sockaddr_in getAddr();//获取m_addr
	/*
		用于外部获取客户端的消息
	*/
    char *getbuf();//获取m_buf
private:
    int m_cfd;//客户端套接字

    char m_buf[BUFSIZE];//当前阶段用char* 类型进行简单通信测试，后续要改成JSON

    struct sockaddr_in m_caddr;//客户端地址信息

};
