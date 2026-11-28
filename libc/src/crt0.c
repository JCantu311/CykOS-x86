#include <stdlib.h>

extern int main(int argc, char** argv, char** envp);
extern void __cykos_init_env(char** envp);

__attribute__((section(".entry"), used))
int _start(int argc, char** argv, char** envp) {
    if (envp) {
        __cykos_init_env(envp);
    }
    int ret = main(argc, argv, envp ? envp : environ);
    exit(ret);
    return ret;
}
