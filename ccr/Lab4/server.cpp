/* Server code - Juego 3 en Raya */

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
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <algorithm>
#include <ctime>

using namespace std;

// ============ CONSTANTES ============
#define TAM_NICK_SIZE 7
#define TAM_MSG_SIZE  11

// ============ ESTRUCTURA DE PARTIDA ============
struct Partida {
    int id;
    string jugadorX;
    string jugadorO;
    string turno;
    char tablero[9];
    bool activa;
    vector<string> espectadores;
};

// ============ VARIABLES GLOBALES ============
map<string, int> ListOfCli;
mutex mtxDatos;
vector<Partida> partidas;
vector<string> colaEspera;
int siguienteId = 1;

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

// ============ REGISTRO (reusa el protocolo del chat) ============

bool recibirRegistro(int S, char& accion, string& nick) {
    char buff[1000];
    int n, tamano;

    n = read(S, buff, 1);
    if (n <= 0) return false;
    accion = buff[0];

    n = read(S, buff, TAM_NICK_SIZE);
    if (n <= 0) return false;
    buff[n] = '\0';
    tamano = atoi(buff);

    n = read(S, buff, tamano);
    if (n <= 0) return false;
    buff[n] = '\0';
    nick = buff;

    n = read(S, buff, TAM_MSG_SIZE);
    if (n <= 0) return false;
    buff[n] = '\0';
    tamano = atoi(buff);

    if (tamano > 0) read(S, buff, tamano);  // ignorar

    return true;
}

// ============ ENVIAR MENSAJES DEL JUEGO ============

void enviarTablero(int S, const Partida& p) {
    string data;
    data += 'T';
    data += 'T';
    for (int i = 0; i < 9; i++) data += p.tablero[i];
    escribirTodo(S, data);
}

void enviarTurno(int S, char simbolo) {
    string data;
    data += 'T';
    data += 't';
    data += simbolo;
    escribirTodo(S, data);
}

void enviarPosTomada(int S, int pos) {
    string data;
    data += 'T';
    data += 'E';
    data += 'P';
    data += 'T';
    data += (char)('0' + pos);
    escribirTodo(S, data);
}

void enviarVictoria(int S) {
    escribirTodo(S, "TW");
}

void enviarDerrota(int S) {
    escribirTodo(S, "TO");
}

// ============ LÓGICA DEL JUEGO ============

bool verificarGanador(const char tablero[9], char simbolo) {
    int L[8][3] = {
        {0,1,2},{3,4,5},{6,7,8},
        {0,3,6},{1,4,7},{2,5,8},
        {0,4,8},{2,4,6}
    };
    for (int i = 0; i < 8; i++) {
        if (tablero[L[i][0]] == simbolo &&
            tablero[L[i][1]] == simbolo &&
            tablero[L[i][2]] == simbolo)
            return true;
    }
    return false;
}

bool verificarEmpate(const char tablero[9]) {
    for (int i = 0; i < 9; i++)
        if (tablero[i] == ' ') return false;
    return true;
}

void broadcastTablero(const Partida& p) {
    auto itX = ListOfCli.find(p.jugadorX);
    if (itX != ListOfCli.end()) enviarTablero(itX->second, p);

    auto itO = ListOfCli.find(p.jugadorO);
    if (itO != ListOfCli.end()) enviarTablero(itO->second, p);

    for (const auto& nick : p.espectadores) {
        auto itE = ListOfCli.find(nick);
        if (itE != ListOfCli.end()) enviarTablero(itE->second, p);
    }
}

Partida* buscarPartidaDe(const string& nick) {
    for (auto& p : partidas) {
        if (p.activa && (p.jugadorX == nick || p.jugadorO == nick))
            return &p;
    }
    return nullptr;
}

void crearPartidaSiPosible() {
    if (colaEspera.size() < 2) return;

    string j1 = colaEspera[0];
    string j2 = colaEspera[1];
    colaEspera.erase(colaEspera.begin(), colaEspera.begin() + 2);

    // Asignar X/O al azar
    string jugadorX, jugadorO;
    if (rand() % 2 == 0) {
        jugadorX = j1; jugadorO = j2;
    } else {
        jugadorX = j2; jugadorO = j1;
    }

    Partida p;
    p.id = siguienteId++;
    p.jugadorX = jugadorX;
    p.jugadorO = jugadorO;
    p.turno = jugadorX;  // X empieza
    p.activa = true;
    for (int i = 0; i < 9; i++) p.tablero[i] = ' ';

    partidas.push_back(p);
    Partida& nueva = partidas.back();

    cout << "[+] Partida " << nueva.id << ": "
         << jugadorX << "(X) vs " << jugadorO << "(O)" << endl;

    auto itX = ListOfCli.find(jugadorX);
    auto itO = ListOfCli.find(jugadorO);

    if (itX != ListOfCli.end()) {
        enviarTablero(itX->second, nueva);
        enviarTurno(itX->second, 'X');
    }
    if (itO != ListOfCli.end()) {
        enviarTablero(itO->second, nueva);
    }
}

// ============ HANDLERS ============

