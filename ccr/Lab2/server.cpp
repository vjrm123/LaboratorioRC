
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <mutex>
#include <algorithm>

using namespace std;

#define TAMANIO_ACCION   1
#define TAMANIO_DE_NOMBRE 7
#define TAMANIO_DE_MSG  11  

map<string, int> ListOfCli;
mutex mtxLista;

string zeroPad(int numero, int tamano) {
    string str = to_string(numero);
    if (str.length() >= (size_t)tamano)
        return str;
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

void enviarMensaje(int S, char accion, const string& nick, const string& msg) {
    string data = empaquetar(accion, nick, msg);
    write(S, data.c_str(), data.size());
}

bool recibirMensaje(int S, char& accion, string& nick, string& msg) {
    char buff[1000];
    int n, tamano;

    n = read(S, buff, 1);
    if (n <= 0) return false;
    accion = buff[0];

    n = read(S, buff, TAMANIO_DE_NOMBRE);
    if (n <= 0) return false;
    buff[n] = '\0';
    tamano = atoi(buff);

    n = read(S, buff, tamano);
    if (n <= 0) return false;
    buff[n] = '\0';
    nick = buff;

    n = read(S, buff, TAMANIO_DE_MSG);
    if (n <= 0) return false;
    buff[n] = '\0';
    tamano = atoi(buff);

    if (tamano > 0) {
        n = read(S, buff, tamano);
        if (n <= 0) return false;
        buff[n] = '\0';
        msg = buff;
    } else {
        msg = "";
    }

    return true;
}

void broadcastMensaje(const string& nick, const string& msg, int emisorSocket) {
    lock_guard<mutex> lock(mtxLista);
    for (auto& cliente : ListOfCli) {
        if (cliente.second != emisorSocket) {
            enviarMensaje(cliente.second, 'b', nick, msg);
        }
    }
}

void enviarPrivado(const string& destino, const string& nick, const string& msg) {
    lock_guard<mutex> lock(mtxLista);
    auto it = ListOfCli.find(destino);
    if (it != ListOfCli.end()) {
        enviarMensaje(it->second, 'm', nick, msg);
    }
}

void eliminarCliente(const string& nick) {
    lock_guard<mutex> lock(mtxLista);
    ListOfCli.erase(nick);
}


void manejarCliente(int S) {
    char accion;
    string nickname, msg;

    if (!recibirMensaje(S, accion, nickname, msg)) {
        close(S);
        return;
    }

    if (accion != 'N') {
        cout << " Error: se esperaba registro (N)" << endl;
        close(S);
        return;
    }

    {
        lock_guard<mutex> lock(mtxLista);
        ListOfCli[nickname] = S;
        cout << " " << nickname << " conectado. Total: " << ListOfCli.size() << endl;
    }

    broadcastMensaje("-->", nickname + " se ha unido.", S);

    while (true) {
        if (!recibirMensaje(S, accion, nickname, msg)) break;

        if (accion == 'M') {
            if (msg[0] == '@') {
                size_t pos = msg.find(' ');
                if (pos != string::npos) {
                    string destino = msg.substr(1, pos - 1);
                    string texto = msg.substr(pos + 1);
                    cout << nickname << " -> @" << destino << ": " << texto << endl;
                    enviarPrivado(destino, nickname, texto);
                }
            }
        }
        else if (accion == 'B') {
            cout << nickname << ": " << msg << endl;
            broadcastMensaje(nickname, msg, S);
        }
        else if (accion == 'Q') {
            cout << " " << nickname << " desconectado" << endl;
            eliminarCliente(nickname);
            broadcastMensaje("Sistema", nickname + " ha salido.", S);
            break;
        }
        else {
            cout << " Accion desconocida: " << accion << endl;
        }
    }

    eliminarCliente(nickname);
    shutdown(S, SHUT_RDWR);
    close(S);
}


int main(void) {
    struct sockaddr_in stSockAddr;
    int ServerSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (-1 == ServerSocket) {
        perror("can not create socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(ServerSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(1100);
    stSockAddr.sin_addr.s_addr = INADDR_ANY;

    if (-1 == bind(ServerSocket, (const struct sockaddr *)&stSockAddr, 
                   sizeof(struct sockaddr_in))) {
        perror("error bind failed");
        close(ServerSocket);
        exit(EXIT_FAILURE);
    }

    if (-1 == listen(ServerSocket, 10)) {
        perror("error listen failed");
        close(ServerSocket);
        exit(EXIT_FAILURE);
    }

    cout << " Esperando clientes..." << endl;

    for (;;) {
        int ClientSocket = accept(ServerSocket, NULL, NULL);
        if (0 > ClientSocket) {
            perror("error accept failed");
            continue;
        }
        thread(manejarCliente, ClientSocket).detach();
    }

    close(ServerSocket);
    return 0;
}