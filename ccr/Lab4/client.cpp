
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

using namespace std;

// ============ CONSTANTES ============
#define TAM_NICK_SIZE 7
#define TAM_MSG_SIZE  11

string miNickname;

// ============ FUNCIONES AUXILIARES ============

string zeroPad(int numero, int tamano) {
    string str = to_string(numero);
    if (str.length() >= (size_t)tamano) return str;
    return string(tamano - str.length(), '0') + str;
}

void escribirTodo(int S, const string& data) {
    int total = data.size();
    int escritos = 0;
    while (escritos < total) {
        int n = write(S, data.c_str() + escritos, total - escritos);
        if (n <= 0) return;
        escritos += n;
    }
}

bool leerTodo(int S, char* buffer, int total) {
    int leidos = 0;
    while (leidos < total) {
        int n = read(S, buffer + leidos, total - leidos);
        if (n <= 0) return false;
        leidos += n;
    }
    return true;
}

void enviarRegistro(int S, const string& nick) {
    string data;
    data += 'N';
    data += zeroPad(nick.size(), TAM_NICK_SIZE);
    data += nick;
    data += zeroPad(0, TAM_MSG_SIZE);
    escribirTodo(S, data);
}

// Enviar acción del juego
void enviarJuego(int S, char tipo) {
    string data;
    data += 'T';
    data += tipo;
    escribirTodo(S, data);
}

// Enviar movimiento
void enviarMovimiento(int S, int pos) {
    string data;
    data += 'T';
    data += 'M';
    data += (char)('0' + pos);
    escribirTodo(S, data);
}

// ============ MOSTRAR TABLERO ============

void mostrarTablero(const string& tablero) {
    cout << "\n     |     |     " << endl;
    cout << "  " << tablero[0] << "  |  " << tablero[1] << "  |  " << tablero[2] << "  " << endl;
    cout << "_____|_____|_____" << endl;
    cout << "     |     |     " << endl;
    cout << "  " << tablero[3] << "  |  " << tablero[4] << "  |  " << tablero[5] << "  " << endl;
    cout << "_____|_____|_____" << endl;
    cout << "     |     |     " << endl;
    cout << "  " << tablero[6] << "  |  " << tablero[7] << "  |  " << tablero[8] << "  " << endl;
    cout << "     |     |     " << endl;
    cout << "\nPosiciones:  1|2|3  /  4|5|6  /  7|8|9" << endl;
}

// ============ PEDIR MOVIMIENTO ============

int pedirPosicion() {
    while (true) {
        cout << "Elige posicion (1-9): ";
        string input;
        getline(cin, input);
        if (input.empty()) continue;
        int pos = atoi(input.c_str());
        if (pos >= 1 && pos <= 9) return pos;
        cout << "Posicion invalida" << endl;
    }
}

// ============ MAIN ============

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
        perror("inet_pton failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }
    else if (0 == Res) {
        perror("invalid IP");
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
        close(SocketFD);
        return 1;
    }

    enviarRegistro(SocketFD, miNickname);
    cout << " Conectado como: " << miNickname << endl;

    // Menú
    cout << "\n=== MENÚ ===" << endl;
    cout << "1. Jugar" << endl;
    cout << "2. Ser espectador" << endl;
    cout << "3. Salir" << endl;
    cout << "Opción: ";

    string opcionStr;
    getline(cin, opcionStr);
    int opcion = atoi(opcionStr.c_str());

    if (opcion == 1) {
        enviarJuego(SocketFD, 'P');
        cout << " Esperando oponente..." << endl;
    } else if (opcion == 2) {
        enviarJuego(SocketFD, 'V');
        cout << " Modo espectador activado. Esperando partida..." << endl;
    } else {
        close(SocketFD);
        return 0;
    }

    // Loop principal
    while (true) {
        char tipo, sub;
        int n = read(SocketFD, &tipo, 1);
        if (n <= 0) {
            cout << "\n Desconectado del servidor" << endl;
            break;
        }
        if (tipo != 'T') continue;

        n = read(SocketFD, &sub, 1);
        if (n <= 0) break;

        if (sub == 'T') {
            // Tablero
            char board[9];
            if (!leerTodo(SocketFD, board, 9)) break;
            string tablero(board, 9);
            mostrarTablero(tablero);
        }
        else if (sub == 't') {
            // Turno
            char simbolo;
            if (!leerTodo(SocketFD, &simbolo, 1)) break;
            cout << "\n>>> ES TU TURNO. Eres '" << simbolo << "' <<<" << endl;
            int pos = pedirPosicion();
            enviarMovimiento(SocketFD, pos);
        }
        else if (sub == 'E') {
            // Error Position Taken
            // [T][E][P][T][pos]
            char p1, t1, posChar;
            if (!leerTodo(SocketFD, &p1, 1)) break;
            if (!leerTodo(SocketFD, &t1, 1)) break;
            if (!leerTodo(SocketFD, &posChar, 1)) break;

            int pos = posChar - '0';
            cout << "\n[!] Posición " << pos << " ya ocupada" << endl;
            int nueva = pedirPosicion();
            enviarMovimiento(SocketFD, nueva);
        }
        else if (sub == 'W') {
            cout << "\n*** ¡GANASTE! ***" << endl;
        }
        else if (sub == 'O') {
            cout << "\n*** Perdiste... ***" << endl;
        }
    }

    close(SocketFD);
    return 0;
}