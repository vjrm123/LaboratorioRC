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
#include<thread>
#include<atomic>

using namespace std;

atomic<bool> conectado(true);

void recibirMensajes(int socketFD){
    char buffer[256];
    int n;

    while(conectado) {
        bzero(buffer,256);
        n = read(socketFD, buffer,255);

        if(n <= 0){
            cout << "\n[!] desconectado" << endl;
            conectado = false;
            break;
        }

        cout << "\ncliente: " << buffer << endl;
        cout << "cliente: " << flush << endl;
    }
}

void enviarMensajes(int socketFD) {
    string os;

    while(conectado) {
        cout << "cliente: ";
        getline(cin,os);

        if(!conectado) break;
        if(os == "salir" || os == "exit") {
            write(socketFD, os.c_str(), os.length());
            conectado = false;
            break;
        }
        if(!os.empty()) {
            write(socketFD, os.c_str(), os.length());
        }
    }
}
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

    cout << "[!] conectado. escribe 'salir' o 'exit' para desconectarte" << endl;
    thread hiloRecibir(recibirMensajes, SocketFD);
    thread hiloEnviar(enviarMensajes, SocketFD);

    hiloRecibir.join();
    hiloEnviar.join();

    shutdown(SocketFD, SHUT_RDWR);
    close(SocketFD);

    return 0;
}