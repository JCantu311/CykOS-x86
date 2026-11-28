#include "sched.h"
#include "gdt.h"
#include "idt.h"
#include "heap.h"
#include "cykastd.h"

typedef struct {
    unsigned int pid;
    unsigned int parent_pid;
    task_state_t state;
    char name[24];
    unsigned int kernel_esp;
    void* kernel_stack;
    unsigned int kernel_stack_top;
    unsigned int sleep_until_ms;
    unsigned int quantum;
    unsigned int total_ticks;
    int exit_code;
    int is_idle;
    paging_space_t* vm_space;
} task_t;

static task_t g_tasks[SCHED_MAX_TASKS];
static task_t* g_current_task;
static task_t* g_idle_task;
static unsigned int g_next_pid = 2;
static volatile int g_preempt_depth;
static int g_sched_initialized;

extern void sched_context_switch(unsigned int* old_esp, unsigned int new_esp);

__asm__(
    ".text\n"
    ".align 4\n"
    ".global sched_context_switch\n"
    "sched_context_switch:\n"
    "    movl 4(%esp), %eax\n"
    "    movl 8(%esp), %edx\n"
    "    pushl %ebp\n"
    "    pushl %ebx\n"
    "    pushl %esi\n"
    "    pushl %edi\n"
    "    movl %esp, (%eax)\n"
    "    movl %edx, %esp\n"
    "    popl %edi\n"
    "    popl %esi\n"
    "    popl %ebx\n"
    "    popl %ebp\n"
    "    ret\n"
    ".global fork_child_iret_trampoline\n"
    "fork_child_iret_trampoline:\n"
    "    popl %eax\n"
    "    movw %ax, %ds\n"
    "    movw %ax, %es\n"
    "    movw %ax, %fs\n"
    "    movw %ax, %gs\n"
    "    popa\n"
    "    addl $8, %esp\n"
    "    iret\n"
);
extern void fork_child_iret_trampoline(void);

