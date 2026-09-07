
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
#include <thread>
#include <vector>
#include <mutex>
#include <algorithm>

using namespace std;

vector<int> clients;
mutex mtxClients;


void enviarConFormato(int socket, const string& mensaje) {
    char tipo = 'N';
    write(socket, &tipo, 1);
    
    uint32_t len = htonl(mensaje.length());
    write(socket, &len, 4);
    
    write(socket, mensaje.c_str(), mensaje.length());
}

// Recibir mensaje con formato: [TIPO][TAMAÑO 4B][MENSAJE]
string recibirConFormato(int socket) {
    char buffer[1024];
    int n;
    
    bzero(buffer, 1);
    n = read(socket, buffer, 1);
    if (n <= 0) return "";
    char tipo = buffer[0];

    uint32_t len;
    n = read(socket, &len, 4);
    if (n <= 0) return "";
    len = ntohl(len);
    
    if (len > 1023) {  

        char* mensaje = new char[len + 1];
        bzero(mensaje, len + 1);
        n = read(socket, mensaje, len);
        if (n <= 0) {
            delete[] mensaje;
            return "";
        }

        mensaje[len] = '\0';
        string resultado(mensaje);
        delete[] mensaje;
        return resultado;

    } else {

        bzero(buffer, len + 1);
        n = read(socket, buffer, len);
        if (n <= 0) return "";
        buffer[len] = '\0';
        return string(buffer);

    }
}


void broadcastMensaje(const string& mensaje, int socketFD) {
    lock_guard<mutex> lock(mtxClients);
    for(int clientFD : clients) {
        if(clientFD != socketFD) {
            enviarConFormato(clientFD, mensaje);  
        }
    }
}

void eliminarCliente(int socketFD) {
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
    enviarConFormato(socketFD, bienvenida);  // CAMBIADO

    while(true) {
        string mensaje = recibirConFormato(socketFD);

        if (mensaje.empty()) {
            cout << "[-] Cliente desconectado. Total clientes: " << clients.size() << endl;
            eliminarCliente(socketFD);
            break;
        }

        if(mensaje == "salir" || mensaje == "exit") {
            string despedida = "desconectado del chat.\n";
            enviarConFormato(socketFD, despedida);  // CAMBIADO
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