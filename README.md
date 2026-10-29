# Torneo de Komikan Concurrente

Tarea 2 de Sistemas Operativos. Simulación de un torneo de Komikan en tiempo real: un servidor empareja jugadores (Puma vs Perros), asigna mesas de juego concurrentes y cada mesa tiene un juez que valida los movimientos de las fichas.

**Integrantes:** Benjamín Bustamante, Catalina Molina

## Contenido del repositorio

|  Archivo        |                                Descripción |
|-----------------|----------------------------------------------------------------------------------|
| `torneo.cpp`    |          Código fuente completo (C++17 con pthreads y semáforos POSIX)           |
| `test_cases.txt`|     Casos de prueba entregados; el programa lee de aquí la primera línea `CONFIG`|
| `README.md`     |             Este mismo archivo                                                   |

## Cómo compilar

Requiere Linux, macOS o WSL con `g++` instalado (en Ubuntu: `sudo apt install build-essential`).

```bash (terminal)
g++ -Wall -Wextra -std=c++17 -pthread -o torneo torneo.cpp
```

La compilación no debe mostrar errores ni advertencias.

## Cómo ejecutar

```bash
./torneo N M [config.txt]
```

- `N`: cantidad total de clientes (la mitad juega de Puma y la mitad de Perros; si N es impar, un cliente queda sin pareja).
- `M`: máximo de mesas activas al mismo tiempo.
- `config.txt`: archivo de configuración opcional.

Ejemplos: (pruebas realizadas con exito)

```bash
./torneo 6 2 test_cases.txt      # 6 clientes, 2 mesas
./torneo 20 2 test_cases.txt     # más jugadores que mesas: los pares esperan
./torneo 4 2                     # sin archivo: usa los valores por defecto
```

La salida es larga porque se imprime cada movimiento con el tablero. Para ver solo el resumen (parejas formadas y resultado de cada partida) se puede filtrar con `grep`:

```bash
./torneo 6 2 test_cases.txt | grep -E "Pareja|FIN"
```

### Probar Ctrl+C

Con una ejecución larga en curso (por ejemplo `./torneo 20 2 test_cases.txt`), presionar `Ctrl+C`. Cada mesa activa termina con `FIN: partida interrumpida por SIGINT` y el programa cierra con el mensaje `Servidor interrumpido con SIGINT: recursos liberados.`

### Probar el abandono de un jugador

Crear un `config.txt` con una línea como esta y ejecutar con el siguinete comando:

```
CONFIG SEED 42 DELAY_MIN 20 DELAY_MAX 60 PUMA_INIT 2 2 FALLA_PROB 10
```

Algunas partidas terminarán con: --> `gana el PUMA por abandono del oponente` (o `gana los PERROS ...`).
lo que variaria seria el nombre del ganador, netamente por como se desarolle el juego.

## Archivo de configuración

Palabras clave seguidas de su valor, en una o varias líneas. La lectura se detiene al llegar a la primera palabra `EVENT` (por eso también sirve `test_cases.txt`, del cual se usa solo la primera línea `CONFIG`).

|     Clave       |                                  Significado                                                                | Valor por defecto |
|-----------------|-------------------------------------------------------------------------------------------------------------|-------------------|
| `SEED`          | Semilla aleatoria                                                                                           |        42         |
| `DELAY_MIN`     | Retardo mínimo entre movimientos (ms)                                                                       |        50         |
| `DELAY_MAX`     | Retardo máximo entre movimientos (ms)                                                                       |        300        |
| `PUMA_INIT r c` | Casilla inicial del puma (fila y columna)                                                                   |         2 2       |
| `FALLA_PROB`    | Probabilidad (por mil) de que un jugador falle en cada intento de movimiento; sirve para probar el abandono |          0        |

## Reglas implementadas

- Tablero de 5x5 casillas con un trono de 3 casillas arriba (`TRONO_TOP`, `TRONO_LEFT`, `TRONO_RIGHT`).
- El puma se mueve a casillas adyacentes libres y captura saltando sobre un perro hacia una casilla vacía en línea recta.
- Los perros se mueven a casillas adyacentes libres y no pueden capturar.
- Gana el puma con 6 o más capturas. Ganan los perros si el puma queda sin movimientos ni saltos posibles.
- Si un jugador abandona, gana el oponente por abandono.
- No existe empate.

## Funciones principales

