#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <semaphore.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <queue>
#include <string>
#include <vector>
using namespace std;

const int APEX = 25, TIZQ = 26, TDER = 27;
const int VACIO = -1, PUMA = -2;

struct Config {
    unsigned seed = 42;
    int dmin = 50, dmax = 300, puma = 12, falla = 0;
} cfg;

volatile sig_atomic_t g_sigint = 0;
pthread_mutex_t log_mtx = PTHREAD_MUTEX_INITIALIZER;

bool leer_config(const char* ruta) {
    ifstream f(ruta);
    if (!f) {
        printf("No se pudo abrir %s\n", ruta);
        return false;
    }
    string p;
    while (f >> p && p != "EVENT") {
        if (p == "SEED") f >> cfg.seed;
        else if (p == "DELAY_MIN") f >> cfg.dmin;
        else if (p == "DELAY_MAX") f >> cfg.dmax;
        else if (p == "FALLA_PROB") f >> cfg.falla;
        else if (p == "PUMA_INIT") {
            int r, c;
            f >> r >> c;
            cfg.puma = r * 5 + c;
        }
    }
    if (cfg.dmin < 1) cfg.dmin = 1;
    if (cfg.dmax < cfg.dmin) cfg.dmax = cfg.dmin;
    if (cfg.puma < 0 || cfg.puma > 24) cfg.puma = 12;
    return true;
}

void log_texto(const string& s) {
    pthread_mutex_lock(&log_mtx);
    fputs(s.c_str(), stdout);
    fflush(stdout);
    pthread_mutex_unlock(&log_mtx);
}

void dormir(unsigned& seed) {
    int ms = cfg.dmin + rand_r(&seed) % (cfg.dmax - cfg.dmin + 1);
    timespec ts{ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&ts, nullptr);
}

bool esperar(sem_t* s) {
    while (!g_sigint) {
        timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 100000000L;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        if (sem_timedwait(s, &ts) == 0) return true;
    }
    return false;
}

vector<int> vecinos(int n) {
    if (n == APEX) return {TIZQ, TDER, 2};
    if (n == TIZQ) return {APEX, TDER, 1};
    if (n == TDER) return {APEX, TIZQ, 3};
    vector<int> v;
    int r = n / 5, c = n % 5;
    if (r > 0) v.push_back(n - 5);
    if (r < 4) v.push_back(n + 5);
    if (c > 0) v.push_back(n - 1);
    if (c < 4) v.push_back(n + 1);
    if (n == 1) v.push_back(TIZQ);
    if (n == 2) v.push_back(APEX);
    if (n == 3) v.push_back(TDER);
    return v;
}

vector<pair<int, int>> saltos(int n) {
    vector<pair<int, int>> v;
    if (n >= 25) return v;
    int r = n / 5, c = n % 5;
    if (r >= 2) v.push_back({n - 5, n - 10});
    if (r <= 2) v.push_back({n + 5, n + 10});
    if (c >= 2) v.push_back({n - 1, n - 2});
    if (c <= 2) v.push_back({n + 1, n + 2});
    return v;
}

struct Mesa;

struct Ficha {
    Mesa* mesa;
    bool es_puma;
    int id;
    unsigned seed;
};

struct Mesa {
    int id, cli_puma, cli_perro;
    pthread_mutex_t mtx;
    sem_t fin;
    int tab[28];
    int pos_puma, pos_perro[12];
    int capturas;
    bool terminada;
    string resultado;
    Ficha fichas[13];
    pthread_t hilos[13];
};

void iniciar(Mesa& m) {
    pthread_mutex_init(&m.mtx, nullptr);
    sem_init(&m.fin, 0, 0);
    for (int i = 0; i < 28; i++) m.tab[i] = VACIO;
    m.capturas = 0;
    m.terminada = false;
    m.pos_puma = cfg.puma;
    m.tab[cfg.puma] = PUMA;
    for (int n = 24, d = 0; n >= 0 && d < 12; n--)
        if (m.tab[n] == VACIO) {
            m.tab[n] = d;
            m.pos_perro[d++] = n;
        }
}

string nombre(int n) {
    if (n >= 25) return n == APEX ? "TRONO_TOP" : n == TIZQ ? "TRONO_LEFT" : "TRONO_RIGHT";
    return "(" + to_string(n / 5) + "," + to_string(n % 5) + ")";
}

void log_mesa(Mesa& m, const string& texto, bool con_tablero) {
    string s = "[Mesa " + to_string(m.id) + "] " + texto + "\n";
    if (con_tablero) {
        auto sim = [&](int n) { return m.tab[n] == VACIO ? '.' : m.tab[n] == PUMA ? 'P' : "0123456789AB"[m.tab[n]]; };
        s += string("              ") + sim(APEX) + "\n";
        s += string("            ") + sim(TIZQ) + "   " + sim(TDER) + "\n";
        for (int r = 0; r < 5; r++) {
            s += "        ";
            for (int c = 0; c < 5; c++) s += string(1, sim(r * 5 + c)) + " ";
            s += "\n";
        }
    }
    log_texto(s);
}

void finalizar(Mesa& m, const string& texto) {
    if (m.terminada) return;
    m.terminada = true;
    m.resultado = texto;
    sem_post(&m.fin);
}

bool acorralado(Mesa& m) {
    for (int v : vecinos(m.pos_puma))
        if (m.tab[v] == VACIO) return false;
    for (auto s : saltos(m.pos_puma))
        if (m.tab[s.first] >= 0 && m.tab[s.second] == VACIO) return false;
    return true;
}

int validar(Mesa& m, bool es_puma, int desde, int hasta, int& sobre) {
    if (hasta < 0 || hasta >= 28 || m.tab[hasta] != VACIO) return 0;
    for (int v : vecinos(desde))
        if (v == hasta) return 1;
    if (es_puma)
        for (auto s : saltos(desde))
            if (s.second == hasta && m.tab[s.first] >= 0) {
                sobre = s.first;
                return 2;
            }
    return 0;
}

void mover(Mesa& m, bool es_puma, int id, int dest) {
    int desde = es_puma ? m.pos_puma : m.pos_perro[id];
    int sobre = -1;
    string texto = (es_puma ? "PUMA " : "PERRO " + to_string(id) + " ") + nombre(desde) + " -> " + nombre(dest);
    int tipo = validar(m, es_puma, desde, dest, sobre);
    if (tipo == 0) {
        log_mesa(m, "MOVIMIENTO INVALIDO " + texto, false);
        return;
    }
    m.tab[desde] = VACIO;
    if (es_puma) {
        m.tab[dest] = PUMA;
        m.pos_puma = dest;
    } else {
        m.tab[dest] = id;
        m.pos_perro[id] = dest;
    }
    if (tipo == 2) {
        int d = m.tab[sobre];
        m.tab[sobre] = VACIO;
        m.pos_perro[d] = -1;
        m.capturas++;
        texto += " (captura al perro " + to_string(d) + ")";
    }
    log_mesa(m, texto, true);
    if (m.capturas >= 6) finalizar(m, "gana el PUMA con 6 capturas");
    else if (acorralado(m)) finalizar(m, "ganan los PERROS, el puma quedo acorralado");
}
//
int main(int argc, char** argv) {
    if (argc == 2 && !leer_config(argv[1])) return 1;
    Mesa m{};
    m.id = 1;
    iniciar(m);
    log_mesa(m, "Tablero inicial", true);
    mover(m, true, 0, 7);
    mover(m, true, 0, 0);
    mover(m, false, 8, 11);
    pthread_mutex_destroy(&m.mtx);
    sem_destroy(&m.fin);
    return 0;
}