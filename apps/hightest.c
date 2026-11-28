#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>

int main(int argc, char** argv) {
    if (argc >= 2 && strcmp(argv[1], "--child") == 0) {
        const char* val = getenv("CYKOS_GCC_ENV");
        printf("[high-child] pid=%d CYKOS_GCC_ENV=%s code_addr=%p\n",
               (int)getpid(), val ? val : "(null)", (void*)&main);
        return (val && strcmp(val, "ok") == 0) ? 77 : 1;
    }

    printf("=== CykOS High-VM (64 MiB..1 GiB) GCC Readiness Test ===\n");
    printf("[high] code=%p stack_var=%p\n", (void*)&main, (void*)&argc);

    /* Allocate 24 MiB via malloc (beyond legacy 16 MiB low heap limit) */
    size_t big_sz = 24u * 1024u * 1024u;
    unsigned char* big = (unsigned char*)malloc(big_sz);
    if (!big) {
        printf("[high] malloc(24 MiB) FAILED!\n");
        return 1;
    }
    big[0] = 0xAA;
    big[big_sz - 1] = 0x55;
    printf("[high] malloc(24 MiB) OK at %p (first=0x%x last=0x%x)\n",
           (void*)big, big[0], big[big_sz - 1]);
    free(big);

    /* Allocate 16 MiB via mmap in high mmap arena (0x30000000+) */
    void* m = mmap((void*)0, 16u * 1024u * 1024u, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED) {
        printf("[high] mmap(16 MiB) FAILED!\n");
        return 1;
    }
    strcpy((char*)m, "high mmap 16 MiB OK");
    printf("[high] mmap(16 MiB) OK at %p: %s\n", m, (char*)m);
    munmap(m, 16u * 1024u * 1024u);

    /* Test fork() + execve() with custom envp */
    pid_t pid = fork();
    if (pid < 0) {
        printf("[high] fork() FAILED!\n");
        return 1;
    }
    if (pid == 0) {
        char* cargv[] = { "/USR/BIN/HIGHTEST.BIN", "--child", NULL };
        char* cenvp[] = { "PATH=/USR/BIN:/BIN", "CYKOS_GCC_ENV=ok", NULL };
        execve("/USR/BIN/HIGHTEST.BIN", cargv, cenvp);
        _exit(127);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    printf("[high] child exit=%d\n", WEXITSTATUS(status));
    if (WEXITSTATUS(status) != 77) {
        printf("[high] execve envp check FAILED!\n");
        return 1;
    }

    printf("High-VM (64 MiB..1 GiB) & envp test PASSED!\n");
    return 0;
}
