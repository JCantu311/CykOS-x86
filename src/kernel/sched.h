#ifndef CYKOS_SCHED_H
#define CYKOS_SCHED_H

#include "paging.h"
#include "idt.h"

#define SCHED_MAX_TASKS     16
#define SCHED_TIME_SLICE_MS 10u
#define SCHED_STACK_SIZE    16384u

typedef enum {
    TASK_UNUSED = 0,
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_ZOMBIE
} task_state_t;

typedef struct {
    unsigned int pid;
    unsigned int state;
    char name[24];
    unsigned int total_ticks;
} sched_task_info_t;

void sched_init(void);
int sched_spawn_kernel(const char* name, void (*entry)(void*), void* arg);
void sched_exit(int exit_code) __attribute__((noreturn));
void sched_yield(void);
void sched_sleep_ms(unsigned int ms);
void sched_tick_from_irq(void);
void sched_preempt_disable(void);
void sched_preempt_enable(void);
void sched_set_current_vm_space(paging_space_t* space);
paging_space_t* sched_get_current_vm_space(void);
int sched_spawn_user_fork(const char* name, paging_space_t* vm_space,
                          const registers_t* child_regs, unsigned int parent_pid);
int sched_waitpid(unsigned int parent_pid, int target_pid, int* status_out, int options);
unsigned int sched_current_pid(void);
int sched_list_tasks(sched_task_info_t* out, int max_tasks);

#endif /* CYKOS_SCHED_H */
