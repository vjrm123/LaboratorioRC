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

using namespace std;

#define TAM_PAQUETE             100
#define TAMANIO_DE_NOMBRE       7
#define TAMANIO_DE_MSG          11
#define TAMANIO_NICK_ARCHIVO    13
#define TAMANIO_NOMBRE_ARCHIVO  13
#define TAMANIO_ARCHIVO         25
#define TAMANIO_NICK_DATOS      2
#define TAMANIO_SECUENCIA       7
#define TAMANIO_DATOS           2

map<string, struct sockaddr_in> ListOfCli;    
int ServerSocket;




string zeroPad(long long numero, int tamano) {
    string str = to_string(numero);
    if (str.length() >= (size_t)tamano)
        return str;
    return string(tamano - str.length(), '0') + str;
}


string completar100(const string& data) {
    if (data.size() > TAM_PAQUETE) return "";
    return data + string(TAM_PAQUETE - data.size(), '\0');
}


string mostrar(const string& paquete) {
    int usado = paquete.size();
    while (usado > 0 && paquete[usado - 1] == '\0') usado--;

    string r;
    for (int i = 0; i < usado; i++) {
        unsigned char c = paquete[i];
        r += (c >= 32 && c < 127) ? (char)c : '.';
    }
    if (usado < (int)paquete.size())
        r += "  [+" + to_string(paquete.size() - usado) + " bytes de relleno]";
    return r;
}

// Leen un campo del paquete desde la posicion 'pos' y la avanzan
bool leerNumero(const char* buff, int& pos, int tamano, long long& numero) {
    if (pos + tamano > TAM_PAQUETE) return false;
    numero = atoll(string(buff + pos, tamano).c_str());
    pos += tamano;
    return true;
}

bool leerTexto(const char* buff, int& pos, long long tamano, string& texto) {
    if (tamano < 0 || pos + tamano > TAM_PAQUETE) return false;
    texto = string(buff + pos, tamano);
    pos += tamano;
    return true;
}

bool mismaDireccion(struct sockaddr_in a, struct sockaddr_in b) {
    return a.sin_port == b.sin_port && a.sin_addr.s_addr == b.sin_addr.s_addr;
}

string nickDe(struct sockaddr_in addr) {
    for (auto& cliente : ListOfCli)
        if (mismaDireccion(cliente.second, addr)) return cliente.first;
    return "";
}




string empaquetar(char accion, const string& nick, const string& msg) {
    int maxMsg = TAM_PAQUETE - 1 - TAMANIO_DE_NOMBRE - nick.size() - TAMANIO_DE_MSG;
    string texto = msg.substr(0, max(maxMsg, 0));      // cortar si no cabe

    string data;
    data += accion;
    data += zeroPad(nick.size(), TAMANIO_DE_NOMBRE);
    data += nick;
    data += zeroPad(texto.size(), TAMANIO_DE_MSG);
    data += texto;
    return completar100(data);
}

bool desempaquetar(const char* buff, char& accion, string& nick, string& msg) {
    int pos = 1;
    long long tamano;
    accion = buff[0];

    if (!leerNumero(buff, pos, TAMANIO_DE_NOMBRE, tamano)) return false;
    if (!leerTexto(buff, pos, tamano, nick)) return false;

    if (!leerNumero(buff, pos, TAMANIO_DE_MSG, tamano)) return false;
    if (!leerTexto(buff, pos, tamano, msg)) return false;

    return true;
}



string empaquetarInicioArchivo(const string& nick, const string& nombre, long long tamArchivo) {
    string data;
    data += 'F';
    data += zeroPad(nick.size(), TAMANIO_NICK_ARCHIVO);
    data += nick;
    data += zeroPad(nombre.size(), TAMANIO_NOMBRE_ARCHIVO);
    data += nombre;
    data += zeroPad(tamArchivo, TAMANIO_ARCHIVO);
    return completar100(data);
}

