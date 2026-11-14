#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

int main(int argc, char** argv) {
    printf("========================================\n");
    printf("  Hello from CykOS POSIX C Standard Library!\n");
    printf("========================================\n");

    printf("Command-line argument count (argc): %d\n", argc);
    for (int i = 0; i < argc; i++) {
        printf("  argv[%d] = '%s'\n", i, argv[i]);
    }

    char cwd[128];
    if (getcwd(cwd, sizeof(cwd))) {
        printf("Current working directory: %s\n", cwd);
    }

    // Dynamic heap allocation test
    char* dynamic_str = (char*)malloc(64);
    if (dynamic_str) {
        snprintf(dynamic_str, 64, "Dynamic memory allocation (malloc + snprintf) OK!");
        printf("[HEAP] %s\n", dynamic_str);
        free(dynamic_str);
    }

    // File I/O test
    const char* test_file = "/TMP/POSIX.TXT";
    printf("[FILE] Writing to %s via fopen/fprintf/fclose...\n", test_file);
    FILE* fp = fopen(test_file, "w");
    if (fp) {
        fprintf(fp, "CykOS POSIX C Library File I/O Test\nLine 2: 123456789\n");
        fclose(fp);
        printf("[FILE] Write succeeded.\n");
    }

    fp = fopen(test_file, "r");
    if (fp) {
        char line[128];
        printf("[FILE] Reading back %s contents:\n", test_file);
        while (fgets(line, sizeof(line), fp)) {
            printf("  > %s", line);
        }
        fclose(fp);
    }

    printf("POSIX C Library test complete.\n");
    return 0;
}
