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

#define TAMANIO_ACCION   1
#define TAMANIO_DE_NOMBRE 7
#define TAMANIO_DE_MSG  11

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
    string data;
    data += accion;
    data += zeroPad(nick.size(), TAMANIO_DE_NOMBRE);
    data += nick;
    data += zeroPad(msg.size(), TAMANIO_DE_MSG);
    data += msg;
    return data;
}

void desempaquetar(const char* buffer, int totalBytes, char& accion, string& nick, string& msg) {
    int offset = 0;
    if (offset >= totalBytes) return;
    accion = buffer[offset];
    offset += 1;

    if (offset + TAMANIO_DE_NOMBRE > totalBytes) return;
    string strTamNick(buffer + offset, TAMANIO_DE_NOMBRE);
    offset += TAMANIO_DE_NOMBRE;
    int tamNick = atoi(strTamNick.c_str());

    if (offset + tamNick > totalBytes) return;
    nick = string(buffer + offset, tamNick);
    offset += tamNick;

    if (offset + TAMANIO_DE_MSG > totalBytes) return;
    string strTamMsg(buffer + offset, TAMANIO_DE_MSG);
    offset += TAMANIO_DE_MSG;
    int tamMsg = atoi(strTamMsg.c_str());

    if (offset + tamMsg > totalBytes) return;
    msg = string(buffer + offset, tamMsg);
}

void enviarMensaje(int S, char accion, const string& nick, const string& msg) {
    string data = empaquetar(accion, nick, msg);
    sendto(S, data.c_str(), data.size(), 0, 
           (const struct sockaddr *)&server_addr, sizeof(server_addr));
}

bool recibirMensaje(int S, char& accion, string& nick, string& msg) {
    char buffer[1024];
    socklen_t len = sizeof(server_addr);
    
    accion = '\0';
    nick.clear();
    msg.clear();

    int n = recvfrom(S, buffer, sizeof(buffer), 0, (struct sockaddr *)&server_addr, &len);
    
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return true; 
        }
        return false;
    }
    if (n == 0) return false;

    desempaquetar(buffer, n, accion, nick, msg);
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
            cout << "\r\n[LISTA] " << msg << ", "<< endl;
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
    cout << " Conectado como: " << miNickname << " (UDP)" << endl;

    thread hiloRecibir(recibirMensajes, SocketFD);
    thread hiloEnviar(enviarMensajes, SocketFD);

    hiloRecibir.join();
    hiloEnviar.join();

    close(SocketFD);
    return 0;
}