bool desempaquetarInicioArchivo(const char* buff, string& nick, string& nombre, long long& tamArchivo) {
    int pos = 1;
    long long tamano;

    if (!leerNumero(buff, pos, TAMANIO_NICK_ARCHIVO, tamano)) return false;
    if (!leerTexto(buff, pos, tamano, nick)) return false;

    if (!leerNumero(buff, pos, TAMANIO_NOMBRE_ARCHIVO, tamano)) return false;
    if (!leerTexto(buff, pos, tamano, nombre)) return false;

    return leerNumero(buff, pos, TAMANIO_ARCHIVO, tamArchivo);
}


string empaquetarDatosArchivo(const string& nick, long long secuencia, const string& datos) {
    string data;
    data += 'D';
    data += zeroPad(nick.size(), TAMANIO_NICK_DATOS);
    data += nick;
    data += zeroPad(secuencia, TAMANIO_SECUENCIA);
    data += zeroPad(datos.size(), TAMANIO_DATOS);
    data += datos;
    return completar100(data);
}

bool desempaquetarDatosArchivo(const char* buff, string& nick, long long& secuencia, string& datos) {
    int pos = 1;
    long long tamano;

    if (!leerNumero(buff, pos, TAMANIO_NICK_DATOS, tamano)) return false;
    if (!leerTexto(buff, pos, tamano, nick)) return false;

    if (!leerNumero(buff, pos, TAMANIO_SECUENCIA, secuencia)) return false;

    if (!leerNumero(buff, pos, TAMANIO_DATOS, tamano)) return false;
    return leerTexto(buff, pos, tamano, datos);
}


string empaquetarFinArchivo(const string& nick, long long totalTrozos) {
    string data;
    data += 'E';
    data += zeroPad(nick.size(), TAMANIO_NICK_DATOS);
    data += nick;
    data += zeroPad(totalTrozos, TAMANIO_SECUENCIA);
    return completar100(data);
}

bool desempaquetarFinArchivo(const char* buff, string& nick, long long& totalTrozos) {
    int pos = 1;
    long long tamano;

    if (!leerNumero(buff, pos, TAMANIO_NICK_DATOS, tamano)) return false;
    if (!leerTexto(buff, pos, tamano, nick)) return false;

    return leerNumero(buff, pos, TAMANIO_SECUENCIA, totalTrozos);
}




void enviarPaquete(struct sockaddr_in destino, const string& paquete) {
    if (paquete.size() != TAM_PAQUETE) return;
    cout << " >> ENVIADO  a " << nickDe(destino) << ": " << mostrar(paquete) << endl;
    sendto(ServerSocket, paquete.c_str(), TAM_PAQUETE, 0,
           (const struct sockaddr *)&destino, sizeof(destino));
}

void enviarMensaje(struct sockaddr_in destino, char accion, const string& nick, const string& msg) {
    enviarPaquete(destino, empaquetar(accion, nick, msg));
}




void broadcastMensaje(const string& nick, const string& msg, struct sockaddr_in emisor) {
    for (auto& cliente : ListOfCli)
        if (!mismaDireccion(cliente.second, emisor))
            enviarMensaje(cliente.second, 'b', nick, msg);
}

void enviarPrivado(const string& destino, const string& nick, const string& msg, struct sockaddr_in emisor) {
    auto it = ListOfCli.find(destino);
    if (it != ListOfCli.end())
        enviarMensaje(it->second, 'm', nick, msg);
    else
        enviarMensaje(emisor, 'm', "Sistema", "El usuario " + destino + " no existe.");
}


void enviarLista(struct sockaddr_in destino) {
    int capacidad = TAM_PAQUETE - 1 - TAMANIO_DE_NOMBRE - 7 - TAMANIO_DE_MSG;   
    string lista;

    for (auto& cliente : ListOfCli) {
        string item = lista.empty() ? cliente.first : ", " + cliente.first;
        if ((int)(lista.size() + item.size()) > capacidad) {
            enviarMensaje(destino, 'l', "Sistema", lista);
            lista = "";
            item = cliente.first;
        }
        lista += item;
    }
    enviarMensaje(destino, 'l', "Sistema", lista);
}


