/*
 * ============================================================================
 *  LAB0UDP - protocolo.h
 *  Formato de TODOS los paquetes. Lo usan server.cpp y client.cpp.
 * ============================================================================
 *
 *  Cada datagrama UDP mide EXACTAMENTE 100 bytes. Lo que sobra se rellena
 *  con '\0'. Los numeros van en texto rellenados con ceros a la izquierda.
 *
 *  ---------------------------------------------------------------- CHAT ----
 *   [1 accion][7 tamNick][nick][11 tamMsg][msg]
 *
 *   Cliente -> Servidor              Servidor -> Cliente
 *   N  registrar nick                b  mensaje a todos
 *   B  mensaje a todos               m  mensaje privado
 *   M  privado: msg="@dest texto"    l  lista de conectados
 *   L  pedir lista
 *   Q  salir
 *
 *   Ejemplo:  B0000003ana00000000004hola
 *
 *  -------------------------------------------------------------- LISTA -----
 *   Cliente envia L. El servidor responde con varios paquetes 'l'
 *   (formato chat, nick="Sistema"):
 *       l0000007Sistema00000000013Conectados: 3
 *       l0000007Sistema00000000015ana, juan, pedro
 *
 *  ----------------------------------------------------------- ARCHIVOS -----
 *   Se usa la MISMA letra en los dos sentidos (como en la pizarra).
 *   Cliente -> Servidor: nick = DESTINO
 *   Servidor -> Cliente: nick = ORIGEN   (el servidor lo cambia)
 *
 *   F  inicio  [F][13 tamNick][nick][13 tamNombre][nombre][25 tamArchivo]
 *   D  datos   [D][2 tamNick][nick][7 secuencia][2 tamDatos][datos]
 *   E  fin     [E][2 tamNick][nick][7 totalTrozos]
 *
 *   Ejemplo: ana envia foto.jpg (200 bytes) a juan
 *     F0000000000004juan0000000000008foto.jpg0000000000000000000000200
 *     D04juan000000084<84 bytes>      (trozo 0, 84 bytes de datos)
 *     D04juan000000184<84 bytes>      (trozo 1)
 *     D04juan000000232<32 bytes>      (trozo 2, el ultimo)
 *     E04juan0000003                  (fin: 3 trozos)
 *   Datos por paquete D = 100 - (1 + 2 + tamNick + 7 + 2) = 88 - tamNick
 * ============================================================================
 */

#ifndef PROTOCOLO_H
#define PROTOCOLO_H

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <string>
#include <algorithm>

using namespace std;

// ============================== CONSTANTES ==================================

const int TAM_PAQUETE = 100;
const int MAX_NICK    = 20;        // para que todos los paquetes quepan

// Chat
const int CHAT_TAM_NICK = 7;
const int CHAT_TAM_MSG  = 11;
const int CHAT_MAX_TEXTO = TAM_PAQUETE - 1 - CHAT_TAM_NICK - CHAT_TAM_MSG;   // 81

// Archivo: paquete F (anchos de la pizarra)
const int F_TAM_NICK    = 13;
const int F_TAM_NOMBRE  = 13;
const int F_TAM_ARCHIVO = 25;
const int F_FIJO = 1 + F_TAM_NICK + F_TAM_NOMBRE + F_TAM_ARCHIVO;          // 52

// Archivo: paquetes D y E
const int D_TAM_NICK  = 2;
const int D_TAM_SEQ   = 7;
const int D_TAM_DATOS = 2;

// Bytes de archivo que caben en un paquete D
inline int capacidadDatos(int largoNick) {
    return TAM_PAQUETE - (1 + D_TAM_NICK + largoNick + D_TAM_SEQ + D_TAM_DATOS);
}

// Largo maximo del nombre de archivo en un paquete F
inline int maxNombreArchivo(int largoNick) {
    return TAM_PAQUETE - F_FIJO - largoNick;
}

// ============================== UTILIDADES ==================================

inline string zeroPad(long long numero, int ancho) {
    string s = to_string(numero);
    if ((int)s.size() >= ancho) return s;
    return string(ancho - s.size(), '0') + s;
}

// Muestra el paquete tal como viaja. Bytes no imprimibles se ven como '.'
// El relleno '\0' del final no se dibuja, solo se indica cuantos bytes son.
inline string protocoloVisible(const string& p) {
    int usado = p.size();
    while (usado > 0 && p[usado - 1] == '\0') usado--;
    string s;
    for (int i = 0; i < usado; i++) {
        unsigned char c = p[i];
        s += (c >= 32 && c < 127) ? (char)c : '.';
    }
    if (usado < (int)p.size())
        s += "  [+" + to_string(p.size() - usado) + " bytes de relleno = 100]";
    return s;
}

// Arma un paquete de 100 bytes campo por campo.
// Si algo no cabe, ok() devuelve false.
class Escritor {
public:
    Escritor() { memset(buf, 0, TAM_PAQUETE); }

    Escritor& letra(char c) { return texto(string(1, c)); }

