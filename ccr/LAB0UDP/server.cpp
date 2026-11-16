/*
 * ============================================================================
 *  LAB0UDP - Servidor de chat UDP
 *  Protocolo: ver protocolo.h  (paquetes fijos de 100 bytes)
 *
 *  Compilar:  g++ -std=c++17 server.cpp -o server
 * ============================================================================
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <iostream>
#include <map>
#include <string>

#include "protocolo.h"

using namespace std;

const int PUERTO = 1100;

int ServerSocket;
map<string, sockaddr_in> clientes;      // nick -> direccion

// ================================ RED =======================================

string direccion(const sockaddr_in& a) {
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &a.sin_addr, ip, sizeof(ip));
    return string(ip) + ":" + to_string(ntohs(a.sin_port));
}

bool mismaDireccion(const sockaddr_in& a, const sockaddr_in& b) {
    return a.sin_port == b.sin_port && a.sin_addr.s_addr == b.sin_addr.s_addr;
}

void enviar(const sockaddr_in& destino, const string& paquete) {
    if (paquete.size() != (size_t)TAM_PAQUETE) return;
    cout << " >> ENVIADO  a  " << direccion(destino) << " | " << protocoloVisible(paquete) << endl;
    sendto(ServerSocket, paquete.data(), TAM_PAQUETE, 0,
           (const sockaddr *)&destino, sizeof(destino));
}

void enviarChat(const sockaddr_in& destino, char accion, const string& nick, const string& msg) {
    enviar(destino, armarChat(accion, nick, msg));
}

// ============================== CLIENTES ====================================

bool buscarCliente(const string& nick, sockaddr_in& addr) {
    auto it = clientes.find(nick);
    if (it == clientes.end()) return false;
    addr = it->second;
    return true;
}

string nickDe(const sockaddr_in& addr) {
    for (auto& c : clientes)
        if (mismaDireccion(c.second, addr)) return c.first;
    return "";
}

void enviarATodosMenos(const sockaddr_in& emisor, const string& nick, const string& msg) {
    for (auto& c : clientes)
        if (!mismaDireccion(c.second, emisor))
            enviarChat(c.second, 'b', nick, msg);
}

// ================================ CHAT ======================================

// N
void manejarRegistro(const MensajeChat& m, const sockaddr_in& emisor) {
    clientes[m.nick] = emisor;
    cout << " [+] " << m.nick << " conectado. Total: " << clientes.size() << endl;
    enviarATodosMenos(emisor, "-->", m.nick + " se ha unido.");
}

// B
void manejarBroadcast(const MensajeChat& m, const sockaddr_in& emisor) {
    cout << " [CHAT] " << m.nick << ": " << m.msg << endl;
    enviarATodosMenos(emisor, m.nick, m.msg);
}

// M   msg = "@destino texto"
void manejarPrivado(const MensajeChat& m, const sockaddr_in& emisor) {
    size_t espacio = m.msg.find(' ');
    if (m.msg.empty() || m.msg[0] != '@' || espacio == string::npos) return;

    string destino = m.msg.substr(1, espacio - 1);
    string texto   = m.msg.substr(espacio + 1);

    sockaddr_in addr;
    if (!buscarCliente(destino, addr)) {
        enviarChat(emisor, 'm', "Sistema", "El usuario " + destino + " no existe.");
        return;
    }
    cout << " [PRIVADO] " << m.nick << " -> " << destino << ": " << texto << endl;
    enviarChat(addr, 'm', m.nick, texto);
}

// Q
void manejarSalida(const MensajeChat& m, const sockaddr_in& emisor) {
    clientes.erase(m.nick);
    cout << " [-] " << m.nick << " desconectado. Total: " << clientes.size() << endl;
    enviarATodosMenos(emisor, "Sistema", m.nick + " ha salido.");
}

// ================================ LISTA =====================================

// L  ->  l "Conectados: n"  +  l "nick1, nick2, ..." (los necesarios)
void manejarLista(const MensajeChat& m, const sockaddr_in& emisor) {
    const string sistema = "Sistema";
    const size_t capacidad = CHAT_MAX_TEXTO - sistema.size();

    cout << " [LISTA] pedida por " << m.nick << endl;
    enviarChat(emisor, 'l', sistema, "Conectados: " + to_string(clientes.size()));

    string linea;
    for (auto& c : clientes) {
        string item = linea.empty() ? c.first : ", " + c.first;
        if (linea.size() + item.size() > capacidad) {
            enviarChat(emisor, 'l', sistema, linea);
            linea.clear();
            item = c.first;
        }
        linea += item;
    }
    if (!linea.empty()) enviarChat(emisor, 'l', sistema, linea);
}

// =============================== ARCHIVOS ===================================
// F, D y E llegan con nick = DESTINO. El servidor cambia ese nick por el
// del ORIGEN y reenvia el paquete al destino con la misma letra.

void manejarArchivo(const string& paquete, const sockaddr_in& emisor) {
    string origen = nickDe(emisor);
    if (origen.empty()) return;                 // cliente no registrado

    char   tipo = paquete[0];
    string destino, reenvio;

    if (tipo == 'F') {
        InicioArchivo a;
        if (!leerInicio(paquete, a)) return;
        destino = a.nick;
        a.nick  = origen;
        reenvio = armarInicio(a);
        cout << " [ARCHIVO] " << origen << " -> " << destino << ": "
             << a.nombre << " (" << a.tamArchivo << " bytes)" << endl;
    }
    else if (tipo == 'D') {
        DatosArchivo d;
        if (!leerDatos(paquete, d)) return;
        destino = d.nick;
        d.nick  = origen;
        reenvio = armarDatos(d);
    }
    else if (tipo == 'E') {
        FinArchivo e;
        if (!leerFin(paquete, e)) return;
        destino = e.nick;
        e.nick  = origen;
        reenvio = armarFin(e);
        cout << " [ARCHIVO] " << origen << " -> " << destino << ": fin ("
             << e.totalTrozos << " trozos)" << endl;
    }

    sockaddr_in addrDestino;
    if (!buscarCliente(destino, addrDestino)) {
        if (tipo == 'F')
            enviarChat(emisor, 'm', "Sistema", "Archivo no enviado: " + destino + " no existe.");
        return;
    }
    if (reenvio.empty()) {
        if (tipo == 'F')
            enviarChat(emisor, 'm', "Sistema", "Archivo no enviado: nombre demasiado largo.");
        return;
    }
    enviar(addrDestino, reenvio);
}

// ================================ MAIN ======================================

int main(void) {
    ServerSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (ServerSocket == -1) {
        perror("can not create socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(ServerSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    int bufSize = 4 * 1024 * 1024;              // para no perder trozos de archivo
    setsockopt(ServerSocket, SOL_SOCKET, SO_RCVBUF, &bufSize, sizeof(bufSize));
    setsockopt(ServerSocket, SOL_SOCKET, SO_SNDBUF, &bufSize, sizeof(bufSize));

    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(PUERTO);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(ServerSocket, (const sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("error bind failed");
        close(ServerSocket);
        exit(EXIT_FAILURE);
    }

    cout << " Servidor UDP en el puerto " << PUERTO
         << " (paquetes de " << TAM_PAQUETE << " bytes)" << endl;

    while (true) {
        char buffer[TAM_PAQUETE];
        sockaddr_in emisor;
        socklen_t   largo = sizeof(emisor);

        int n = recvfrom(ServerSocket, buffer, TAM_PAQUETE, 0, (sockaddr *)&emisor, &largo);
        if (n != TAM_PAQUETE) {
            if (n > 0) cout << " Paquete ignorado de " << direccion(emisor)
                            << ": mide " << n << " bytes (deben ser 100)" << endl;
            continue;
        }

        string paquete(buffer, TAM_PAQUETE);
        cout << " << RECIBIDO de " << direccion(emisor) << " | " << protocoloVisible(paquete) << endl;

        // ---- archivos ----
        char tipo = paquete[0];
        if (tipo == 'F' || tipo == 'D' || tipo == 'E') {
            manejarArchivo(paquete, emisor);
            continue;
        }

        // ---- chat y lista ----
        MensajeChat m;
        if (!leerChat(paquete, m)) continue;

        switch (m.accion) {
            case 'N': manejarRegistro(m, emisor);  break;
            case 'B': manejarBroadcast(m, emisor); break;
            case 'M': manejarPrivado(m, emisor);   break;
            case 'L': manejarLista(m, emisor);     break;
            case 'Q': manejarSalida(m, emisor);    break;
            default:  cout << " Accion desconocida: " << m.accion << endl;
        }
    }

    close(ServerSocket);
    return 0;
}
