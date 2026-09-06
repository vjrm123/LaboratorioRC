/* Server code */

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
#include<vector>
#include<mutex>

using namespace std;

vector<int> clients;
mutex mtxClients;

void broadcastMensaje(const string& mensaje, int socketFD) {
    lock_guard<mutex> lock(mtxClients);
    for(int clientFD : clients) {
        if(clientFD != socketFD) {
            write(clientFD, mensaje.c_str(), mensaje.length());
        }
    }
}

void eliminarCliente(int socketFD)  {
    lock_guard<mutex> lock(mtxClients);
    clients.erase(remove(clients.begin(), clients.end(), socketFD), clients.end());
}

void manejarCliente(int socketFD) {
    char buffer[256];
    int n;
    {
        lock_guard<mutex> lock(mtxClients);
        clients.push_back(socketFD);
        cout << "[+] Cliente conectado. Total clientes: " << clients.size() << endl;
    }

    string bienvenida = "bienvenido al chat. Escribe 'salir' o 'exit' para desconectarte.";
    write(socketFD, bienvenida.c_str(), bienvenida.length());

    while(true) {
        bzero(buffer, 256);
        n = read(socketFD, buffer, 255);

        if( n <= 0) {
            cout << "[-] Cliente desconectado. Total clientes: " << clients.size() << endl;
            eliminarCliente(socketFD);
            break;
        }

        string mensaje(buffer);

        mensaje.erase(remove(mensaje.begin(), mensaje.end(), '\n'), mensaje.end());
        mensaje.erase(remove(mensaje.begin(), mensaje.end(), '\r'), mensaje.end());

        if(mensaje == "salir" || mensaje == "exit") {
            string despedida = "desconectado del chat.\n";
            write(socketFD, despedida.c_str(), despedida.length());
            eliminarCliente(socketFD);
            cout << "[-] Cliente desconectado. Total: " << clients.size() << endl;
            break;
        }

        cout << "cliente: " << mensaje << endl;
        broadcastMensaje(mensaje, socketFD);
    }

    shutdown(socketFD, SHUT_RDWR);
    close(socketFD);
}




int main(void)
{
    struct sockaddr_in stSockAddr;
    int SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    char buffer[256];
    int n;

    string os;

    if(-1 == SocketFD)
    {
        perror("can not create socket");
        exit(EXIT_FAILURE);
    }

    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));

    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(1100);
    stSockAddr.sin_addr.s_addr = INADDR_ANY;

    if(-1 == bind(SocketFD,
        (const struct sockaddr *)&stSockAddr,
        sizeof(struct sockaddr_in)))
    {
        perror("error bind failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    if(-1 == listen(SocketFD, 10))
    {
        perror("error listen failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    vector<thread> threads;

    for(;;) {
        int ConnectFD = accept(SocketFD, NULL, NULL);

        if(0 > ConnectFD) {
            perror("error accept failed");
            close(SocketFD);
            exit(EXIT_FAILURE);
        }
        threads.emplace_back(thread(manejarCliente, ConnectFD));
        threads.back().detach();
    }

    for(auto& t : threads) {
        if(t.joinable()) {
            t.join();
        }
    }

    close(SocketFD);
    return 0;
}