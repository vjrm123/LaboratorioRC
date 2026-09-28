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
#include <fstream>
#include <iterator>

using namespace std;

#define TAMANIO_ACCION   1
#define TAMANIO_DE_NOMBRE 7
#define TAMANIO_DE_MSG  11

atomic<bool> conectado(true);
string miNickname;

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
    char buff[100000];
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

    n = read(S, buff, tamano);
    if (n <= 0) return false;
    buff[n] = '\0';
    msg = buff;

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
        else if (accion == 'f') {
            size_t p = msg.find('|');
            if (p != string::npos) {
                string filename = msg.substr(0, p);
                string contenido = msg.substr(p + 1);

                string ruta = "recibido_" + filename;
                ofstream archivo(ruta, ios::binary);
                if (archivo.is_open()) {
                    archivo.write(contenido.c_str(), contenido.size());
                    archivo.close();
                    cout << "\r\n[Archivo de " << nick << "]: " 
                         << filename << " → " << ruta << endl;
                } else {
                    cout << "\r\n[Error] No se pudo guardar archivo" << endl;
                }
                cout << "> " << flush;
            }
        }
        else if (accion == 'e') {
            cout << "\r\n[ERROR] " << msg << endl;
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

        // ← NUEVO: Lista de conectados
        if (input == "/lista" || input == "/users") {
            enviarMensaje(S, 'L', miNickname, "");
            continue;
        }

        // ← NUEVO: Enviar archivo
        if (input.substr(0, 6) == "/file ") {
            string resto = input.substr(6);
            size_t pos = resto.find(' ');
            if (pos == string::npos) {
                cout << "Formato: /file destino ruta_archivo" << endl;
                continue;
            }
            string destino = resto.substr(0, pos);
            string ruta = resto.substr(pos + 1);

            ifstream archivo(ruta, ios::binary);
            if (!archivo.is_open()) {
                cout << "No se pudo abrir: " << ruta << endl;
                continue;
            }

            string contenido((istreambuf_iterator<char>(archivo)),
                             istreambuf_iterator<char>());
            archivo.close();

            // Extraer solo el nombre del archivo (sin ruta)
            string filename = ruta;
            size_t barra = ruta.find_last_of('/');
            if (barra != string::npos)
                filename = ruta.substr(barra + 1);

            // Enviar: "destino|filename|contenido"
            string mensaje = destino + "|" + filename + "|" + contenido;
            enviarMensaje(S, 'F', miNickname, mensaje);
            cout << "Archivo '" << filename << "' enviado a " 
                 << destino << " (" << contenido.size() << " bytes)" << endl;
            continue;
        }

        if (input[0] == '@') {
            size_t pos = input.find(' ');
            if (pos != string::npos) {
                string destino = input.substr(1, pos - 1);
                string texto = input.substr(pos + 1);
                enviarMensaje(S, 'M', miNickname, "@" + destino + " " + texto);
            } else {
                cout << "Formato: @nickname mensaje" << endl;
            }
        }
        else {
            enviarMensaje(S, 'B', miNickname, input);
        }
    }
}


int main(void) {
    struct sockaddr_in stSockAddr;
    int Res;
    int SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (-1 == SocketFD) {
        perror("cannot create socket");
        exit(EXIT_FAILURE);
    }

    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(1100);

    Res = inet_pton(AF_INET, "127.0.0.1", &stSockAddr.sin_addr);

    if (0 > Res) {
        perror("error: first parameter is not a valid address family");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }
    else if (0 == Res) {
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

    thread hiloRecibir(recibirMensajes, SocketFD);
    thread hiloEnviar(enviarMensajes, SocketFD);

    hiloRecibir.join();
    hiloEnviar.join();

    shutdown(SocketFD, SHUT_RDWR);
    close(SocketFD);

    return 0;
}