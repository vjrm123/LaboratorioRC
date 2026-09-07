
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

#define TAMANIO_ACCION 1
#define TAMANIO_NICKNAME 7
#define TAMANIO_MENSAJE 11
#define TAMANIO_TOTAL (TAMANIO_ACCION + TAMANIO_NICKNAME + TAMANIO_MENSAJE)

atomic<bool> conectado(true);
string miNickname;

void empaquetarMensaje(char* buffer, char accion, const string& nickname, const string& mensaje) {
    bzero(buffer, TAMANIO_TOTAL);
    buffer[0] = accion;
    strncpy(buffer + 1, nickname.c_str(), TAMANIO_NICKNAME);
    strncpy(buffer + 1 + TAMANIO_NICKNAME, mensaje.c_str(), TAMANIO_MENSAJE);
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


void recibirMensajes(int socketFD) {
    char accion;
    string nickname, mensaje;
    
    while(conectado) {
        if (!recibirMensaje(socketFD, accion, nickname, mensaje)) {
            cout << "\n[!] Desconectado del servidor" << endl;
            conectado = false;
            break;
        }
        
        if (accion == 'B') {
            cout << "\r\n" << nickname << ": " << mensaje << endl;
        } else if (accion == 'M') {
            cout << "\r\n[" << nickname << "] " << mensaje << endl;
        } else {
            cout << "\r\n[Sistema] " << mensaje << endl;
        }
        cout << "> " << flush;
    }
}

void enviarMensajes(int socketFD) {
    string input;
    
    while(conectado) {
        cout << "> ";
        getline(cin, input);
        
        if (!conectado) break;
        
        if (input == "/quit" || input == "salir") {
            enviarMensaje(socketFD, 'Q', miNickname, "Cerrando...");
            conectado = false;
            break;
        }
        
        if (input.empty()) continue;
        
        if (input[0] == '@') {
            size_t pos = input.find(' ');
            if (pos != string::npos) {
                string destino = input.substr(1, pos - 1);
                string mensaje = input.substr(pos + 1);
                enviarMensaje(socketFD, 'M', miNickname, 
                              "@" + destino + " " + mensaje);
            }
        } else {
            
            enviarMensaje(socketFD, 'M', miNickname, input);
        }
    }
}


int main(void) {
    struct sockaddr_in stSockAddr;
    int SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (-1 == SocketFD) {
        perror("cannot create socket");
        exit(EXIT_FAILURE);
    }

    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(1100);

    if (inet_pton(AF_INET, "127.0.0.1", &stSockAddr.sin_addr) <= 0) {
        perror("invalid address");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    if (-1 == connect(SocketFD, (const struct sockaddr *)&stSockAddr, 
                      sizeof(struct sockaddr_in))) {
        perror("connect failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    cout << "Ingresa tu nickname (max 7 caracteres): ";
    getline(cin, miNickname);
    if (miNickname.length() > TAMANIO_NICKNAME) {
        miNickname = miNickname.substr(0, TAMANIO_NICKNAME);
    }

    enviarMensaje(SocketFD, 'N', miNickname, "Conectando...");
    
    cout << "[!] Conectado como: " << miNickname << endl;
    cout << "[!] Comandos: /quit para salir, @nick mensaje para privado" << endl;
    cout << "=====================================" << endl;

    thread hiloRecibir(recibirMensajes, SocketFD);
    thread hiloEnviar(enviarMensajes, SocketFD);

    hiloRecibir.join();
    hiloEnviar.join();

    shutdown(SocketFD, SHUT_RDWR);
    close(SocketFD);

    return 0;
}