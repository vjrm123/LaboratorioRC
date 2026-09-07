
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
#include <map>
#include <mutex>
#include <algorithm>

using namespace std;

#define TAMANIO_ACCION 1
#define TAMANIO_NICKNAME 7
#define TAMANIO_MENSAJE 11
#define TAMANIO_TOTAL (TAMANIO_ACCION + TAMANIO_NICKNAME + TAMANIO_MENSAJE)

struct ClienteInfo {
    int socket;
    string nickname;
};

map<string, int> listaDeClientes;
mutex mtxClientes;


void empaquetarMensaje(char* buffer, char accion, const string& nickname, const string& mensaje) {
    bzero(buffer, TAMANIO_TOTAL);
    
    buffer[0] = accion;
    
    int nickLen = nickname.length();
    if (nickLen > TAMANIO_NICKNAME) nickLen = TAMANIO_NICKNAME;
    strncpy(buffer + 1, nickname.c_str(), nickLen);
    
    int msgLen = mensaje.length();
    if (msgLen > TAMANIO_MENSAJE) msgLen = TAMANIO_MENSAJE;
    strncpy(buffer + 1 + TAMANIO_NICKNAME, mensaje.c_str(), msgLen);
}

void desempaquetarMensaje(const char* buffer, char& accion,string& nickname, string& mensaje) {

    accion = buffer[0];
    
    char nick[TAMANIO_NICKNAME + 1] = {0};
    strncpy(nick, buffer + 1, TAMANIO_NICKNAME);
    nickname = string(nick);

    nickname.erase(nickname.find_last_not_of(' ') + 1);
    
    char msg[TAMANIO_MENSAJE + 1] = {0};
    strncpy(msg, buffer + 1 + TAMANIO_NICKNAME, TAMANIO_MENSAJE);
    mensaje = string(msg);
    mensaje.erase(mensaje.find_last_not_of(' ') + 1);
}

void enviarMensaje(int socket, char accion, const string& nickname, const string& mensaje) {
    char buffer[TAMANIO_TOTAL];
    empaquetarMensaje(buffer, accion, nickname, mensaje);
    write(socket, buffer, TAMANIO_TOTAL);
}

bool recibirMensaje(int socket, char& accion,string& nickname, string& mensaje) {
    char buffer[TAMANIO_TOTAL];
    bzero(buffer, TAMANIO_TOTAL);
    int n = read(socket, buffer, TAMANIO_TOTAL);
    if (n <= 0) return false;
    desempaquetarMensaje(buffer, accion, nickname, mensaje);
    return true;
}


void broadcastMensaje(char accion, const string& nickname, const string& mensaje, int socketFD) {
    lock_guard<mutex> lock(mtxClientes);
    for(const auto& cliente : listaDeClientes) {
        if(cliente.second != socketFD) {
            enviarMensaje(cliente.second, accion, nickname, mensaje);
        }
    }
}

void enviarMensajePrivado(const string& destino, char accion,
                          const string& nickname, const string& mensaje) {
    lock_guard<mutex> lock(mtxClientes);
    auto it = listaDeClientes.find(destino);
    if (it != listaDeClientes.end()) {
        enviarMensaje(it->second, accion, nickname, mensaje);
    }
}

void eliminarCliente(const string& nickname) {
    lock_guard<mutex> lock(mtxClientes);
    auto it = listaDeClientes.find(nickname);
    if (it != listaDeClientes.end()) {
        close(it->second);
        listaDeClientes.erase(it);
    }
}

void manejarCliente(int socketFD) {
    char accion;
    string nickname, mensaje;
    
    if (!recibirMensaje(socketFD, accion, nickname, mensaje)) {
        close(socketFD);
        return;
    }

    if (accion != 'N') {
        cout << "[-] Error: Se esperaba registro (N)" << endl;
        close(socketFD);
        return;
    }
    
    {
        lock_guard<mutex> lock(mtxClientes);
        listaDeClientes[nickname] = socketFD;
        cout << "[+] " << nickname << " se ha conectado. Total: " 
             << listaDeClientes.size() << endl;
    }
    
    broadcastMensaje('B', "Sistema", nickname + " se ha unido al chat.", socketFD);
    
    while(true) {
        if (!recibirMensaje(socketFD, accion, nickname, mensaje)) {
            break;
        }
        
        switch(accion) {
            case 'M':  
                cout << nickname << ": " << mensaje << endl;
                broadcastMensaje('M', nickname, mensaje, socketFD);
                break;
                
            case 'Q':  
                cout << "[-] " << nickname << " se ha desconectado" << endl;
                eliminarCliente(nickname);
                broadcastMensaje('B', "Sistema", 
                                 nickname + " ha salido del chat.", socketFD);
                close(socketFD);
                return;
                
            case 'B':  
                cout << "[BROADCAST] " << nickname << ": " << mensaje << endl;
                broadcastMensaje('B', nickname, mensaje, socketFD);
                break;
                
            default:
                cout << "[!] Acción desconocida: " << accion << endl;
                break;
        }
    }
    
    eliminarCliente(nickname);
    broadcastMensaje('B', "Sistema", nickname + " ha salido del chat.", socketFD);
    close(socketFD);
}


int main(void) {
    struct sockaddr_in stSockAddr;
    int SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    
    if(-1 == SocketFD) {
        perror("can not create socket");
        exit(EXIT_FAILURE);
    }
    
    int opt = 1;
    setsockopt(SocketFD, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(1100);
    stSockAddr.sin_addr.s_addr = INADDR_ANY;
    
    if(-1 == bind(SocketFD, (const struct sockaddr *)&stSockAddr, 
                  sizeof(struct sockaddr_in))) {
        perror("error bind failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }
    
    if(-1 == listen(SocketFD, 10)) {
        perror("error listen failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }
    
    cout << "[+] Servidor iniciado en puerto 1100" << endl;
    cout << "[+] Protocolo: " << TAMANIO_TOTAL << " bytes por mensaje" << endl;
    cout << "[+] Esperando clientes..." << endl;
    
    vector<thread> threads;
    
    for(;;) {
        int ConnectFD = accept(SocketFD, NULL, NULL);
        if(0 > ConnectFD) {
            perror("error accept failed");
            continue;
        }
        threads.emplace_back(manejarCliente, ConnectFD);
        threads.back().detach();
    }
    
    close(SocketFD);
    return 0;
}