|        Función            |                              Qué hace                                              |
|---------------------------|------------------------------------------------------------------------------------|
| `leer_config`             | Lee semilla, retardos, posición del puma y probabilidad de falla                   |
| `log_texto`, `log_mesa`   | Imprimen eventos y el tablero sin mezclar líneas entre mesas                       |
| `dormir`                  | Retardo aleatorio entre `DELAY_MIN` y `DELAY_MAX`                                  |
| `esperar`                 | Espera un semáforo cada 100 ms revisando si llegó Ctrl+C                           |
| `vecinos`, `saltos`       | Calculan las casillas adyacentes y los saltos posibles desde una casilla           |
| `iniciar`                 | Prepara una mesa: tablero, puma, 12 perros, mutex y semáforo                       |
| `validar`, `mover`        | El juez valida un movimiento (adyacencia, casilla libre, captura) y lo aplica      |
| `acorralado`, `finalizar` | Detectan la derrota del puma y cierran la partida                                  |
| `turno`                   | Elige el movimiento de una ficha (el puma prefiere capturar)                       |
| `hilo_ficha`              | Hilo de cada ficha: espera, bloquea la mesa y mueve                                |
| `juez`                    | Hilo de cada mesa: crea las 13 fichas, espera el fin y libera la mesa              |
| `cliente`                 | Hilo de cada jugador: llega en un momento aleatorio y se anota en su cola          |
| `matchmaker`              | Empareja un puma con un perro y espera una mesa libre                              |
| `manejador`               | Manejador de SIGINT: activa una bandera global                                     |

## Diseño y justificación

**Estructura.** N hilos cliente se anotan en una cola de pumas o de perros. Un hilo matchmaker saca un puma y un perro, espera una mesa libre y lanza un hilo juez. Cada juez crea 13 hilos (1 puma y 12 perros) que mueven fichas con retardos aleatorios.

**Exclusión mutua.** Cada mesa tiene un único `pthread_mutex_t` que protege todo su estado (tablero, posiciones, capturas y bandera de término). Una ficha toma el lock, valida y aplica su movimiento, y lo suelta. Así, si dos fichas quieren la misma casilla, solo la primera en tomar el lock la ocupa; la otra ve la casilla ocupada y no se mueve.

**Límite de mesas.** Un semáforo POSIX (`sem_mesas`) inicializado en M. El matchmaker hace espera pasiva antes de abrir una mesa y el juez lo libera al terminar, por lo que nunca hay más de M mesas activas.

**Otras primitivas.** Los semáforos `sem_puma` y `sem_perro` cuentan los clientes en cola; un mutex protege las colas; un semáforo por mesa (`fin`) avisa al juez que la partida terminó; un mutex serializa la impresión.

**Riesgos de concurrencia.**
- *Deadlock:* no hay espera circular porque cada mesa usa un solo mutex, y el único anidamiento es mutex de mesa seguido del mutex de impresión, siempre en ese orden.
- *Starvation:* las colas son FIFO, así que ningún cliente espera indefinidamente. Entre las fichas, los retardos aleatorios hacen muy improbable que una acapare el lock.
- *Livelock:* las fichas no reintentan en un ciclo apretado; siempre duermen un retardo aleatorio entre intentos.

**Ctrl+C.** El manejador solo activa una bandera. Los hilos la revisan: los clientes dejan de anotarse, el matchmaker deja de emparejar, los jueces cierran sus mesas y el programa libera semáforos y mutex antes de terminar.

**Por qué un mutex por mesa.** Es la opción más simple de razonar y de depurar, y elimina el riesgo de deadlock. El costo es que las fichas de una misma mesa no se mueven en paralelo, pero la sección crítica es muy corta comparada con los retardos de 50 a 300 ms, así que casi no afecta. Una alternativa con un mutex por casilla permite más concurrencia, pero obliga a ordenar los bloqueos para evitar deadlock.

## Supuestos y limitaciones

- El enunciado incluye el dibujo del tablero. Se asumió un trono de 3 casillas conectado a la fila superior, con movimientos ortogonales; los saltos del puma se hacen solo en línea recta entre casillas de la cuadrícula.
- Los perros y el puma eligen sus movimientos al azar (el puma prefiere capturar si puede). No hay límite de jugadas, por lo que una partida podría alargarse.
- Del archivo `test_cases.txt` solo se lee la configuración inicial; los eventos (`EVENT ...`) no se ejecutan.

