#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <iostream>
#include <string>

using namespace std;

int main(void)
{
    struct sockaddr_in stSockAddr;
    int Res;
    int SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    int n;

    char buffer[256];
    string os;

    if (-1 == SocketFD)
    {
        perror("cannot create socket");
        exit(EXIT_FAILURE);
    }

    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));

    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(1100);

    Res = inet_pton(AF_INET, "127.0.0.1", &stSockAddr.sin_addr);

    if (0 > Res)
    {
        perror("error: first parameter is not a valid address family");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }
    else if (0 == Res)
    {
        perror("invalid IP address");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    if (-1 == connect(SocketFD,(const struct sockaddr *)&stSockAddr,sizeof(struct sockaddr_in)))
    {
        perror("connect failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    while (true) {
        

        cout << "cliente: ";
        getline(cin,os);
        n = write(SocketFD, os.c_str(), os.length());
        bzero(buffer, 256);
        n = read(SocketFD, buffer, 255);
        cout << "servidor: " << buffer << endl;
    
    }

    shutdown(SocketFD, SHUT_RDWR);
    close(SocketFD);

    return 0;
}