#include <stdlib.h>

extern int main(int argc, char** argv);

__attribute__((section(".entry"), used))
void _start(int argc, char** argv) {
    int ret = main(argc, argv);
    exit(ret);
}
