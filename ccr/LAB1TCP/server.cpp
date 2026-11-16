/*
 * LAB1TCP - Servidor TCP con select()  (sin threads)
 *
 * PROTOCOLO
 *   Chat:     [1 accion][7 tamNick][nick][11 tamMsg][msg]
 *             N registrar | B todos | M privado ("@dest texto") | L lista | Q salir
 *             respuestas del servidor: b todos | m privado | l lista
 *
 *   Archivo:  [F][13 tamNick][nick][13 tamNombre][nombre][25 tamArchivo][archivo]
 *             cliente -> servidor: nick = destino
 *             servidor -> cliente: nick = origen
 *
 * Compilar: g++ server.cpp -o server
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <iostream>
#include <map>
#include <string>

using namespace std;

#define TAMANIO_ACCION          1
#define TAMANIO_DE_NOMBRE       7
#define TAMANIO_DE_MSG          11
#define TAMANIO_NICK_ARCHIVO    13
#define TAMANIO_NOMBRE_ARCHIVO  13
#define TAMANIO_ARCHIVO         25
#define TAMANIO_BLOQUE          4096

map<string, int> ListOfCli;     // nick -> socket
fd_set master;                  // sockets que vigila select()
int fdmax;
int ServerSocket;


// ===================== FUNCIONES BASICAS =====================

string zeroPad(long long numero, int tamano) {
    string str = to_string(numero);
    if (str.length() >= (size_t)tamano)
        return str;
    return string(tamano - str.length(), '0') + str;
}

// Para imprimir el protocolo (bytes raros se ven como '.')
string mostrar(const string& data) {
    string r;
    for (unsigned char c : data)
        r += (c >= 32 && c < 127) ? (char)c : '.';
    return r;
}

// read() puede leer menos de lo pedido: repetir hasta completar
bool leerExacto(int S, char* buff, long long n) {
    long long leidos = 0;
    while (leidos < n) {
        int r = read(S, buff + leidos, n - leidos);
        if (r <= 0) return false;
        leidos += r;
    }
    return true;
}

bool escribirExacto(int S, const char* buff, long long n) {
    long long escritos = 0;
    while (escritos < n) {
        int r = write(S, buff + escritos, n - escritos);
        if (r <= 0) return false;
        escritos += r;
    }
    return true;
}

bool leerNumero(int S, int tamano, long long& numero) {
    char buff[30];
    if (!leerExacto(S, buff, tamano)) return false;
    buff[tamano] = '\0';
    numero = atoll(buff);
    return true;
}

bool leerTexto(int S, long long tamano, string& texto) {
    texto.assign(tamano, '\0');
    if (tamano == 0) return true;
    return leerExacto(S, &texto[0], tamano);
}

string nickDe(int S) {
    for (auto& cliente : ListOfCli)
        if (cliente.second == S) return cliente.first;
    return "";
}


// ===================== PROTOCOLO DE CHAT =====================

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
    cout << " >> ENVIADO  a " << nickDe(S) << ": " << mostrar(data) << endl;
    escribirExacto(S, data.c_str(), data.size());
}

// La accion ya fue leida, aqui se lee el resto
bool recibirMensaje(int S, string& nick, string& msg) {
    long long tamano;

    if (!leerNumero(S, TAMANIO_DE_NOMBRE, tamano)) return false;
    if (!leerTexto(S, tamano, nick)) return false;

    if (!leerNumero(S, TAMANIO_DE_MSG, tamano)) return false;
    if (!leerTexto(S, tamano, msg)) return false;

    return true;
}


// ===================== PROTOCOLO DE ARCHIVO =====================

string empaquetarCabeceraArchivo(const string& nick, const string& nombre, long long tamArchivo) {
    string data;
    data += 'F';
    data += zeroPad(nick.size(), TAMANIO_NICK_ARCHIVO);
    data += nick;
    data += zeroPad(nombre.size(), TAMANIO_NOMBRE_ARCHIVO);
    data += nombre;
    data += zeroPad(tamArchivo, TAMANIO_ARCHIVO);
    return data;
}

// La 'F' ya fue leida, aqui se lee la cabecera (sin el archivo)
bool recibirCabeceraArchivo(int S, string& nick, string& nombre, long long& tamArchivo) {
    long long tamano;

    if (!leerNumero(S, TAMANIO_NICK_ARCHIVO, tamano)) return false;
    if (!leerTexto(S, tamano, nick)) return false;

    if (!leerNumero(S, TAMANIO_NOMBRE_ARCHIVO, tamano)) return false;
    if (!leerTexto(S, tamano, nombre)) return false;

    if (!leerNumero(S, TAMANIO_ARCHIVO, tamArchivo)) return false;

    return true;
}


// ===================== ACCIONES DEL SERVIDOR =====================

void broadcastMensaje(const string& nick, const string& msg, int emisorSocket) {
    for (auto& cliente : ListOfCli)
        if (cliente.second != emisorSocket)
            enviarMensaje(cliente.second, 'b', nick, msg);
}

void enviarPrivado(const string& destino, const string& nick, const string& msg, int emisorSocket) {
    auto it = ListOfCli.find(destino);
    if (it != ListOfCli.end())
        enviarMensaje(it->second, 'm', nick, msg);
    else
        enviarMensaje(emisorSocket, 'm', "Sistema", "El usuario " + destino + " no existe.");
}

void enviarLista(int S) {
    string lista;
    for (auto& cliente : ListOfCli) {
        if (!lista.empty()) lista += ", ";
        lista += cliente.first;
    }
    enviarMensaje(S, 'l', "Sistema", lista);
}

// Recibe el archivo del emisor y lo pasa al destino de a bloques
bool reenviarArchivo(int S) {
    string destino, nombre;
    long long tamArchivo;
    if (!recibirCabeceraArchivo(S, destino, nombre, tamArchivo)) return false;

    string origen = nickDe(S);
    cout << " << RECIBIDO de " << origen << ": "
         << mostrar(empaquetarCabeceraArchivo(destino, nombre, tamArchivo))
         << " + <" << tamArchivo << " bytes>" << endl;

    int destinoSocket = -1;
    auto it = ListOfCli.find(destino);
    if (it != ListOfCli.end()) destinoSocket = it->second;

    // cabecera con el nick del ORIGEN
    if (destinoSocket != -1) {
        string cabecera = empaquetarCabeceraArchivo(origen, nombre, tamArchivo);
        cout << " >> ENVIADO  a " << destino << ": " << mostrar(cabecera)
             << " + <" << tamArchivo << " bytes>" << endl;
        escribirExacto(destinoSocket, cabecera.c_str(), cabecera.size());
    }

    // el archivo se lee completo aunque el destino no exista,
    // si no esos bytes se mezclarian con el siguiente mensaje
    char buff[TAMANIO_BLOQUE];
    long long faltan = tamArchivo;
    while (faltan > 0) {
        long long n = min<long long>(faltan, TAMANIO_BLOQUE);
        if (!leerExacto(S, buff, n)) return false;
        if (destinoSocket != -1) escribirExacto(destinoSocket, buff, n);
        faltan -= n;
    }

    if (destinoSocket == -1)
        enviarMensaje(S, 'm', "Sistema", "Archivo no enviado: " + destino + " no existe.");
    else
        cout << " " << origen << " -> " << destino << ": archivo " << nombre << " enviado" << endl;

    return true;
}

void eliminarCliente(int S) {
    string nick = nickDe(S);
    ListOfCli.erase(nick);
    close(S);
    FD_CLR(S, &master);

    if (!nick.empty()) {
        cout << " " << nick << " desconectado. Total: " << ListOfCli.size() << endl;
        broadcastMensaje("Sistema", nick + " ha salido.", S);
    }
}


// ===================== MANEJAR CLIENTES =====================

void aceptarCliente() {
    int ClientSocket = accept(ServerSocket, NULL, NULL);
    if (ClientSocket < 0) {
        perror("error accept failed");
        return;
    }
    FD_SET(ClientSocket, &master);
    if (ClientSocket > fdmax) fdmax = ClientSocket;
    cout << " Nueva conexion en el socket " << ClientSocket << endl;
}

// select() dijo que este cliente envio algo: leer UN mensaje.
// Devuelve false si hay que desconectarlo.
bool manejarCliente(int S) {
    char accion;
    if (!leerExacto(S, &accion, TAMANIO_ACCION)) return false;

    if (accion == 'F')
        return reenviarArchivo(S);

    string nickname, msg;
    if (!recibirMensaje(S, nickname, msg)) return false;
    cout << " << RECIBIDO de " << nickDe(S) << ": "
         << mostrar(empaquetar(accion, nickname, msg)) << endl;

    if (accion == 'N') {
        ListOfCli[nickname] = S;
        cout << " " << nickname << " conectado. Total: " << ListOfCli.size() << endl;
        broadcastMensaje("-->", nickname + " se ha unido.", S);
    }
    else if (accion == 'B') {
        broadcastMensaje(nickname, msg, S);
    }
    else if (accion == 'M') {
        if (!msg.empty() && msg[0] == '@') {
            size_t pos = msg.find(' ');
            if (pos != string::npos) {
                string destino = msg.substr(1, pos - 1);
                string texto = msg.substr(pos + 1);
                enviarPrivado(destino, nickname, texto, S);
            }
        }
    }
    else if (accion == 'L') {
        enviarLista(S);
    }
    else if (accion == 'Q') {
        return false;
    }
    else {
        cout << " Accion desconocida: " << accion << endl;
    }
    return true;
}


// ===================== MAIN =====================

int main(void) {
    struct sockaddr_in stSockAddr;
    signal(SIGPIPE, SIG_IGN);   // si un cliente se cae, el servidor sigue

    ServerSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
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

    FD_ZERO(&master);
    FD_SET(ServerSocket, &master);
    fdmax = ServerSocket;

    cout << " Esperando clientes..." << endl;

    for (;;) {
        fd_set read_fds = master;   // select() modifica el set, se usa una copia
        if (select(fdmax + 1, &read_fds, NULL, NULL, NULL) == -1) {
            perror("select");
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i <= fdmax; i++) {
            if (!FD_ISSET(i, &read_fds)) continue;

            if (i == ServerSocket)
                aceptarCliente();               // cliente nuevo
            else if (!manejarCliente(i))
                eliminarCliente(i);             // cliente salio o se cayo
        }
    }

    close(ServerSocket);
    return 0;
}