void handlePlay(const string& nick) {
    lock_guard<mutex> lock(mtxDatos);

    // Si ya está en cola o jugando, ignorar
    if (find(colaEspera.begin(), colaEspera.end(), nick) != colaEspera.end())
        return;
    if (buscarPartidaDe(nick) != nullptr) return;

    colaEspera.push_back(nick);
    cout << "[+] " << nick << " quiere jugar. En cola: "
         << colaEspera.size() << endl;
    crearPartidaSiPosible();
}

void handleView(int S, const string& nick) {
    lock_guard<mutex> lock(mtxDatos);

    // Buscar la última partida activa
    for (int i = partidas.size() - 1; i >= 0; i--) {
        if (partidas[i].activa) {
            partidas[i].espectadores.push_back(nick);
            enviarTablero(S, partidas[i]);
            cout << "[+] " << nick << " es espectador de partida "
                 << partidas[i].id << endl;
            return;
        }
    }
    cout << "[!] " << nick << " quiere ver pero no hay partida activa" << endl;
}

void handleMove(int S, const string& nick, int pos) {
    lock_guard<mutex> lock(mtxDatos);

    Partida* p = buscarPartidaDe(nick);
    if (p == nullptr) return;

    if (p->turno != nick) return;  // No es su turno

    if (pos < 1 || pos > 9) return;

    if (p->tablero[pos-1] != ' ') {
        enviarPosTomada(S, pos);
        return;
    }

    char simbolo = (p->jugadorX == nick) ? 'X' : 'O';
    p->tablero[pos-1] = simbolo;

    // ¿Ganó?
    if (verificarGanador(p->tablero, simbolo)) {
        p->activa = false;
        broadcastTablero(*p);

        string perdedor = (simbolo == 'X') ? p->jugadorO : p->jugadorX;
        auto itG = ListOfCli.find(nick);
        auto itP = ListOfCli.find(perdedor);
        if (itG != ListOfCli.end()) enviarVictoria(itG->second);
        if (itP != ListOfCli.end()) enviarDerrota(itP->second);

        cout << "[+] " << nick << " gano partida " << p->id << endl;
        return;
    }

    // ¿Empate?
    if (verificarEmpate(p->tablero)) {
        p->activa = false;
        broadcastTablero(*p);
        cout << "[+] Empate en partida " << p->id << endl;
        return;
    }

    // Cambiar turno
    p->turno = (p->turno == p->jugadorX) ? p->jugadorO : p->jugadorX;
    broadcastTablero(*p);

    auto itTurno = ListOfCli.find(p->turno);
    if (itTurno != ListOfCli.end()) {
        char sig = (p->turno == p->jugadorX) ? 'X' : 'O';
        enviarTurno(itTurno->second, sig);
    }
}

// ============ MANEJAR CLIENTE ============

void manejarCliente(int S) {
    char accion;
    string nickname;

    // Registro
    if (!recibirRegistro(S, accion, nickname)) {
        close(S);
        return;
    }
    if (accion != 'N') {
        close(S);
        return;
    }

    {
        lock_guard<mutex> lock(mtxDatos);
        ListOfCli[nickname] = S;
        cout << "[+] " << nickname << " conectado. Total: "
             << ListOfCli.size() << endl;
    }

    // Loop principal
    while (true) {
        char tipo, sub;
        int n = read(S, &tipo, 1);
        if (n <= 0) break;
        if (tipo != 'T') continue;

        n = read(S, &sub, 1);
        if (n <= 0) break;

        if (sub == 'P') {
            handlePlay(nickname);
        }
        else if (sub == 'V') {
            handleView(S, nickname);
        }
        else if (sub == 'M') {
            char posChar;
            n = read(S, &posChar, 1);
            if (n <= 0) break;
            handleMove(S, nickname, posChar - '0');
        }
    }

    // Desconexión
    {
        lock_guard<mutex> lock(mtxDatos);

        // Quitar de la cola
        colaEspera.erase(
            remove(colaEspera.begin(), colaEspera.end(), nickname),
            colaEspera.end());

        // Si estaba jugando, el otro gana
        for (auto& p : partidas) {
            if (p.activa && (p.jugadorX == nickname || p.jugadorO == nickname)) {
                string otro = (p.jugadorX == nickname) ? p.jugadorO : p.jugadorX;
                p.activa = false;
                auto it = ListOfCli.find(otro);
                if (it != ListOfCli.end()) enviarVictoria(it->second);
                cout << "[!] " << nickname << " se desconecto. "
                     << otro << " gana." << endl;
                break;
            }
        }

        // Quitar de espectadores
        for (auto& p : partidas) {
            p.espectadores.erase(
                remove(p.espectadores.begin(), p.espectadores.end(), nickname),
                p.espectadores.end());
        }

        ListOfCli.erase(nickname);
        cout << "[-] " << nickname << " desconectado. Total: "
             << ListOfCli.size() << endl;
    }

    close(S);
}

// ============ MAIN ============

int main(void) {
    srand(time(NULL));

    struct sockaddr_in stSockAddr;
    int ServerSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

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

    cout << "[+] Servidor 3 en Raya - Puerto 1100" << endl;
    cout << "[+] Esperando clientes..." << endl;

    for (;;) {
        int ClientSocket = accept(ServerSocket, NULL, NULL);
        if (0 > ClientSocket) {
            perror("error accept failed");
            continue;
        }
        thread(manejarCliente, ClientSocket).detach();
    }

    close(ServerSocket);
    return 0;
}