#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <iostream>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <atomic>

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
#define MAX_NICK                20
#define PAUSA_US                300    

atomic<bool> conectado(true);
string miNickname;
struct sockaddr_in server_addr;


map<string, string> nombreRecibiendo;
map<string, long long> tamanoRecibiendo;
map<string, map<long long, string>> trozosRecibidos;




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

string nombreSinRuta(const string& ruta) {
    size_t pos = ruta.find_last_of("/\\");
    return (pos == string::npos) ? ruta : ruta.substr(pos + 1);
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




void enviarPaquete(int S, const string& paquete) {
    if (paquete.size() != TAM_PAQUETE) return;
    cout << "\r >> ENVIADO:  " << mostrar(paquete) << endl;
    sendto(S, paquete.c_str(), TAM_PAQUETE, 0, (const struct sockaddr *)&server_addr, sizeof(server_addr));
}

void enviarMensaje(int S, char accion, const string& nick, const string& msg) {
    enviarPaquete(S, empaquetar(accion, nick, msg));
}


void enviarArchivo(int S, const string& destino, const string& ruta) {
    ifstream archivo(ruta, ios::binary);
    if (!archivo) {
        cout << " No se pudo abrir: " << ruta << endl;
        return;
    }
    string contenido((istreambuf_iterator<char>(archivo)), istreambuf_iterator<char>());
    archivo.close();

    string nombre = nombreSinRuta(ruta);
    long long tamArchivo = contenido.size();

    
    int largoNick = max(destino.size(), miNickname.size());

    
    if ((int)nombre.size() > TAM_PAQUETE - 52 - largoNick) {
        cout << " Nombre de archivo muy largo (max " << TAM_PAQUETE - 52 - largoNick << ")" << endl;
        return;
    }

    
    int capacidad = TAM_PAQUETE - (1 + TAMANIO_NICK_DATOS + largoNick + TAMANIO_SECUENCIA + TAMANIO_DATOS);
    long long totalTrozos = (tamArchivo + capacidad - 1) / capacidad;

    enviarPaquete(S, empaquetarInicioArchivo(destino, nombre, tamArchivo));
    usleep(20000);

    for (long long i = 0; i < totalTrozos; i++) {
        string trozo = contenido.substr(i * capacidad, capacidad);
        enviarPaquete(S, empaquetarDatosArchivo(destino, i, trozo));
        usleep(PAUSA_US);
    }

    usleep(20000);
    enviarPaquete(S, empaquetarFinArchivo(destino, totalTrozos));

    cout << " Archivo " << nombre << " (" << tamArchivo << " bytes, " << totalTrozos << " trozos) enviado a " << destino << endl;
}




void recibirInicioArchivo(const char* buff) {
    string origen, nombre;
    long long tamArchivo;
    if (!desempaquetarInicioArchivo(buff, origen, nombre, tamArchivo)) return;

    nombreRecibiendo[origen] = nombreSinRuta(nombre);
    tamanoRecibiendo[origen] = tamArchivo;
    trozosRecibidos[origen].clear();

    cout << " [Archivo] " << origen << " te envia " << nombre << " (" << tamArchivo << " bytes)" << endl;
}

void recibirDatosArchivo(const char* buff) {
    string origen, datos;
    long long secuencia;
    if (!desempaquetarDatosArchivo(buff, origen, secuencia, datos)) return;

    if (nombreRecibiendo.count(origen))
        trozosRecibidos[origen][secuencia] = datos;
}

void recibirFinArchivo(const char* buff) {
    string origen;
    long long totalTrozos;
    if (!desempaquetarFinArchivo(buff, origen, totalTrozos)) return;

    if (!nombreRecibiendo.count(origen)) {
        cout << " [Archivo] se perdio el paquete F de " << origen << endl;
        return;
    }

   
    string salida = "recibido_" + nombreRecibiendo[origen];
    ofstream archivo(salida, ios::binary);
    long long faltan = 0, bytes = 0;

    for (long long i = 0; i < totalTrozos; i++) {
        if (trozosRecibidos[origen].count(i) == 0) {
            faltan++;
            continue;
        }
        string& trozo = trozosRecibidos[origen][i];
        archivo.write(trozo.c_str(), trozo.size());
        bytes += trozo.size();
    }
    archivo.close();

    if (faltan == 0 && bytes == tamanoRecibiendo[origen])
        cout << " [Archivo de " << origen << "]: guardado como " << salida  << " (" << bytes << " bytes) OK" << endl;
    else
        cout << " [Archivo de " << origen << "]: " << salida << " INCOMPLETO, faltan "<< faltan << " trozos" << endl;

    nombreRecibiendo.erase(origen);
    tamanoRecibiendo.erase(origen);
    trozosRecibidos.erase(origen);
}




void recibirMensajes(int S) {
    char buff[TAM_PAQUETE];

    while (conectado) {
        socklen_t largo = sizeof(server_addr);
        int n = recvfrom(S, buff, TAM_PAQUETE, 0, (struct sockaddr *)&server_addr, &largo);

        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;  
            cout << "\n Error de red" << endl;
            conectado = false;
            break;
        }
        if (n != TAM_PAQUETE) continue;

        cout << "\r << RECIBIDO: " << mostrar(string(buff, TAM_PAQUETE)) << endl;

        if (buff[0] == 'F') recibirInicioArchivo(buff);
        else if (buff[0] == 'D') recibirDatosArchivo(buff);
        else if (buff[0] == 'E') recibirFinArchivo(buff);
        else {
            char accion;
            string nick, msg;
            if (!desempaquetar(buff, accion, nick, msg)) continue;

            if (accion == 'b') {
                cout << nick << ": " << msg << endl;
            }
            else if (accion == 'm') {
                cout << "[Privado de " << nick << "]: " << msg << endl;
            }
            else if (accion == 'l') {
                cout << "[Conectados]: " << msg << endl;
            }
        }
        cout << "> " << flush;
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

        if (input == "/lista") {
            enviarMensaje(S, 'L', miNickname, "");
        }
        else if (input.rfind("/archivo ", 0) == 0) {
            
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
}




int main(void) {
    int SocketFD = socket(AF_INET, SOCK_DGRAM, 0);
    if (-1 == SocketFD) {
        perror("cannot create socket");
        exit(EXIT_FAILURE);
    }


    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(SocketFD, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    int tamBuffer = 4 * 1024 * 1024;
    setsockopt(SocketFD, SOL_SOCKET, SO_RCVBUF, &tamBuffer, sizeof(tamBuffer));

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

    if (miNickname.empty() || miNickname.size() > MAX_NICK) {
        cout << " El nombre debe tener entre 1 y " << MAX_NICK << " caracteres" << endl;
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
