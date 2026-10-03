#include "net/TcpServer.h"

int main(int argc, char const *argv[])
{

    TcpServer server;
    if(server.init())
    {
        server.start();
    }
    return 0;
}