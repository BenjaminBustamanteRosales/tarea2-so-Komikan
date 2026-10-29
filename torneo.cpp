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

sem_t sem_mesas;

void turno(Mesa& m, Ficha* f) {
    int desde = f->es_puma ? m.pos_puma : m.pos_perro[f->id];
    vector<int> op;
    if (f->es_puma)
        for (auto s : saltos(desde))
            if (m.tab[s.first] >= 0 && m.tab[s.second] == VACIO) op.push_back(s.second);
    if (op.empty())
        for (int v : vecinos(desde))
            if (m.tab[v] == VACIO) op.push_back(v);
    if (!op.empty()) mover(m, f->es_puma, f->id, op[rand_r(&f->seed) % op.size()]);
}

void* hilo_ficha(void* arg) {
    Ficha* f = static_cast<Ficha*>(arg);
    Mesa& m = *f->mesa;
    bool seguir = true;
    while (seguir) {
        dormir(f->seed);
        pthread_mutex_lock(&m.mtx);
        if (g_sigint) finalizar(m, "partida interrumpida por SIGINT");
        if (cfg.falla > 0 && rand_r(&f->seed) % 1000 < cfg.falla)
            finalizar(m, string("gana ") + (f->es_puma ? "los PERROS" : "el PUMA") + " por abandono del oponente");
        seguir = !m.terminada && (f->es_puma || m.pos_perro[f->id] >= 0);
        if (seguir) turno(m, f);
        pthread_mutex_unlock(&m.mtx);
    }
    return nullptr;
}

void* juez(void* arg) {
    Mesa& m = *static_cast<Mesa*>(arg);
    iniciar(m);
    log_mesa(m, "Comienza la partida", true);
    for (int i = 0; i < 13; i++) {
        m.fichas[i] = {&m, i == 0, i - 1, cfg.seed + m.id * 100 + i};
        pthread_create(&m.hilos[i], nullptr, hilo_ficha, &m.fichas[i]);
    }
    if (!esperar(&m.fin)) {
        pthread_mutex_lock(&m.mtx);
        finalizar(m, "partida interrumpida por SIGINT");
        pthread_mutex_unlock(&m.mtx);
    }
    for (int i = 0; i < 13; i++) pthread_join(m.hilos[i], nullptr);
    log_mesa(m, "FIN: " + m.resultado, false);
    pthread_mutex_destroy(&m.mtx);
    sem_destroy(&m.fin);
    sem_post(&sem_mesas);
    return nullptr;
}

int g_n, g_m, g_creadas = 0;
vector<Mesa> g_mesas;
vector<pthread_t> g_hilos_mesa;
pthread_mutex_t mm_mtx = PTHREAD_MUTEX_INITIALIZER;
sem_t sem_puma, sem_perro;
queue<int> cola_puma, cola_perro;

void* cliente(void* arg) {
    int id = *static_cast<int*>(arg);
    unsigned seed = cfg.seed + id;
    timespec ts{0, (rand_r(&seed) % 200) * 1000000L};
    nanosleep(&ts, nullptr);
    if (g_sigint) return nullptr;
    bool puma = id % 2 == 0;
    pthread_mutex_lock(&mm_mtx);
    (puma ? cola_puma : cola_perro).push(id);
    pthread_mutex_unlock(&mm_mtx);
    log_texto("[Servidor] Llega el cliente " + to_string(id) + (puma ? " como PUMA\n" : " como PERROS\n"));
    sem_post(puma ? &sem_puma : &sem_perro);
    return nullptr;
}

void* matchmaker(void*) {
    for (int i = 0; i < g_n / 2; i++) {
        if (!esperar(&sem_puma) || !esperar(&sem_perro)) break;
        pthread_mutex_lock(&mm_mtx);
        int cp = cola_puma.front();
        cola_puma.pop();
        int cd = cola_perro.front();
        cola_perro.pop();
        pthread_mutex_unlock(&mm_mtx);
        log_texto("[Servidor] Pareja " + to_string(i + 1) + ": cliente " + to_string(cp) + " (PUMA) vs cliente " +
                  to_string(cd) + " (PERROS)\n");
        if (!esperar(&sem_mesas)) break;
        g_mesas[i].id = i + 1;
        g_mesas[i].cli_puma = cp;
        g_mesas[i].cli_perro = cd;
        pthread_create(&g_hilos_mesa[i], nullptr, juez, &g_mesas[i]);
        g_creadas++;
    }
    return nullptr;
}

void manejador(int) {
    g_sigint = 1;
}

int main(int argc, char** argv) {
    if (argc < 3 || argc > 4) {
        printf("Uso: %s N M [config.txt]\n", argv[0]);
        return 1;
    }
    g_n = atoi(argv[1]);
    g_m = atoi(argv[2]);
    if (g_n < 2 || g_m < 1) {
        printf("N debe ser >= 2 y M >= 1\n");
        return 1;
    }
    if (argc == 4 && !leer_config(argv[3])) return 1;

    struct sigaction sa = {};
    sa.sa_handler = manejador;
    sigaction(SIGINT, &sa, nullptr);

    sem_init(&sem_mesas, 0, g_m);
    sem_init(&sem_puma, 0, 0);
    sem_init(&sem_perro, 0, 0);
    g_mesas.resize(g_n / 2);
    g_hilos_mesa.resize(g_n / 2);
    vector<pthread_t> hilos(g_n);
    vector<int> ids(g_n);

    pthread_t mm;
    pthread_create(&mm, nullptr, matchmaker, nullptr);
    for (int i = 0; i < g_n; i++) {
        ids[i] = i;
        pthread_create(&hilos[i], nullptr, cliente, &ids[i]);
    }
    for (int i = 0; i < g_n; i++) pthread_join(hilos[i], nullptr);
    pthread_join(mm, nullptr);
    for (int i = 0; i < g_creadas; i++) pthread_join(g_hilos_mesa[i], nullptr);

    if (g_sigint) puts("Servidor interrumpido con SIGINT: recursos liberados.");
    sem_destroy(&sem_mesas);
    sem_destroy(&sem_puma);
    sem_destroy(&sem_perro);
    pthread_mutex_destroy(&mm_mtx);
    pthread_mutex_destroy(&log_mtx);
    return 0;
}