    Escritor& numero(long long v, int ancho) {
        string s = zeroPad(v, ancho);
        if ((int)s.size() != ancho) bien = false;
        return texto(s);
    }

    Escritor& texto(const string& s) {
        if (!bien || off + (int)s.size() > TAM_PAQUETE) { bien = false; return *this; }
        memcpy(buf + off, s.data(), s.size());
        off += s.size();
        return *this;
    }

    // [tamaño][texto]
    Escritor& campo(int anchoTam, const string& s) { return numero(s.size(), anchoTam).texto(s); }

    bool ok() const { return bien; }
    string paquete() const { return bien ? string(buf, TAM_PAQUETE) : ""; }

private:
    char buf[TAM_PAQUETE];
    int  off  = 0;
    bool bien = true;
};

// Lee un paquete de 100 bytes campo por campo.
// Si algo esta mal formado, ok() devuelve false.
class Lector {
public:
    Lector(const string& p) : p(p) {}

    Lector& letra(char& c) {
        string s;
        texto(1, s);
        if (bien) c = s[0];
        return *this;
    }

    Lector& numero(int ancho, long long& v) {
        string s;
        texto(ancho, s);
        if (!bien) return *this;
        for (char c : s) if (!isdigit((unsigned char)c)) { bien = false; return *this; }
        v = atoll(s.c_str());
        return *this;
    }

    Lector& texto(long long n, string& s) {
        if (!bien || n < 0 || off + n > (long long)p.size()) { bien = false; return *this; }
        s = p.substr(off, n);
        off += n;
        return *this;
    }

    // [tamaño][texto]
    Lector& campo(int anchoTam, string& s) {
        long long n = 0;
        numero(anchoTam, n);
        return texto(n, s);
    }

    bool ok() const { return bien; }

private:
    const string& p;
    int  off  = 0;
    bool bien = true;
};

// ================================ CHAT ======================================

struct MensajeChat {
    char   accion = 0;
    string nick;
    string msg;
};

inline string armarChat(char accion, const string& nick, const string& msg) {
    string n = nick.substr(0, CHAT_MAX_TEXTO);
    string m = msg.substr(0, CHAT_MAX_TEXTO - n.size());
    Escritor w;
    w.letra(accion).campo(CHAT_TAM_NICK, n).campo(CHAT_TAM_MSG, m);
    return w.paquete();
}

inline bool leerChat(const string& p, MensajeChat& m) {
    Lector r(p);
    r.letra(m.accion).campo(CHAT_TAM_NICK, m.nick).campo(CHAT_TAM_MSG, m.msg);
    return r.ok();
}

// ============================== ARCHIVOS ====================================

// F: [F][13 tamNick][nick][13 tamNombre][nombre][25 tamArchivo]
struct InicioArchivo {
    string    nick;
    string    nombre;
    long long tamArchivo = 0;
};

inline string armarInicio(const InicioArchivo& a) {
    Escritor w;
    w.letra('F').campo(F_TAM_NICK, a.nick).campo(F_TAM_NOMBRE, a.nombre)
     .numero(a.tamArchivo, F_TAM_ARCHIVO);
    return w.paquete();
}

inline bool leerInicio(const string& p, InicioArchivo& a) {
    char c = 0;
    Lector r(p);
    r.letra(c).campo(F_TAM_NICK, a.nick).campo(F_TAM_NOMBRE, a.nombre)
     .numero(F_TAM_ARCHIVO, a.tamArchivo);
    return r.ok() && c == 'F';
}

// D: [D][2 tamNick][nick][7 secuencia][2 tamDatos][datos]
struct DatosArchivo {
    string    nick;
    long long secuencia = 0;
    string    datos;
};

inline string armarDatos(const DatosArchivo& d) {
    Escritor w;
    w.letra('D').campo(D_TAM_NICK, d.nick).numero(d.secuencia, D_TAM_SEQ)
     .campo(D_TAM_DATOS, d.datos);
    return w.paquete();
}

inline bool leerDatos(const string& p, DatosArchivo& d) {
    char c = 0;
    Lector r(p);
    r.letra(c).campo(D_TAM_NICK, d.nick).numero(D_TAM_SEQ, d.secuencia)
     .campo(D_TAM_DATOS, d.datos);
    return r.ok() && c == 'D';
}

// E: [E][2 tamNick][nick][7 totalTrozos]
struct FinArchivo {
    string    nick;
    long long totalTrozos = 0;
};

inline string armarFin(const FinArchivo& e) {
    Escritor w;
    w.letra('E').campo(D_TAM_NICK, e.nick).numero(e.totalTrozos, D_TAM_SEQ);
    return w.paquete();
}

inline bool leerFin(const string& p, FinArchivo& e) {
    char c = 0;
    Lector r(p);
    r.letra(c).campo(D_TAM_NICK, e.nick).numero(D_TAM_SEQ, e.totalTrozos);
    return r.ok() && c == 'E';
}

#endif
