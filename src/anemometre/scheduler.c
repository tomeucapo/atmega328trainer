#include "scheduler.h"
#include <stddef.h>

// ======================================================
// SCHEDULER COOPERATIVO
// sched_tick() lo invoca la ISR de TIMER0 (1 ms). El bucle
// principal llama a sched_manage() para ejecutar cada tarea
// cuando le toca. Las tareas se ejecutan hasta que terminan:
// ninguna debe bloquearse indefinidamente.
// ======================================================

static volatile uint32_t tick_count = 0;

static sched_task_t *tasks = NULL;
static uint8_t task_count = 0;

void sched_tick(void)
{
    tick_count++;
}

uint32_t sched_get_tick(void)
{
    return tick_count;
}

void sched_init(sched_task_t *task_list, uint8_t count)
{
    uint8_t i;
    uint32_t now = tick_count;

    tasks = task_list;
    task_count = count;

    for(i = 0; i < task_count; i++)
    {
        tasks[i].next_run = now + tasks[i].phase + tasks[i].interval;
    }
}

void sched_manage(void)
{
    uint8_t i;
    uint32_t now = tick_count;

    for(i = 0; i < task_count; i++)
    {
        // Si la tarea llega tarde, se re-programa desde ahora:
        // no se acumulan ejecuciones perdidas.
        if((int32_t)(now - tasks[i].next_run) >= 0)
        {
            tasks[i].next_run = now + tasks[i].interval;
            tasks[i].fn();
        }
    }
}