#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>

typedef struct {
    void (*fn)(void);    // funcion de la tarea
    uint16_t interval;   // periodo en ms
    uint16_t phase;      // retardo inicial en ms (escalona las tareas)
    uint32_t next_run;   // tick de la proxima ejecucion (uso interno)
} sched_task_t;

// Inicializa la tabla de tareas. El tick y el acceso al contador
// los gestiona el scheduler; sched_tick() debe llamarse desde una
// ISR de 1 ms.
void sched_init(sched_task_t *tasks, uint8_t count);

// Incrementa el contador de ticks (llamar desde la ISR de TIMER0).
void sched_tick(void);

// Devuelve el contador de ticks (ms transcurridos).
uint32_t sched_get_tick(void);

// Ejecuta las tareas que les toque (llamar desde el bucle principal).
void sched_manage(void);

#endif