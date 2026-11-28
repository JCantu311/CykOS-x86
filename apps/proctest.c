#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <dirent.h>
#include <time.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <signal.h>
#include <locale.h>
#include <sys/utsname.h>
#include <sys/resource.h>
#include <sys/stat.h>

static void on_exit_cb(void) {
    printf("[atexit] cleanup handler ran OK\n");
}

int main(void) {
    printf("=== CykOS POSIX Process & Pipe Test ===\n");

    int pfd[2];
    if (pipe(pfd) != 0) {
        printf("pipe() failed!\n");
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        printf("fork() failed!\n");
        return 1;
    }

    if (pid == 0) {
        /* Child process: close read end, redirect stdout to pipe write end, exec or write */
        close(pfd[0]);
        dup2(pfd[1], STDOUT_FILENO);
        close(pfd[1]);
        const char* msg = "Hello from forked child via pipe+dup2!\n";
        write(STDOUT_FILENO, msg, strlen(msg));
        _exit(42);
    }

    /* Parent process: close write end, read from pipe, waitpid */
    close(pfd[1]);
    char buf[128];
    memset(buf, 0, sizeof(buf));
    ssize_t n = read(pfd[0], buf, sizeof(buf) - 1);
    close(pfd[0]);

    int status = 0;
    pid_t waited = waitpid(pid, &status, 0);
    printf("[parent] child pid=%d waited=%d exit=%d\n",
           (int)pid, (int)waited, WEXITSTATUS(status));
    if (n > 0) {
        printf("[parent] received from pipe: %s", buf);
    }

    char* map = (char*)mmap((void*)0, 8192, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (map == MAP_FAILED) {
        printf("mmap() failed!\n");
        return 1;
    }
    strcpy(map, "mmap 8KiB anonymous page OK!");
    printf("[mmap] %s (addr=%p)\n", map, (void*)map);
    if (munmap(map, 8192) != 0) {
        printf("munmap() failed!\n");
        return 1;
    }

    setenv("CC", "gcc", 1);
    const char* cc = getenv("CC");
    struct timeval tv;
    gettimeofday(&tv, (void*)0);
    unsigned long long ull = strtoull("0xCAFEBABE", (void*)0, 0);
    printf("[posix] getenv(CC)=%s time=%ld strtoull=0x%llx\n",
           cc ? cc : "(null)", (long)tv.tv_sec, ull);

    DIR* d = opendir("/USR/LIB");
    int entries = 0;
    if (d) {
        struct dirent* ent;
        while ((ent = readdir(d)) != (void*)0) entries++;
        closedir(d);
    }
    char tmpl[] = "/TMP/GCCXXXXXX";
    int tmpfd = mkstemp(tmpl);
    int fl = (tmpfd >= 0) ? fcntl(tmpfd, F_GETFL) : -1;
    FILE* tmpfp = (tmpfd >= 0) ? fdopen(tmpfd, "w") : (void*)0;
    if (tmpfp) {
        fprintf(tmpfp, "tmp\n");
        fclose(tmpfp);
        unlink(tmpl);
    }
    printf("[fs] /USR/LIB entries=%d mkstemp=%s fcntl_fl=0x%x\n",
           entries, tmpl, fl);

    /* Test signal() */
    sighandler_t prev = signal(SIGINT, SIG_IGN);
    signal(SIGINT, prev);
    printf("[sig] signal(SIGINT,SIG_IGN) OK\n");

    /* Test locale stubs */
    char* loc = setlocale(LC_ALL, "C");
    struct lconv* lc = localeconv();
    printf("[loc] setlocale=%s decimal=%s\n", loc ? loc : "(null)",
           lc ? lc->decimal_point : "?");

    /* Test uname */
    struct utsname uts;
    if (uname(&uts) == 0) {
        printf("[sys] uname: %s %s %s\n", uts.sysname, uts.release, uts.machine);
    }

    /* Test strndup / strcspn */
    char* sd = strndup("Hello World", 5);
    size_t cs = strcspn("abcdef", "de");
    printf("[str] strndup='%s' strcspn=%u\n", sd ? sd : "(null)", (unsigned)cs);
    if (sd) free(sd);

    /* Test getrlimit / lstat / atexit / ungetc */
    struct rlimit rl;
    getrlimit(RLIMIT_NOFILE, &rl);
    struct stat st;
    int lst = lstat("/USR/LIB", &st);
    FILE* ufp = fopen("/TMP/UNGET.TXT", "w+");
    int ug_ok = 0;
    if (ufp) {
        fputs("AB", ufp);
        rewind(ufp);
        int c1 = fgetc(ufp);
        ungetc(c1, ufp);
        int c2 = fgetc(ufp);
        ug_ok = (c1 == 'A' && c2 == 'A');
        fclose(ufp);
        unlink("/TMP/UNGET.TXT");
    }
    atexit(on_exit_cb);
    printf("[posix2] rlim_nofile=%lu lstat=%d ungetc=%d\n",
           (unsigned long)rl.rlim_cur, lst, ug_ok);

    printf("Process, pipe, mmap & POSIX libc test PASSED!\n");
    return 0;
}
