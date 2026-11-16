
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
#include <thread>
#include <mutex>

using namespace std;

#define TAMANIO_ACCION          1
#define TAMANIO_DE_NOMBRE       7
#define TAMANIO_DE_MSG          11
#define TAMANIO_NICK_ARCHIVO    13
#define TAMANIO_NOMBRE_ARCHIVO  13
#define TAMANIO_ARCHIVO         25
#define TAMANIO_BLOQUE          4096

int ServerSocket;
fd_set master;                  // sockets que vigila select() (solo lo usa el thread de select)
int fdmax;

// El thread de select guarda en SD lo que llega; el thread de cada cliente lo saca.
map<int, string> SD;            // socket -> datos recibidos (string guarda bytes binarios)
map<int, mutex>  SL;            // socket -> mutex que protege SD[socket]
map<int, bool>   SC;            // socket -> true si el cliente cerro la conexion
map<int, mutex>  SW;            // socket -> mutex para escribir en ese socket
mutex mtxMapas;                 // protege la estructura de los maps de arriba

map<string, int> ListOfCli;     // nick -> socket
mutex mtxClientes;

mutex mtxConsola;


// ===================== FUNCIONES BASICAS =====================

void imprimir(const string& linea) {
    lock_guard<mutex> lock(mtxConsola);
    cout << linea << endl;
}

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

// Acceso seguro a los maps (un map no se puede modificar desde dos threads a la vez)
mutex& lockDatos(int S)     { lock_guard<mutex> g(mtxMapas); return SL[S]; }
mutex& lockEscritura(int S) { lock_guard<mutex> g(mtxMapas); return SW[S]; }
string& datosDe(int S)      { lock_guard<mutex> g(mtxMapas); return SD[S]; }
bool& cerradoDe(int S)      { lock_guard<mutex> g(mtxMapas); return SC[S]; }

string nickDe(int S) {
    lock_guard<mutex> lock(mtxClientes);
    for (auto& cliente : ListOfCli)
        if (cliente.second == S) return cliente.first;
    return "";
}

int socketDe(const string& nick) {
    lock_guard<mutex> lock(mtxClientes);
    auto it = ListOfCli.find(nick);
    return (it != ListOfCli.end()) ? it->second : -1;
}


// ===================== LECTURA DESDE SD (thread del cliente) =====================

// Saca hasta 'maximo' bytes de SD[S]. Si esta vacio, espera.
// Devuelve cuantos saco, o 0 si el cliente se desconecto.
long long sacarDatos(int S, char* buff, long long maximo) {
    while (true) {
        {
            lock_guard<mutex> lock(lockDatos(S));        // SL[S].lock()
            string& datos = datosDe(S);                   // SD[S].data
            if (!datos.empty()) {
                long long n = min<long long>(maximo, datos.size());
                memcpy(buff, datos.data(), n);
                datos.erase(0, n);
                return n;
            }
            if (cerradoDe(S)) return 0;
        }                                                 // SL[S].unlock()
        usleep(1000);                                     // no hay datos: esperar un poco
    }
}

