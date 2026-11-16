

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <thread>
#include <atomic>
#include <mutex>

#include "protocolo.h"

using namespace std;

const char* IP_SERVIDOR = "127.0.0.1";
const int   PUERTO      = 1100;
const int   PAUSA_US    = 300;       

int          SocketFD;
sockaddr_in  servidor;
string       miNick;
atomic<bool> conectado(true);
mutex        mtxConsola;



void imprimir(const string& linea) {
    lock_guard<mutex> lock(mtxConsola);
    cout << "\r" << linea << endl;
    cout << "> " << flush;
}



void enviar(const string& paquete) {
    if (paquete.size() != (size_t)TAM_PAQUETE) return;
    imprimir(" >> ENVIADO:  " + protocoloVisible(paquete));
    sendto(SocketFD, paquete.data(), TAM_PAQUETE, 0,
           (const sockaddr *)&servidor, sizeof(servidor));
}

void enviarChat(char accion, const string& msg) {
    enviar(armarChat(accion, miNick, msg));
}



string nombreBase(const string& ruta) {
    size_t pos = ruta.find_last_of("/\\");
    return pos == string::npos ? ruta : ruta.substr(pos + 1);
}


void enviarArchivo(const string& destino, const string& ruta) {
    if (destino.empty() || destino.size() > (size_t)MAX_NICK) {
        imprimir(" Nick destino invalido (max " + to_string(MAX_NICK) + " caracteres)");
        return;
    }

    ifstream in(ruta, ios::binary);
    if (!in) {
        imprimir(" No se pudo abrir el archivo: " + ruta);
        return;
    }
    string contenido((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
    in.close();

  
    int largoNick = max(destino.size(), miNick.size());
    string nombre = nombreBase(ruta);
    if (nombre.empty() || (int)nombre.size() > maxNombreArchivo(largoNick)) {
        imprimir(" Nombre de archivo muy largo (max " + to_string(maxNombreArchivo(largoNick)) + " caracteres)");
        return;
    }

    long long tamArchivo  = contenido.size();
    int       capacidad   = capacidadDatos(largoNick);
    long long totalTrozos = (tamArchivo + capacidad - 1) / capacidad;

    imprimir(" Enviando " + nombre + " (" + to_string(tamArchivo) + " bytes, "+ to_string(totalTrozos) + " trozos de " + to_string(capacidad) + " bytes) a " + destino);

   
    enviar(armarInicio({destino, nombre, tamArchivo}));
    usleep(20000);

   
    for (long long seq = 0; seq < totalTrozos; seq++) {
        string trozo = contenido.substr(seq * capacidad, capacidad);
        enviar(armarDatos({destino, seq, trozo}));
        usleep(PAUSA_US);
    }

   
    usleep(20000);
    enviar(armarFin({destino, totalTrozos}));

    imprimir(" Archivo " + nombre + " enviado.");
}



struct Recepcion {
    string                   nombre;
    long long                tamArchivo = 0;
    map<long long, string>   trozos;          
};
map<string, Recepcion> recepciones;          

void recibirInicio(const string& paquete) {
    InicioArchivo a;
    if (!leerInicio(paquete, a)) return;

    Recepcion& r = recepciones[a.nick];
    r.nombre     = nombreBase(a.nombre);       
    r.tamArchivo = a.tamArchivo;
    r.trozos.clear();

    imprimir(" [ARCHIVO] " + a.nick + " te envia " + r.nombre + " (" + to_string(a.tamArchivo) + " bytes)");
}

void recibirDatos(const string& paquete) {
    DatosArchivo d;
    if (!leerDatos(paquete, d)) return;

    auto it = recepciones.find(d.nick);
    if (it != recepciones.end())
        it->second.trozos[d.secuencia] = d.datos;
}

void recibirFin(const string& paquete) {
    FinArchivo e;
    if (!leerFin(paquete, e)) return;

    auto it = recepciones.find(e.nick);
    if (it == recepciones.end()) {
        imprimir(" [ARCHIVO] Se perdio el paquete F de " + e.nick + ", archivo descartado.");
        return;
    }
    Recepcion& r = it->second;

    
    string    salida = "recibido_" + r.nombre;
    ofstream  out(salida, ios::binary);
    long long faltan = 0, bytes = 0;
    for (long long i = 0; i < e.totalTrozos; i++) {
        auto t = r.trozos.find(i);
        if (t == r.trozos.end()) { faltan++; continue; }
        out.write(t->second.data(), t->second.size());
        bytes += t->second.size();
    }
    out.close();

    if (faltan == 0 && bytes == r.tamArchivo)
        imprimir(" [ARCHIVO] Recibido de " + e.nick + ": " + salida + " (" + to_string(bytes) + " bytes) OK");
    else
        imprimir(" [ARCHIVO] " + salida + " INCOMPLETO: faltan " + to_string(faltan) + " trozos (" + to_string(bytes) + "/" + to_string(r.tamArchivo) + " bytes)");

    recepciones.erase(it);
}




bool procesarComando(const string& input) {
    if (input == "salir" || input == "/quit") {
        enviarChat('Q', "");
        return false;
    }

    if (input == "/lista") {
        enviarChat('L', "");
    }
    else if (input.rfind("/archivo", 0) == 0) {
        stringstream ss(input);
        string cmd, destino, ruta;
        ss >> cmd >> destino;
        getline(ss >> ws, ruta);               
        if (destino.empty() || ruta.empty())
            imprimir(" Formato: /archivo nick ruta");
        else
            enviarArchivo(destino, ruta);
    }
    else if (input[0] == '@') {
        if (input.find(' ') == string::npos)
            imprimir(" Formato: @nick mensaje");
        else
            enviarChat('M', input);            
    }
    else {
        enviarChat('B', input);
    }
    return true;
}



void hiloRecibir() {
    char buffer[TAM_PAQUETE];

    while (conectado) {
        socklen_t largo = sizeof(servidor);
        int n = recvfrom(SocketFD, buffer, TAM_PAQUETE, 0, (sockaddr *)&servidor, &largo);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;  // timeout
            imprimir(" Error de red, desconectado.");
            conectado = false;
            break;
        }
        if (n != TAM_PAQUETE) continue;

        string paquete(buffer, TAM_PAQUETE);
        imprimir(" << RECIBIDO: " + protocoloVisible(paquete));

        // ---- archivos ----
        switch (paquete[0]) {
            case 'F': recibirInicio(paquete); continue;
            case 'D': recibirDatos(paquete);  continue;
            case 'E': recibirFin(paquete);    continue;
        }

        
        MensajeChat m;
        if (!leerChat(paquete, m)) continue;

        switch (m.accion) {
            case 'b': imprimir(m.nick + ": " + m.msg);                   break;
            case 'm': imprimir("[Privado de " + m.nick + "]: " + m.msg); break;
            case 'l': imprimir("[LISTA] " + m.msg);                      break;
        }
    }
}

void hiloEnviar() {
    string input;
    while (conectado) {
        if (!getline(cin, input)) input = "salir";     
        if (!conectado) break;
        if (input.empty()) { imprimir(""); continue; }
        if (!procesarComando(input)) conectado = false;
    }
}



int main(void) {
    SocketFD = socket(AF_INET, SOCK_DGRAM, 0);
    if (SocketFD == -1) {
        perror("cannot create socket");
        exit(EXIT_FAILURE);
    }

    timeval tv = {1, 0};                         
    setsockopt(SocketFD, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    int bufSize = 4 * 1024 * 1024;
    setsockopt(SocketFD, SOL_SOCKET, SO_RCVBUF, &bufSize, sizeof(bufSize));

    memset(&servidor, 0, sizeof(servidor));
    servidor.sin_family = AF_INET;
    servidor.sin_port   = htons(PUERTO);
    if (inet_pton(AF_INET, IP_SERVIDOR, &servidor.sin_addr) <= 0) {
        perror("invalid IP address");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    cout << "Ingresa tu NOMBRE: ";
    getline(cin, miNick);
    if (miNick.empty() || miNick.size() > (size_t)MAX_NICK || miNick.find(' ') != string::npos) {
        cout << " Nombre invalido (1-" << MAX_NICK << " caracteres, sin espacios)" << endl;
        close(SocketFD);
        return 1;
    }

    
    enviarChat('N', "");

    thread recibir(hiloRecibir);
    thread enviarT(hiloEnviar);

    enviarT.join();
    recibir.join();

    close(SocketFD);
    return 0;
}
