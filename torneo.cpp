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

int main(int argc, char** argv) {
    if (argc == 2 && !leer_config(argv[1])) return 1;
    printf("seed=%u delay=%d-%d puma=%d falla=%d\n", cfg.seed, cfg.dmin, cfg.dmax, cfg.puma, cfg.falla);
    return 0;
}