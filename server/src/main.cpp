#include "net/TcpServer.h"

int main()
{

    TcpServer server;
    if(server.init())
    {
        server.start();
    }
    return 0;
}