static unsigned int irq_save(void) {
    unsigned int flags;
    __asm__ volatile("pushfl; popl %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

static void irq_restore(unsigned int flags) {
    if (flags & (1u << 9)) {
        __asm__ volatile("sti" ::: "memory");
    }
}

static void sched_copy_name(char* dst, const char* src, unsigned int max_len) {
    unsigned int i = 0;
    if (src) {
        while (src[i] && i + 1u < max_len) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = '\0';
}

static void sched_idle_loop(void* arg) {
    (void)arg;
    for (;;) {
        __asm__ volatile("sti; hlt");
    }
}

static void sched_thread_trampoline(void (*entry)(void*), void* arg) {
    __asm__ volatile("sti" ::: "memory");
    if (entry) entry(arg);
    sched_exit(0);
}

static void sched_reap_zombies_locked(void) {
    for (unsigned int i = 1; i < SCHED_MAX_TASKS; i++) {
        task_t* t = &g_tasks[i];
        if (t->state == TASK_ZOMBIE && t != g_current_task && t->kernel_stack && t->parent_pid == 0) {
            void* stack = t->kernel_stack;
            memset(t, 0, sizeof(*t));
            free(stack);
        }
    }
}

static task_t* sched_pick_next_locked(void) {
    unsigned int current_idx = (unsigned int)(g_current_task - g_tasks);
    for (unsigned int offset = 1; offset <= SCHED_MAX_TASKS; offset++) {
        unsigned int idx = (current_idx + offset) % SCHED_MAX_TASKS;
        task_t* candidate = &g_tasks[idx];
        if (candidate->state == TASK_READY && !candidate->is_idle) {
            return candidate;
        }
    }
    if (g_current_task->state == TASK_RUNNING && !g_current_task->is_idle) {
        return g_current_task;
    }
    if (g_idle_task && (g_idle_task->state == TASK_READY || g_idle_task == g_current_task)) {
        return g_idle_task;
    }
    return g_current_task;
}

static void sched_schedule_locked(void) {
    if (!g_sched_initialized || !g_current_task) return;
    task_t* prev = g_current_task;
    task_t* next = sched_pick_next_locked();
    if (!next || next == prev) {
        if (prev->state == TASK_READY) prev->state = TASK_RUNNING;
        return;
    }

    if (prev->state == TASK_RUNNING) {
        prev->state = TASK_READY;
    }
    next->state = TASK_RUNNING;
    next->quantum = SCHED_TIME_SLICE_MS;
    g_current_task = next;

    if (next->kernel_stack_top) {
        gdt_set_kernel_stack(next->kernel_stack_top);
    }
    paging_switch_address_space(next->vm_space);
    sched_context_switch(&prev->kernel_esp, next->kernel_esp);
}

void sched_init(void) {
    unsigned int flags = irq_save();
    memset(g_tasks, 0, sizeof(g_tasks));
    g_preempt_depth = 0;

    g_tasks[0].pid = 1;
    g_tasks[0].state = TASK_RUNNING;
    sched_copy_name(g_tasks[0].name, "kernel", sizeof(g_tasks[0].name));
    g_tasks[0].kernel_stack = (void*)0;
    g_tasks[0].kernel_stack_top = gdt_get_kernel_stack();
    g_tasks[0].quantum = SCHED_TIME_SLICE_MS;
    g_tasks[0].is_idle = 0;
    g_current_task = &g_tasks[0];
    g_next_pid = 2;
    g_sched_initialized = 1;

    int idle_pid = sched_spawn_kernel("idle", sched_idle_loop, (void*)0);
    if (idle_pid > 0) {
        for (unsigned int i = 0; i < SCHED_MAX_TASKS; i++) {
            if (g_tasks[i].pid == (unsigned int)idle_pid) {
                g_tasks[i].is_idle = 1;
                g_idle_task = &g_tasks[i];
                break;
            }
        }
    }
    irq_restore(flags);
}

int sched_spawn_kernel(const char* name, void (*entry)(void*), void* arg) {
    if (!g_sched_initialized || !entry) return -1;

    unsigned int flags = irq_save();
    sched_reap_zombies_locked();

    int slot = -1;
    for (unsigned int i = 1; i < SCHED_MAX_TASKS; i++) {
        if (g_tasks[i].state == TASK_UNUSED) {
            slot = (int)i;
            break;
        }
    }
    if (slot < 0) {
        irq_restore(flags);
        return -1;
    }

    void* stack = malloc(SCHED_STACK_SIZE);
    if (!stack) {
        irq_restore(flags);
        return -1;
    }

    task_t* t = &g_tasks[slot];
    memset(t, 0, sizeof(*t));
    t->pid = g_next_pid++;
    t->state = TASK_READY;
    sched_copy_name(t->name, name ? name : "kthread", sizeof(t->name));
    t->kernel_stack = stack;
    t->kernel_stack_top = ((unsigned int)stack + SCHED_STACK_SIZE) & ~0xFu;
    t->quantum = SCHED_TIME_SLICE_MS;

    unsigned int* sp = (unsigned int*)t->kernel_stack_top;
    *--sp = (unsigned int)arg;
    *--sp = (unsigned int)entry;
    *--sp = 0u; /* dummy return address for sched_thread_trampoline */
    *--sp = (unsigned int)sched_thread_trampoline;
    *--sp = 0u; /* ebp */
    *--sp = 0u; /* ebx */
    *--sp = 0u; /* esi */
    *--sp = 0u; /* edi */
    t->kernel_esp = (unsigned int)sp;

    int pid = (int)t->pid;
    irq_restore(flags);
    return pid;
}

void sched_exit(int exit_code) {
    (void)irq_save();
    if (!g_sched_initialized || !g_current_task || g_current_task == &g_tasks[0]) {
        for (;;) __asm__ volatile("sti; hlt");
    }
    g_current_task->exit_code = exit_code;
    g_current_task->state = TASK_ZOMBIE;
    sched_schedule_locked();
    for (;;) __asm__ volatile("sti; hlt");
}

void sched_yield(void) {
    if (!g_sched_initialized) return;
    unsigned int flags = irq_save();
    if (g_preempt_depth == 0) {
        sched_schedule_locked();
    }
    irq_restore(flags);
}

void sched_sleep_ms(unsigned int ms) {
    if (ms == 0) return;
    if (!g_sched_initialized || !g_current_task || g_preempt_depth > 0) {
        unsigned int start = idt_get_millis();
        while ((unsigned int)(idt_get_millis() - start) < ms) {
            __asm__ volatile("hlt" ::: "memory");
        }
        return;
    }

    unsigned int flags = irq_save();
    g_current_task->sleep_until_ms = idt_get_millis() + ms;
    g_current_task->state = TASK_BLOCKED;
    sched_schedule_locked();
    irq_restore(flags);
}

void sched_tick_from_irq(void) {
    if (!g_sched_initialized || !g_current_task) return;

    unsigned int now = idt_get_millis();
    for (unsigned int i = 0; i < SCHED_MAX_TASKS; i++) {
        if (g_tasks[i].state == TASK_BLOCKED &&
            (int)(now - g_tasks[i].sleep_until_ms) >= 0) {
            g_tasks[i].state = TASK_READY;
        }
    }

    g_current_task->total_ticks++;
    if (g_preempt_depth > 0) return;

    if (g_current_task->quantum > 0) {
        g_current_task->quantum--;
    }
    if (g_current_task->quantum == 0) {
        g_current_task->quantum = SCHED_TIME_SLICE_MS;
        sched_schedule_locked();
    }
}

void sched_preempt_disable(void) {
    g_preempt_depth++;
}

void sched_preempt_enable(void) {
    if (g_preempt_depth > 0) g_preempt_depth--;
}

void sched_set_current_vm_space(paging_space_t* space) {
    if (g_current_task) {
        g_current_task->vm_space = space;
    }
}

paging_space_t* sched_get_current_vm_space(void) {
    return g_current_task ? g_current_task->vm_space : (void*)0;
}

int sched_spawn_user_fork(const char* name, paging_space_t* vm_space,
                          const registers_t* child_regs, unsigned int parent_pid) {
    if (!g_sched_initialized || !vm_space || !child_regs) return -1;

    unsigned int flags = irq_save();
    sched_reap_zombies_locked();

    int slot = -1;
    for (unsigned int i = 1; i < SCHED_MAX_TASKS; i++) {
        if (g_tasks[i].state == TASK_UNUSED) {
            slot = (int)i;
            break;
        }
    }
    if (slot < 0) {
        irq_restore(flags);
        return -1;
    }

    void* stack = malloc(SCHED_STACK_SIZE);
    if (!stack) {
        irq_restore(flags);
        return -1;
    }

    unsigned int stack_top = ((unsigned int)stack + SCHED_STACK_SIZE) & ~0xFu;
    unsigned int* sp = (unsigned int*)stack_top;

    /* Copy registers_t onto child's kernel stack for fork_child_iret_trampoline */
    sp -= sizeof(registers_t) / sizeof(unsigned int);
    registers_t* frame = (registers_t*)sp;
    *frame = *child_regs;
    frame->eax = 0; /* Child receives 0 from fork() */

    /* sched_context_switch pops edi, esi, ebx, ebp, then ret -> fork_child_iret_trampoline */
    *(--sp) = (unsigned int)fork_child_iret_trampoline;
    *(--sp) = 0; /* ebp */
    *(--sp) = 0; /* ebx */
    *(--sp) = 0; /* esi */
    *(--sp) = 0; /* edi */

    task_t* t = &g_tasks[slot];
    memset(t, 0, sizeof(*t));
    t->pid = g_next_pid++;
    t->parent_pid = parent_pid;
    t->state = TASK_READY;
    sched_copy_name(t->name, name ? name : "forked", sizeof(t->name));
    t->kernel_stack = stack;
    t->kernel_stack_top = stack_top;
    t->kernel_esp = (unsigned int)sp;
    t->quantum = SCHED_TIME_SLICE_MS;
    t->vm_space = vm_space;

    int pid = (int)t->pid;
    irq_restore(flags);
    return pid;
}

int sched_waitpid(unsigned int parent_pid, int target_pid, int* status_out, int options) {
    if (!g_sched_initialized) return -1;

    for (;;) {
        unsigned int flags = irq_save();
        int has_matching_child = 0;

        for (unsigned int i = 1; i < SCHED_MAX_TASKS; i++) {
            task_t* t = &g_tasks[i];
            if (t->state == TASK_UNUSED || t->parent_pid != parent_pid) continue;
            if (target_pid > 0 && (int)t->pid != target_pid) continue;

            has_matching_child = 1;
            if (t->state == TASK_ZOMBIE) {
                int child_pid = (int)t->pid;
                int exit_code = t->exit_code;
                void* stack = t->kernel_stack;
                memset(t, 0, sizeof(*t));
                irq_restore(flags);
                if (stack) free(stack);
                if (status_out) *status_out = (exit_code & 0xFF) << 8;
                return child_pid;
            }
        }
        irq_restore(flags);

        if (!has_matching_child) return -1;
        if (options & 1) return 0; /* WNOHANG */
        sched_sleep_ms(5u);
    }
}

unsigned int sched_current_pid(void) {
    return g_current_task ? g_current_task->pid : 1u;
}

int sched_list_tasks(sched_task_info_t* out, int max_tasks) {
    if (!out || max_tasks <= 0) return 0;
    unsigned int flags = irq_save();
    sched_reap_zombies_locked();
    int count = 0;
    for (unsigned int i = 0; i < SCHED_MAX_TASKS && count < max_tasks; i++) {
        if (g_tasks[i].state == TASK_UNUSED) continue;
        out[count].pid = g_tasks[i].pid;
        out[count].state = (unsigned int)g_tasks[i].state;
        sched_copy_name(out[count].name, g_tasks[i].name, sizeof(out[count].name));
        out[count].total_ticks = g_tasks[i].total_ticks;
        count++;
    }
    irq_restore(flags);
    return count;
}
