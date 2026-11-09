#include "syscall.h"
#include "idt.h"
#include "cykastd.h"
#include "fat16.h"
#include "heap.h"

// Maximum open file descriptors
#define MAX_FDS 8

typedef struct {
    int in_use;
    fat16_entry_t entry;
    unsigned int cursor;
} fd_table_t;

static fd_table_t g_fds[MAX_FDS];

static void syscall_handler(registers_t* regs) {
    unsigned int syscall_num = regs->eax;
    unsigned int arg1 = regs->ebx;
    unsigned int arg2 = regs->ecx;
    unsigned int arg3 = regs->edx;
    unsigned int arg4 = regs->esi;

    switch (syscall_num) {
        case SYS_EXIT: {
            // arg1 = exit code
            // Return exit code in eax
            regs->eax = arg1;
            break;
        }

        case SYS_WRITE: {
            // arg1 = fd, arg2 = buffer pointer, arg3 = length
            int fd = (int)arg1;
            const char* buf = (const char*)arg2;
            size_t count = (size_t)arg3;

            if (!buf || count == 0) {
                regs->eax = 0;
                break;
            }

            if (fd == FD_STDOUT) {
                for (size_t i = 0; i < count; i++) {
                    print_char(buf[i]);
                }
                regs->eax = (unsigned int)count;
            } else if (fd == FD_STDERR) {
                for (size_t i = 0; i < count; i++) {
                    char temp[2] = { buf[i], '\0' };
                    print_color_string(temp, VGA_RED);
                }
                regs->eax = (unsigned int)count;
            } else {
                regs->eax = (unsigned int)-1; // Not supported on other fds yet
            }
            break;
        }

        case SYS_READ: {
            // arg1 = fd, arg2 = buffer pointer, arg3 = count
            int fd = (int)arg1;
            char* buf = (char*)arg2;
            size_t count = (size_t)arg3;

            if (!buf || count == 0) {
                regs->eax = 0;
                break;
            }

            if (fd == FD_STDIN) {
                size_t read_bytes = 0;
                while (read_bytes < count) {
                    char c = get_char();
                    buf[read_bytes++] = c;
                    print_char(c); // Echo character
                    if (c == '\n' || c == '\r') {
                        break;
                    }
                }
                regs->eax = (unsigned int)read_bytes;
            } else if (fd >= 3 && fd < MAX_FDS && g_fds[fd].in_use) {
                int bytes = fat16_read_file(&g_fds[fd].entry, buf, count);
                regs->eax = (unsigned int)(bytes >= 0 ? bytes : -1);
            } else {
                regs->eax = (unsigned int)-1;
            }
            break;
        }

        case SYS_OPEN: {
            // arg1 = filename
            const char* filename = (const char*)arg1;
            if (!filename) {
                regs->eax = (unsigned int)-1;
                break;
            }

            int found_fd = -1;
            for (int i = 3; i < MAX_FDS; i++) {
                if (!g_fds[i].in_use) {
                    found_fd = i;
                    break;
                }
            }

            if (found_fd == -1) {
                regs->eax = (unsigned int)-1; // No free FDs
                break;
            }

            if (fat16_find_file(filename, &g_fds[found_fd].entry) == 0) {
                g_fds[found_fd].in_use = 1;
                g_fds[found_fd].cursor = 0;
                regs->eax = (unsigned int)found_fd;
            } else {
                regs->eax = (unsigned int)-1; // File not found
            }
            break;
        }

        case SYS_CLOSE: {
            // arg1 = fd
            int fd = (int)arg1;
            if (fd >= 3 && fd < MAX_FDS && g_fds[fd].in_use) {
                g_fds[fd].in_use = 0;
                regs->eax = 0;
            } else {
                regs->eax = (unsigned int)-1;
            }
            break;
        }

        case SYS_BRK: {
            // arg1 = increment in bytes (if 0, returns current break)
            int incr = (int)arg1;
            void* res = sbrk(incr);
            regs->eax = (unsigned int)res;
            break;
        }

        case SYS_CLEAR: {
            clear_screen();
            regs->eax = 0;
            break;
        }

        case SYS_SET_CURSOR: {
            // arg1 = col, arg2 = row
            unsigned short pos = (arg2 * 80 + arg1);
            outb(0x3D4, 0x0E);
            outb(0x3D5, (pos >> 8) & 0xFF);
            outb(0x3D4, 0x0F);
            outb(0x3D5, pos & 0xFF);
            regs->eax = 0;
            break;
        }

        case SYS_GET_KEY: {
            char c = get_char();
            regs->eax = (unsigned char)c;
            break;
        }

        case SYS_HAS_KEY: {
            regs->eax = (unsigned int)has_key();
            break;
        }

        case SYS_SLEEP: {
            secwait(arg1);
            regs->eax = 0;
            break;
        }

        case SYS_FAT_READ: {
            // arg1 = filename, arg2 = buffer, arg3 = max_bytes
            int res = fat16_read_file_by_name((const char*)arg1, (void*)arg2, arg3);
            regs->eax = (unsigned int)res;
            break;
        }

        case SYS_FAT_WRITE: {
            // arg1 = filename, arg2 = data, arg3 = size
            int res = fat16_write_file((const char*)arg1, (const void*)arg2, arg3);
            regs->eax = (unsigned int)res;
            break;
        }

        default: {
            // Unknown syscall
            regs->eax = (unsigned int)-1;
            break;
        }
    }
}

void syscall_init(void) {
    memset(g_fds, 0, sizeof(g_fds));
    register_interrupt_handler(128, syscall_handler);
}
