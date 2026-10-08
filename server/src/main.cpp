#include "net/TcpServer.h"
#include <sodium.h>

int main()
{
    if(sodium_init() < 0)
    {
        return 1;
    }

    TcpServer server;
    if(server.init())
    {
        server.start();
    }
    return 0;
}