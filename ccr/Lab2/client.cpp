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
#include <atomic>

using namespace std;

atomic<bool> conectado(true);


void enviarConFormato(int socket, const string& mensaje) {
    char tipo = 'N';
    write(socket, &tipo, 1);
    
    uint32_t len = htonl(mensaje.length());
    write(socket, &len, 4);
    
    write(socket, mensaje.c_str(), mensaje.length());
}

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



void recibirMensajes(int socketFD) {
    while(conectado) {
       
        string mensaje = recibirConFormato(socketFD);

        if (mensaje.empty()) {
            cout << "\n[!] desconectado" << endl;
            conectado = false;
            break;
        }

        cout << "\r \n" << mensaje << endl;  
        cout << "cliente: " << flush;
    }
}

void enviarMensajes(int socketFD) {
    string os;

    while(conectado) {
        cout << "cliente: ";
        getline(cin, os);

        if(!conectado) break;
        if(os == "salir" || os == "exit") {
            enviarConFormato(socketFD, os);  
            conectado = false;
            break;
        }
        if(!os.empty()) {
            enviarConFormato(socketFD, os);  
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