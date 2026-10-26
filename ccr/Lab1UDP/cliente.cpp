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
#include <errno.h> 

using namespace std;

#define TAM_PAQUETE 100
#define TAM_NICK    7
#define TAM_MSG     11

atomic<bool> conectado(true);
string miNickname;
int SocketFD;
struct sockaddr_in server_addr;

string zeroPad(int numero, int tamano) {
    string str = to_string(numero);
    if (str.length() >= (size_t)tamano) return str;
    return string(tamano - str.length(), '0') + str;
}

string empaquetar(char accion, const string& nick, const string& msg) {
    char buffer[TAM_PAQUETE];
    memset(buffer, 0, TAM_PAQUETE);

    int offset = 0;
    buffer[offset] = accion;
    offset += 1;

    int nickLen = nick.size();
    int msgLen = msg.size();
    
    if (nickLen + msgLen > 81) {
        if (nickLen > 81) { nickLen = 81; msgLen = 0; }
        else { msgLen = 81 - nickLen; }
    }

    string szNick = zeroPad(nickLen, TAM_NICK);
    memcpy(buffer + offset, szNick.c_str(), TAM_NICK);
    offset += TAM_NICK;

    memcpy(buffer + offset, nick.c_str(), nickLen);
    offset += nickLen;

    string szMsg = zeroPad(msgLen, TAM_MSG);
    memcpy(buffer + offset, szMsg.c_str(), TAM_MSG);
    offset += TAM_MSG;

    memcpy(buffer + offset, msg.c_str(), msgLen);

    return string(buffer, TAM_PAQUETE);
}

void desempaquetar(const char* buffer, int totalBytes, char& accion, string& nick, string& msg) {
    if (totalBytes != TAM_PAQUETE) return;

    int offset = 0;
    accion = buffer[offset];
    offset += 1;

    string strTamNick(buffer + offset, TAM_NICK);
    offset += TAM_NICK;
    int tamNick = atoi(strTamNick.c_str());

    nick = string(buffer + offset, tamNick);
    offset += tamNick;

    string strTamMsg(buffer + offset, TAM_MSG);
    offset += TAM_MSG;
    int tamMsg = atoi(strTamMsg.c_str());

    msg = string(buffer + offset, tamMsg);
}

void enviarMensaje(int S, char accion, const string& nick, const string& msg) {
    string data = empaquetar(accion, nick, msg);
    sendto(S, data.c_str(), data.size(), 0, 
           (const struct sockaddr *)&server_addr, sizeof(server_addr));
}

bool recibirMensaje(int S, char& accion, string& nick, string& msg) {
    char buffer[TAM_PAQUETE];
    socklen_t len = sizeof(server_addr);
    
    accion = '\0';
    nick.clear();
    msg.clear();

    int n = recvfrom(S, buffer, TAM_PAQUETE, 0, (struct sockaddr *)&server_addr, &len);
    
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return true; // Timeout
        return false;
    }
    if (n == 0) return false;

    if (n == TAM_PAQUETE) {
        desempaquetar(buffer, n, accion, nick, msg);
    }
    return true;
}

void recibirMensajes(int S) {
    char accion;
    string nick, msg;

    while (conectado) {
        if (!recibirMensaje(S, accion, nick, msg)) {
            cout << "\n Desconectado del servidor" << endl;
            conectado = false;
            break;
        }
        
        if (nick.empty() && msg.empty()) continue;

        if (accion == 'b') {
            cout << "\r\n" << nick << ": " << msg << endl;
            cout << "> " << flush;
        }
        else if (accion == 'm') {
            cout << "\r\n[Privado de " << nick << "]: " << msg << endl;
            cout << "> " << flush;
        }
        else if (accion == 'l') {
            cout << "\r\n[LISTA] " << msg << endl;
            cout << "> " << flush;
        }
    }
}

void enviarMensajes(int S) {
    string input;
    while (conectado) {
        cout << "> ";
        getline(cin, input);

        if (!conectado) break;
        if (input == "salir" || input == "/quit") {
            enviarMensaje(S, 'Q', miNickname, "");
            conectado = false;
            break;
        }
        if (input.empty()) continue;

        if (input == "/lista" || input == "/users") {
            enviarMensaje(S, 'L', miNickname, "");
            continue;
        }

        if (input[0] == '@') {
            size_t pos = input.find(' ');
            if (pos != string::npos) {
                string destino = input.substr(1, pos - 1);
                string texto = input.substr(pos + 1);
                enviarMensaje(S, 'M', miNickname, "@" + destino + " " + texto);
            } else {
                cout << " Formato: @nickname mensaje" << endl;
            }
        }
        else {
            enviarMensaje(S, 'B', miNickname, input);
        }
    }
}

int main(void) {
    SocketFD = socket(AF_INET, SOCK_DGRAM, 0);
    if (-1 == SocketFD) {
        perror("cannot create socket");
        exit(EXIT_FAILURE);
    }

    struct timeval tv;
    tv.tv_sec = 1;  
    tv.tv_usec = 0;
    setsockopt(SocketFD, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(1100);
    
    if (inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr) <= 0) {
        perror("invalid IP address");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    cout << "Ingresa tu NOMBRE: ";
    getline(cin, miNickname);

    if (miNickname.empty()) {
        close(SocketFD);
        return 1;
    }

    enviarMensaje(SocketFD, 'N', miNickname, "");
    cout << "servidor UDP" << endl;

    thread hiloRecibir(recibirMensajes, SocketFD);
    thread hiloEnviar(enviarMensajes, SocketFD);

    hiloRecibir.join();
    hiloEnviar.join();

    close(SocketFD);
    return 0;
}