// Saca exactamente n bytes (espera hasta que lleguen)
bool leerExacto(int S, char* buff, long long n) {
    long long leidos = 0;
    while (leidos < n) {
        long long r = sacarDatos(S, buff + leidos, n - leidos);
        if (r == 0) return false;
        leidos += r;
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


// ===================== ESCRITURA =====================

bool escribirExacto(int S, const char* buff, long long n) {
    long long escritos = 0;
    while (escritos < n) {
        int r = write(S, buff + escritos, n - escritos);
        if (r <= 0) return false;
        escritos += r;
    }
    return true;
}

// Envia un mensaje completo sin que otro thread se meta en medio
void enviarDatos(int S, const string& data) {
    lock_guard<mutex> lock(lockEscritura(S));
    escribirExacto(S, data.c_str(), data.size());
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
    imprimir(" >> ENVIADO  a " + nickDe(S) + ": " + mostrar(data));
    enviarDatos(S, data);
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
    map<string, int> copia;
    {
        lock_guard<mutex> lock(mtxClientes);
        copia = ListOfCli;
    }
    for (auto& cliente : copia)
        if (cliente.second != emisorSocket)
            enviarMensaje(cliente.second, 'b', nick, msg);
}

void enviarPrivado(const string& destino, const string& nick, const string& msg, int emisorSocket) {
    int destinoSocket = socketDe(destino);
    if (destinoSocket != -1)
        enviarMensaje(destinoSocket, 'm', nick, msg);
    else
        enviarMensaje(emisorSocket, 'm', "Sistema", "El usuario " + destino + " no existe.");
}

void enviarLista(int S) {
    string lista;
    {
        lock_guard<mutex> lock(mtxClientes);
        for (auto& cliente : ListOfCli) {
            if (!lista.empty()) lista += ", ";
            lista += cliente.first;
        }
    }
    enviarMensaje(S, 'l', "Sistema", lista);
}

// Recibe el archivo del emisor y lo pasa al destino de a bloques
bool reenviarArchivo(int S) {
    string destino, nombre;
    long long tamArchivo;
    if (!recibirCabeceraArchivo(S, destino, nombre, tamArchivo)) return false;

    string origen = nickDe(S);
    imprimir(" << RECIBIDO de " + origen + ": "
             + mostrar(empaquetarCabeceraArchivo(destino, nombre, tamArchivo))
             + " + <" + to_string(tamArchivo) + " bytes>");

    int destinoSocket = socketDe(destino);

    // Mientras se pasa el archivo nadie mas puede escribirle al destino,
    // si no, un mensaje de chat quedaria metido en medio del archivo.
    unique_lock<mutex> lockDestino;
    if (destinoSocket != -1) {
        lockDestino = unique_lock<mutex>(lockEscritura(destinoSocket));

        string cabecera = empaquetarCabeceraArchivo(origen, nombre, tamArchivo);
        imprimir(" >> ENVIADO  a " + destino + ": " + mostrar(cabecera)
                 + " + <" + to_string(tamArchivo) + " bytes>");
        escribirExacto(destinoSocket, cabecera.c_str(), cabecera.size());
    }

    // el archivo se lee completo aunque el destino no exista,
    // si no esos bytes se mezclarian con el siguiente mensaje
    char buff[TAMANIO_BLOQUE];
    long long faltan = tamArchivo;
    while (faltan > 0) {
        long long n = sacarDatos(S, buff, min<long long>(faltan, TAMANIO_BLOQUE));
        if (n == 0) return false;
        if (destinoSocket != -1) escribirExacto(destinoSocket, buff, n);
        faltan -= n;
    }

    if (destinoSocket == -1) {
        enviarMensaje(S, 'm', "Sistema", "Archivo no enviado: " + destino + " no existe.");
    } else {
        lockDestino.unlock();
        imprimir(" " + origen + " -> " + destino + ": archivo " + nombre + " enviado");
    }
    return true;
}

void eliminarCliente(int S) {
    string nick = nickDe(S);
    {
        lock_guard<mutex> lock(mtxClientes);
        ListOfCli.erase(nick);
    }
    if (!nick.empty()) {
        imprimir(" " + nick + " desconectado.");
        broadcastMensaje("Sistema", nick + " ha salido.", S);
    }
}


// ===================== THREAD DE CADA CLIENTE =====================

// Procesa UN mensaje. Devuelve false si el cliente salio o se cayo.
bool manejarMensaje(int S) {
    char accion;
    if (!leerExacto(S, &accion, TAMANIO_ACCION)) return false;

    if (accion == 'F')
        return reenviarArchivo(S);

    string nickname, msg;
    if (!recibirMensaje(S, nickname, msg)) return false;
    imprimir(" << RECIBIDO de " + nickDe(S) + ": " + mostrar(empaquetar(accion, nickname, msg)));

    if (accion == 'N') {
        {
            lock_guard<mutex> lock(mtxClientes);
            ListOfCli[nickname] = S;
        }
        imprimir(" " + nickname + " conectado.");
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
        imprimir(" Accion desconocida: " + string(1, accion));
    }
    return true;
}

void manejarCliente(int S) {
    while (manejarMensaje(S)) {}

    eliminarCliente(S);

    // avisar al thread de select que deje este socket y esperar a que lo haga
    shutdown(S, SHUT_RDWR);
    while (true) {
        {
            lock_guard<mutex> lock(lockDatos(S));
            if (cerradoDe(S)) break;
        }
        usleep(1000);
    }
    close(S);
}


// ===================== THREAD DE SELECT =====================

void aceptarCliente() {
    int ClientSocket = accept(ServerSocket, NULL, NULL);
    if (ClientSocket < 0) {
        perror("error accept failed");
        return;
    }

    {
        lock_guard<mutex> lock(lockDatos(ClientSocket));
        datosDe(ClientSocket).clear();
        cerradoDe(ClientSocket) = false;
    }

    FD_SET(ClientSocket, &master);
    if (ClientSocket > fdmax) fdmax = ClientSocket;
    imprimir(" Nueva conexion en el socket " + to_string(ClientSocket));

    thread(manejarCliente, ClientSocket).detach();     // un thread por cliente
}

// select() dijo que hay datos: leer lo que haya y guardarlo en SD[S]
void guardarDatos(int S) {
    char buff[TAMANIO_BLOQUE];
    int n = read(S, buff, TAMANIO_BLOQUE);

    lock_guard<mutex> lock(lockDatos(S));               // SL[S].lock()
    if (n <= 0) {
        cerradoDe(S) = true;                            // el cliente cerro
        FD_CLR(S, &master);
    } else {
        datosDe(S).append(buff, n);                     // SD[S] += datos
    }
}                                                       // SL[S].unlock()


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

    imprimir(" Esperando clientes...");

    for (;;) {
        fd_set read_fds = master;   // select() modifica el set, se usa una copia
        if (select(fdmax + 1, &read_fds, NULL, NULL, NULL) == -1) {
            perror("select");
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i <= fdmax; i++) {
            if (!FD_ISSET(i, &read_fds)) continue;

            if (i == ServerSocket)
                aceptarCliente();       // cliente nuevo -> crea su thread
            else
                guardarDatos(i);        // datos -> SD[i]
        }
    }

    close(ServerSocket);
    return 0;
}
