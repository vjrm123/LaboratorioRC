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
#include <mutex>

using namespace std;

#define TAM_PAQUETE 100
#define TAM_NICK    7
#define TAM_MSG     11

map<string, struct sockaddr_in> ListOfCli;
mutex mtxLista;
int ServerSocket;

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

    // Retorna exactamente 100 bytes, ignorando el \0 de los strings
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

void enviarMensaje(struct sockaddr_in destino, char accion, const string& nick, const string& msg) {
    string data = empaquetar(accion, nick, msg);
    sendto(ServerSocket, data.c_str(), data.size(), 0, 
           (const struct sockaddr *)&destino, sizeof(destino));
}

void broadcastMensaje(const string& nick, const string& msg, struct sockaddr_in emisor) {
    lock_guard<mutex> lock(mtxLista);
    for (auto& cliente : ListOfCli) {
        
        if (cliente.second.sin_port != emisor.sin_port || cliente.second.sin_addr.s_addr != emisor.sin_addr.s_addr) {
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

int main(void) {
    struct sockaddr_in server_addr;

    ServerSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (-1 == ServerSocket) {
        perror("can not create socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(ServerSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(1100);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (-1 == bind(ServerSocket, (const struct sockaddr *)&server_addr, sizeof(server_addr))) {
        perror("error bind failed");
        close(ServerSocket);
        exit(EXIT_FAILURE);
    }

    cout << " Servidor UDP " << endl;

    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        char buffer[TAM_PAQUETE];

        int n = recvfrom(ServerSocket, buffer, TAM_PAQUETE, 0, 
                         (struct sockaddr *)&client_addr, &client_len);
        
        if (n != TAM_PAQUETE) continue;

        char accion;
        string nickname, msg;
        desempaquetar(buffer, n, accion, nickname, msg);

        if (accion == 'N') {
            {
                lock_guard<mutex> lock(mtxLista);
                ListOfCli[nickname] = client_addr;
            }
            cout << " [+] " << nickname << " conectado. Total: " << ListOfCli.size() << endl;
            broadcastMensaje("-->", nickname + " se ha unido.", client_addr);
        }
        else if (accion == 'M') {
            if (!msg.empty() && msg[0] == '@') {
                size_t pos = msg.find(' ');
                if (pos != string::npos) {
                    string destino = msg.substr(1, pos - 1);
                    string texto = msg.substr(pos + 1);
                    cout << " " << nickname << " -> @" << destino << ": " << texto << endl;
                    enviarPrivado(destino, nickname, texto);
                }
            }
        }
        else if (accion == 'B') {
            cout << " " << nickname << ": " << msg << endl;
            broadcastMensaje(nickname, msg, client_addr);
        }
        else if (accion == 'L') {
            string lista = "Conectados (" + to_string(ListOfCli.size()) + "): ";
            {
                lock_guard<mutex> lock(mtxLista);
                for (auto& cliente : ListOfCli) {
                    lista += cliente.first + " ";
                }
            }
            cout << " " << nickname << " pidio la lista de conectados." << endl;
            enviarMensaje(client_addr, 'l', "Sistema", lista);
        }
        else if (accion == 'Q') {
            cout << " [-] " << nickname << " desconectado" << endl;
            eliminarCliente(nickname);
            broadcastMensaje("Sistema", nickname + " ha salido.", client_addr);
        }
    }

    close(ServerSocket);
    return 0;
}