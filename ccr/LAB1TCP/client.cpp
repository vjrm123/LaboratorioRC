/*
 * LAB1TCP - Cliente TCP  (como Lab2: un thread recibe y otro envia)
 *
 * PROTOCOLO
 *   Chat:     [1 accion][7 tamNick][nick][11 tamMsg][msg]
 *   Archivo:  [F][13 tamNick][nick][13 tamNombre][nombre][25 tamArchivo][archivo]
 *
 * Comandos:
 *   texto                 mensaje a todos
 *   @nick texto           mensaje privado
 *   /lista                ver conectados
 *   /archivo nick ruta    enviar cualquier archivo
 *   salir                 salir
 *
 * Compilar: g++ -pthread client.cpp -o cliente
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <atomic>

using namespace std;

#define TAMANIO_ACCION          1
#define TAMANIO_DE_NOMBRE       7
#define TAMANIO_DE_MSG          11
#define TAMANIO_NICK_ARCHIVO    13
#define TAMANIO_NOMBRE_ARCHIVO  13
#define TAMANIO_ARCHIVO         25
#define TAMANIO_BLOQUE          4096

atomic<bool> conectado(true);
string miNickname;


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

string nombreSinRuta(const string& ruta) {
    size_t pos = ruta.find_last_of("/\\");
    return (pos == string::npos) ? ruta : ruta.substr(pos + 1);
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
    cout << "\r >> ENVIADO:  " << mostrar(data) << endl;
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

void enviarArchivo(int S, const string& destino, const string& ruta) {
    ifstream archivo(ruta, ios::binary);
    if (!archivo) {
        cout << " No se pudo abrir: " << ruta << endl;
        return;
    }
    archivo.seekg(0, ios::end);
    long long tamArchivo = archivo.tellg();
    archivo.seekg(0, ios::beg);

    string nombre = nombreSinRuta(ruta);
    string cabecera = empaquetarCabeceraArchivo(destino, nombre, tamArchivo);
    cout << "\r >> ENVIADO:  " << mostrar(cabecera) << " + <" << tamArchivo << " bytes>" << endl;
    escribirExacto(S, cabecera.c_str(), cabecera.size());

    char buff[TAMANIO_BLOQUE];
    long long faltan = tamArchivo;
    while (faltan > 0) {
        long long n = min<long long>(faltan, TAMANIO_BLOQUE);
        archivo.read(buff, n);
        if (!escribirExacto(S, buff, n)) break;
        faltan -= n;
    }
    cout << " Archivo " << nombre << " (" << tamArchivo << " bytes) enviado a " << destino << endl;
}

bool recibirArchivo(int S) {
    string origen, nombre;
    long long tamArchivo;
    if (!recibirCabeceraArchivo(S, origen, nombre, tamArchivo)) return false;

    cout << "\r << RECIBIDO: " << mostrar(empaquetarCabeceraArchivo(origen, nombre, tamArchivo))
         << " + <" << tamArchivo << " bytes>" << endl;

    string salida = "recibido_" + nombreSinRuta(nombre);
    ofstream archivo(salida, ios::binary);

    char buff[TAMANIO_BLOQUE];
    long long faltan = tamArchivo;
    while (faltan > 0) {
        long long n = min<long long>(faltan, TAMANIO_BLOQUE);
        if (!leerExacto(S, buff, n)) return false;
        archivo.write(buff, n);
        faltan -= n;
    }
    archivo.close();

    cout << " [Archivo de " << origen << "]: " << nombre << " guardado como " << salida << endl;
    cout << "> " << flush;
    return true;
}


// ===================== THREADS =====================

void recibirMensajes(int S) {
    char accion;
    string nick, msg;

    while (conectado) {
        if (!leerExacto(S, &accion, TAMANIO_ACCION)) break;

        if (accion == 'F') {
            if (!recibirArchivo(S)) break;
            continue;
        }

        if (!recibirMensaje(S, nick, msg)) break;
        cout << "\r << RECIBIDO: " << mostrar(empaquetar(accion, nick, msg)) << endl;

        if (accion == 'b') {
            cout << nick << ": " << msg << endl;
        }
        else if (accion == 'm') {
            cout << "[Privado de " << nick << "]: " << msg << endl;
        }
        else if (accion == 'l') {
            cout << "[Conectados]: " << msg << endl;
        }
        cout << "> " << flush;
    }

    if (conectado) cout << "\n Desconectado del servidor" << endl;
    conectado = false;
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

        if (input == "/lista") {
            enviarMensaje(S, 'L', miNickname, "");
        }
        else if (input.rfind("/archivo ", 0) == 0) {
            // /archivo nick ruta
            string resto = input.substr(9);
            size_t pos = resto.find(' ');
            if (pos != string::npos) {
                string destino = resto.substr(0, pos);
                string ruta = resto.substr(pos + 1);
                enviarArchivo(S, destino, ruta);
            } else {
                cout << " Formato: /archivo nick ruta" << endl;
            }
        }
        else if (input[0] == '@') {
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

    shutdown(S, SHUT_RDWR);   // despierta al thread que recibe
}


// ===================== MAIN =====================

int main(void) {
    struct sockaddr_in stSockAddr;
    int Res;
    signal(SIGPIPE, SIG_IGN);

    int SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (-1 == SocketFD) {
        perror("cannot create socket");
        exit(EXIT_FAILURE);
    }

    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(1100);

    Res = inet_pton(AF_INET, "127.0.0.1", &stSockAddr.sin_addr);
    if (0 >= Res) {
        perror("invalid IP address");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    if (-1 == connect(SocketFD, (const struct sockaddr *)&stSockAddr,
                      sizeof(struct sockaddr_in))) {
        perror("connect failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    cout << "Ingresa tu NOMBRE: ";
    getline(cin, miNickname);

    if (miNickname.empty()) {
        cout << " El nombre no puede estar vacio" << endl;
        close(SocketFD);
        return 1;
    }

    enviarMensaje(SocketFD, 'N', miNickname, "");
    cout << " Conectado como: " << miNickname << endl;
    cout << " Comandos: texto | @nick msg | /lista | /archivo nick ruta | salir" << endl;

    thread hiloRecibir(recibirMensajes, SocketFD);
    thread hiloEnviar(enviarMensajes, SocketFD);

    hiloEnviar.join();
    hiloRecibir.join();

    close(SocketFD);
    return 0;
}