void reenviarArchivo(const char* buff, struct sockaddr_in emisor) {
    string origen = nickDe(emisor);
    if (origen.empty()) return;

    string destino, paquete;

    if (buff[0] == 'F') {
        string nombre;
        long long tamArchivo;
        if (!desempaquetarInicioArchivo(buff, destino, nombre, tamArchivo)) return;
        paquete = empaquetarInicioArchivo(origen, nombre, tamArchivo);
        cout << " " << origen << " -> " << destino << ": archivo " << nombre << " (" << tamArchivo << " bytes)" << endl;
    }
    else if (buff[0] == 'D') {
        string datos;
        long long secuencia;
        if (!desempaquetarDatosArchivo(buff, destino, secuencia, datos)) return;
        paquete = empaquetarDatosArchivo(origen, secuencia, datos);
    }
    else if (buff[0] == 'E') {
        long long totalTrozos;
        if (!desempaquetarFinArchivo(buff, destino, totalTrozos)) return;
        paquete = empaquetarFinArchivo(origen, totalTrozos);
    }

    auto it = ListOfCli.find(destino);
    if (it == ListOfCli.end()) {
        if (buff[0] == 'F')
            enviarMensaje(emisor, 'm', "Sistema", "Archivo no enviado: " + destino + " no existe.");
        return;
    }
    if (paquete.empty()) {
        if (buff[0] == 'F')
            enviarMensaje(emisor, 'm', "Sistema", "Archivo no enviado: nombre muy largo.");
        return;
    }
    enviarPaquete(it->second, paquete);
}

void eliminarCliente(const string& nick) {
    ListOfCli.erase(nick);
}




int main(void) {
    struct sockaddr_in stSockAddr;

    ServerSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (-1 == ServerSocket) {
        perror("can not create socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(ServerSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    int tamBuffer = 4 * 1024 * 1024;   // buffer grande para no perder trozos de archivo
    setsockopt(ServerSocket, SOL_SOCKET, SO_RCVBUF, &tamBuffer, sizeof(tamBuffer));

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

    cout << " Servidor UDP esperando paquetes de " << TAM_PAQUETE << " bytes..." << endl;

    for (;;) {
        char buff[TAM_PAQUETE];
        struct sockaddr_in emisor;
        socklen_t largo = sizeof(emisor);

        int n = recvfrom(ServerSocket, buff, TAM_PAQUETE, 0,
                         (struct sockaddr *)&emisor, &largo);
        if (n != TAM_PAQUETE) {
            if (n > 0) cout << " Paquete ignorado: mide " << n << " bytes" << endl;
            continue;
        }

        string quien = nickDe(emisor);
        cout << " << RECIBIDO de " << (quien.empty() ? "?" : quien) << ": "<< mostrar(string(buff, TAM_PAQUETE)) << endl;


        if (buff[0] == 'F' || buff[0] == 'D' || buff[0] == 'E') {
            reenviarArchivo(buff, emisor);
            continue;
        }

        
        char accion;
        string nickname, msg;
        if (!desempaquetar(buff, accion, nickname, msg)) continue;

        if (accion == 'N') {
            ListOfCli[nickname] = emisor;
            cout << " " << nickname << " conectado. Total: " << ListOfCli.size() << endl;
            broadcastMensaje("-->", nickname + " se ha unido.", emisor);
        }
        else if (accion == 'B') {
            broadcastMensaje(nickname, msg, emisor);
        }
        else if (accion == 'M') {
            if (!msg.empty() && msg[0] == '@') {
                size_t pos = msg.find(' ');
                if (pos != string::npos) {
                    string destino = msg.substr(1, pos - 1);
                    string texto = msg.substr(pos + 1);
                    enviarPrivado(destino, nickname, texto, emisor);
                }
            }
        }
        else if (accion == 'L') {
            enviarLista(emisor);
        }
        else if (accion == 'Q') {
            eliminarCliente(nickname);
            cout << " " << nickname << " desconectado. Total: " << ListOfCli.size() << endl;
            broadcastMensaje("Sistema", nickname + " ha salido.", emisor);
        }
        else {
            cout << " Accion desconocida: " << accion << endl;
        }
    }

    close(ServerSocket);
    return 0;
}
