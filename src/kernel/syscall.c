#include "syscall.h"
#include "idt.h"
#include "cykastd.h"
#include "vfs.h"
#include "heap.h"
#include "modload.h"
#include "usercmd.h"
#include "permissions.h"

// Maximum open file descriptors
#define MAX_FDS 8
#define SYS_FILE_IO_LIMIT (1024 * 1024)

#define OPEN_ACCMODE 0x0003
#define OPEN_RDONLY  0x0000
#define OPEN_WRONLY  0x0001
#define OPEN_RDWR    0x0002
#define OPEN_CREAT   0x0040
#define OPEN_EXCL    0x0080
#define OPEN_TRUNC   0x0200
#define OPEN_APPEND  0x0400
#define OPEN_LARGEFILE 0x8000

typedef struct {
    int in_use;
    vfs_file_t entry;
    unsigned int cursor;
    unsigned int flags;
    char path[128];
} fd_table_t;

static fd_table_t g_fds[MAX_FDS];
static const char* g_pipe_input;
static unsigned int g_pipe_input_len;
static unsigned int g_pipe_input_pos;
static char* g_pipe_output;
static unsigned int g_pipe_output_capacity;
static unsigned int g_pipe_output_len;
static int g_pipe_active;

#define DEFERRED_CONSOLE_CAPACITY 4096
typedef struct {
    char value;
    unsigned char is_error;
} deferred_console_char_t;
static deferred_console_char_t g_deferred_console[DEFERRED_CONSOLE_CAPACITY];
static unsigned int g_deferred_console_length;
static int g_keyboard_event_extended;

// Kernel-owned physical-key event adapter for applications that need held-key
// state. Keep this separate from the keyboard module's shell character API so
// translated characters and module ABI details cannot consume or alter events.
static int syscall_try_key_event(void) {
    int scancode;
    while ((scancode = keyboard_irq_read_scancode()) >= 0) {
        unsigned char raw = (unsigned char)scancode;
        if (raw == 0xE0) {
            g_keyboard_event_extended = 1;
            continue;
        }
        if (raw == 0xE1) {
            g_keyboard_event_extended = 0;
            continue;
        }

        unsigned int key_id = (g_keyboard_event_extended ? 128u : 0u) +
                              (raw & 0x7Fu) + 1u;
        g_keyboard_event_extended = 0;
        if (raw & 0x80u) return (int)(0x100u + key_id);
        return (int)key_id;
    }
    return -1;
}

static int syscall_graphics_mode_active(void) {
    cyk_display_driver_t* dpy = modload_get_display_driver();
    return dpy && dpy->get_mode && dpy->get_mode() != 0;
}

static void syscall_defer_console_write(const char* data, unsigned int length,
                                        int is_error) {
    for (unsigned int i = 0; i < length &&
         g_deferred_console_length < DEFERRED_CONSOLE_CAPACITY; i++) {
        deferred_console_char_t* item = &g_deferred_console[g_deferred_console_length++];
        item->value = data[i];
        item->is_error = is_error ? 1 : 0;
    }
}

void syscall_flush_deferred_console(void) {
    if (syscall_graphics_mode_active()) return;
    unsigned int length = g_deferred_console_length;
    g_deferred_console_length = 0;
    for (unsigned int i = 0; i < length; i++) {
        deferred_console_char_t* item = &g_deferred_console[i];
        if (item->is_error) {
            char text[2] = { item->value, '\0' };
            print_color_string(text, VGA_RED);
        } else {
            print_char(item->value);
        }
    }
}

static int syscall_absolute_path(const char* path, char output[128]) {
    if (!path || !path[0]) return -1;
    unsigned int length = strlen(path);
    if (path[0] == '/' || path[0] == '\\') {
        if (length >= 128) return -1;
        memcpy(output, path, length + 1);
        return 0;
    }

    char cwd[128];
    int cwd_length = vfs_getcwd(cwd, sizeof(cwd));
    if (cwd_length < 0) return -1;
    unsigned int separator = cwd_length > 1 ? 1 : 0;
    if ((unsigned int)cwd_length + separator + length >= 128) return -1;
    memcpy(output, cwd, (unsigned int)cwd_length);
    unsigned int offset = (unsigned int)cwd_length;
    if (separator) output[offset++] = '/';
    memcpy(output + offset, path, length + 1);
    return 0;
}

void syscall_pipe_begin(const char* input, unsigned int input_len, char* output, unsigned int output_capacity) {
    g_pipe_input = input;
    g_pipe_input_len = input_len;
    g_pipe_input_pos = 0;
    g_pipe_output = output;
    g_pipe_output_capacity = output_capacity;
    g_pipe_output_len = 0;
    g_pipe_active = 1;
}

unsigned int syscall_pipe_end(void) {
    unsigned int output_len = g_pipe_output_len;
    g_pipe_input = 0;
    g_pipe_input_len = 0;
    g_pipe_input_pos = 0;
    g_pipe_output = 0;
    g_pipe_output_capacity = 0;
    g_pipe_output_len = 0;
    g_pipe_active = 0;
    return output_len;
}

unsigned int syscall_pipe_write(const char* data, unsigned int length) {
    if (!g_pipe_active || !g_pipe_output || !data) return 0;
    unsigned int available = g_pipe_output_capacity - g_pipe_output_len;
    unsigned int written = length < available ? length : available;
    memcpy(g_pipe_output + g_pipe_output_len, data, written);
    g_pipe_output_len += written;
    return written;
}

int syscall_pipe_read(char* data, unsigned int length) {
    if (!g_pipe_active || !g_pipe_input || !data) return -1;
    unsigned int available = g_pipe_input_len - g_pipe_input_pos;
    unsigned int bytes = length < available ? length : available;
    memcpy(data, g_pipe_input + g_pipe_input_pos, bytes);
    g_pipe_input_pos += bytes;
    return (int)bytes;
}

int syscall_pipe_active(void) {
    return g_pipe_active;
}

int syscall_pipe_output_active(void) {
    return g_pipe_active && g_pipe_output != 0;
}

static void syscall_handler(registers_t* regs) {
    unsigned int syscall_num = regs->eax;
    unsigned int arg1 = regs->ebx;
    unsigned int arg2 = regs->ecx;
    unsigned int arg3 = regs->edx;
    unsigned int arg4 = regs->esi;
    (void)arg4;

    switch (syscall_num) {
        case SYS_EXIT: {
            // arg1 = exit code
            // Return exit code in eax
            regs->eax = arg1;
            if (usercmd_is_running()) {
                usercmd_terminate((int)arg1);
            }
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
                if (g_pipe_active && g_pipe_output) {
                    regs->eax = syscall_pipe_write(buf, (unsigned int)count);
                } else if (syscall_graphics_mode_active()) {
                    syscall_defer_console_write(buf, (unsigned int)count, 0);
                    regs->eax = (unsigned int)count;
                } else {
                    for (size_t i = 0; i < count; i++) {
                        print_char(buf[i]);
                    }
                    regs->eax = (unsigned int)count;
                }
            } else if (fd == FD_STDERR) {
                if (syscall_graphics_mode_active()) {
                    syscall_defer_console_write(buf, (unsigned int)count, 1);
                } else {
                    for (size_t i = 0; i < count; i++) {
                        char temp[2] = { buf[i], '\0' };
                        print_color_string(temp, VGA_RED);
                    }
                }
                regs->eax = (unsigned int)count;
            } else if (fd >= 3 && fd < MAX_FDS && g_fds[fd].in_use &&
                (g_fds[fd].flags & OPEN_ACCMODE) != 0) {
                fd_table_t* file = &g_fds[fd];
                if (permissions_check(file->path, CYK_PERMISSION_WRITE) != 0) {
                    regs->eax = (unsigned int)-1;
                    break;
                }
                if (file->flags & OPEN_APPEND) file->cursor = file->entry.size;
                if (arg3 > SYS_FILE_IO_LIMIT || file->cursor > SYS_FILE_IO_LIMIT - arg3) {
                    regs->eax = (unsigned int)-1;
                    break;
                }

                unsigned int new_size = file->entry.size;
                if (new_size < file->cursor + arg3) new_size = file->cursor + arg3;
                if (new_size > SYS_FILE_IO_LIMIT) {
                    regs->eax = (unsigned int)-1;
                    break;
                }
                unsigned char* contents = (unsigned char*)malloc(new_size ? new_size : 1);
                if (!contents) {
                    regs->eax = (unsigned int)-1;
                    break;
                }
                if (file->entry.size && vfs_read_at(&file->entry, 0, contents, file->entry.size) != (int)file->entry.size) {
                    free(contents);
                    regs->eax = (unsigned int)-1;
                    break;
                }
                if (new_size > file->entry.size) {
                    memset(contents + file->entry.size, 0, new_size - file->entry.size);
                }
                memcpy(contents + file->cursor, buf, arg3);
                int result = vfs_write_file(file->path, contents, new_size);
                free(contents);
                if (result == 0 && vfs_find_file(file->path, &file->entry) == 0) {
                    file->cursor += arg3;
                    regs->eax = arg3;
                } else {
                    regs->eax = (unsigned int)-1;
                }
            } else {
                regs->eax = (unsigned int)-1;
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
                if (g_pipe_active && g_pipe_input) {
                    int bytes = syscall_pipe_read(buf, (unsigned int)count);
                    regs->eax = (unsigned int)(bytes >= 0 ? bytes : -1);
                } else {
                    size_t read_bytes = 0;
                    asm volatile("sti" ::: "memory");
                    while (read_bytes < count) {
                        char c = get_char();
                        if (c == 0x03) { // Ctrl+C
                            print_string("^C\n");
                            if (usercmd_is_running()) {
                                usercmd_terminate(130);
                            }
                            regs->eax = (unsigned int)-1;
                            break;
                        }
                        buf[read_bytes++] = c;
                        print_char(c); // Echo character
                        if (c == '\n' || c == '\r') {
                            break;
                        }
                    }
                    asm volatile("cli" ::: "memory");
                    if (regs->eax != (unsigned int)-1) {
                        regs->eax = (unsigned int)read_bytes;
                    }
                }
            } else if (fd >= 3 && fd < MAX_FDS && g_fds[fd].in_use &&
                (g_fds[fd].flags & OPEN_ACCMODE) != OPEN_WRONLY) {
                int bytes = vfs_read_at(&g_fds[fd].entry, g_fds[fd].cursor, buf, count);
                if (bytes > 0) g_fds[fd].cursor += (unsigned int)bytes;
                regs->eax = (unsigned int)(bytes >= 0 ? bytes : -1);
            } else {
                regs->eax = (unsigned int)-1;
            }
            break;
        }

        case SYS_OPEN: {
            // Linux i386 open(path, flags, mode); this VFS currently supports read-only opens.
            const char* filename = (const char*)arg1;
            unsigned int flags = arg2;
            unsigned int supported_flags = OPEN_ACCMODE | OPEN_CREAT | OPEN_EXCL |
                OPEN_TRUNC | OPEN_APPEND | OPEN_LARGEFILE;
            char absolute_path[128];
            if (!filename || (flags & OPEN_ACCMODE) == 3 || (flags & ~supported_flags) != 0 ||
                syscall_absolute_path(filename, absolute_path) != 0) {
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

            int found = vfs_find_file(absolute_path, &g_fds[found_fd].entry) == 0;
            if (!found && (flags & OPEN_CREAT)) {
                if (vfs_write_file(absolute_path, 0, 0) != 0 ||
                    vfs_find_file(absolute_path, &g_fds[found_fd].entry) != 0) {
                    regs->eax = (unsigned int)-1;
                    break;
                }
                (void)permissions_create(absolute_path, arg3, 0);
                found = 1;
            } else if (found && (flags & OPEN_CREAT) && (flags & OPEN_EXCL)) {
                regs->eax = (unsigned int)-1;
                break;
            }
            if (found && (g_fds[found_fd].entry.attributes & 0x10)) found = 0;
            if (found && permissions_check(absolute_path,
                (flags & OPEN_ACCMODE) == OPEN_RDONLY ? CYK_PERMISSION_READ :
                (flags & OPEN_ACCMODE) == OPEN_WRONLY ? CYK_PERMISSION_WRITE :
                CYK_PERMISSION_READ | CYK_PERMISSION_WRITE) != 0) found = 0;
            if (found && (flags & OPEN_TRUNC)) {
                if ((flags & OPEN_ACCMODE) == 0 || vfs_write_file(absolute_path, 0, 0) != 0 ||
                    vfs_find_file(absolute_path, &g_fds[found_fd].entry) != 0) found = 0;
            }
            if (found) {
                g_fds[found_fd].in_use = 1;
                g_fds[found_fd].flags = flags;
                g_fds[found_fd].cursor = (flags & OPEN_APPEND) ? g_fds[found_fd].entry.size : 0;
                unsigned int path_length = strlen(absolute_path);
                memcpy(g_fds[found_fd].path, absolute_path, path_length);
                g_fds[found_fd].path[path_length] = '\0';
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
                g_fds[fd].flags = 0;
                g_fds[fd].path[0] = '\0';
                regs->eax = 0;
            } else {
                regs->eax = (unsigned int)-1;
            }
            break;
        }

        case SYS_BRK: {
            unsigned int current = (unsigned int)sbrk(0);
            unsigned int requested = arg1;
            if (requested >= HEAP_START_ADDR && requested <= heap_get_max_addr()) {
                int difference = (int)(requested - current);
                if (sbrk(difference) != (void*)-1) current = (unsigned int)sbrk(0);
            }
            regs->eax = current;
            break;
        }

        case SYS_SBRK: {
            void* res = sbrk((int)arg1);
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
            asm volatile("sti" ::: "memory");
            char c = get_char();
            asm volatile("cli" ::: "memory");
            if (c == 0x03) { // Ctrl+C
                print_string("^C\n");
                if (usercmd_is_running()) {
                    usercmd_terminate(130);
                }
            }
            regs->eax = (unsigned char)c;
            break;
        }

        case SYS_HAS_KEY: {
            regs->eax = (unsigned int)has_key();
            break;
        }

        case SYS_TRY_GET_KEY: {
            cyk_keyboard_driver_t* kbd = modload_get_keyboard_driver();
            regs->eax = (kbd && kbd->try_get_char)
                ? (unsigned int)kbd->try_get_char()
                : (unsigned int)-1;
            break;
        }

        case SYS_TRY_KEY_EVENT: {
            regs->eax = (unsigned int)syscall_try_key_event();
            break;
        }

        case SYS_SLEEP: {
            asm volatile("sti" ::: "memory");
            secwait(arg1);
            asm volatile("cli" ::: "memory");
            regs->eax = 0;
            break;
        }

        case SYS_MSLEEP: {
            asm volatile("sti" ::: "memory");
            msecwait(arg1);
            asm volatile("cli" ::: "memory");
            regs->eax = 0;
            break;
        }

        case SYS_FAT_READ: {
            // arg1 = filename, arg2 = buffer, arg3 = max_bytes
            char absolute_path[128];
            int res = syscall_absolute_path((const char*)arg1, absolute_path) == 0 &&
                permissions_check(absolute_path, CYK_PERMISSION_READ) == 0
                ? vfs_read_file_by_name(absolute_path, (void*)arg2, arg3) : -1;
            regs->eax = (unsigned int)res;
            break;
        }

        case SYS_FAT_WRITE: {
            // arg1 = filename, arg2 = data, arg3 = size
            char absolute_path[128];
            int res = syscall_absolute_path((const char*)arg1, absolute_path) == 0 &&
                (permissions_check(absolute_path, CYK_PERMISSION_WRITE) == 0 ||
                 vfs_find_file(absolute_path, &(vfs_file_t){0}) != 0)
                ? vfs_write_file(absolute_path, (const void*)arg2, arg3) : -1;
            regs->eax = (unsigned int)res;
            break;
        }

        case SYS_FAT_DELETE: {
            // arg1 = filename
            char absolute_path[128];
            int res = syscall_absolute_path((const char*)arg1, absolute_path) == 0 &&
                permissions_check(absolute_path, CYK_PERMISSION_WRITE) == 0
                ? vfs_delete_file(absolute_path) : -1;
            regs->eax = (unsigned int)res;
            break;
        }

        case SYS_MKDIR:
        case SYS_CYK_MKDIR: {
            // arg1 = path
            char absolute_path[128];
            int res = syscall_absolute_path((const char*)arg1, absolute_path) == 0
                ? vfs_mkdir(absolute_path) : -1;
            if (res == 0) (void)permissions_create(absolute_path, arg2, 1);
            regs->eax = (unsigned int)res;
            break;
        }

        case SYS_RMDIR:
        case SYS_CYK_RMDIR: {
            // arg1 = path
            int res = vfs_rmdir((const char*)arg1);
            regs->eax = (unsigned int)res;
            break;
        }

        case SYS_CHDIR:
        case SYS_CYK_CHDIR: {
            // arg1 = path
            char absolute_path[128];
            int res = syscall_absolute_path((const char*)arg1, absolute_path) == 0 &&
                permissions_check(absolute_path, CYK_PERMISSION_EXEC) == 0
                ? vfs_chdir(absolute_path) : -1;
            regs->eax = (unsigned int)res;
            break;
        }

        case SYS_CYK_GETCWD: {
            // arg1 = buffer, arg2 = max_bytes
            int res = vfs_getcwd((char*)arg1, (unsigned int)arg2);
            regs->eax = (unsigned int)res;
            break;
        }

        case SYS_GETCWD: {
            int res = vfs_getcwd((char*)arg1, arg2);
            regs->eax = res < 0 ? (unsigned int)-1 : arg1;
            break;
        }

        case SYS_UNLINK: {
            char absolute_path[128];
            regs->eax = (unsigned int)(syscall_absolute_path((const char*)arg1, absolute_path) == 0 &&
                permissions_check(absolute_path, CYK_PERMISSION_WRITE) == 0
                ? vfs_delete_file(absolute_path) : -1);
            break;
        }

        case SYS_STAT: {
            char absolute_path[128];
            const char* path = absolute_path;
            unsigned int* statbuf = (unsigned int*)arg2;
            if (!statbuf || syscall_absolute_path((const char*)arg1, absolute_path) != 0) {
                regs->eax = (unsigned int)-1;
                break;
            }
            vfs_file_t file;
            if (vfs_find_file(path, &file) != 0) {
                regs->eax = (unsigned int)-1;
                break;
            }
            memset(statbuf, 0, 13 * sizeof(unsigned int));
            unsigned int mode = 0, uid = 0, gid = 0;
            if (permissions_get(path, &mode, &uid, &gid) != 0) {
                regs->eax = (unsigned int)-1;
                break;
            }
            statbuf[1] = file.first_cluster;
            statbuf[2] = mode;
            statbuf[3] = 1;
            statbuf[4] = uid;
            statbuf[5] = gid;
            statbuf[7] = file.size;
            statbuf[8] = 512;
            statbuf[9] = (file.size + 511) / 512;
            regs->eax = 0;
            break;
        }

        case SYS_CHMOD:
        case SYS_CHOWN:
        case SYS_ACCESS: {
            char absolute_path[128];
            if (syscall_absolute_path((const char*)arg1, absolute_path) != 0) {
                regs->eax = (unsigned int)-1;
                break;
            }
            if (syscall_num == SYS_CHMOD) regs->eax = (unsigned int)permissions_chmod(absolute_path, arg2);
            else if (syscall_num == SYS_CHOWN) regs->eax = (unsigned int)permissions_chown(absolute_path, arg2, arg3);
            else {
                unsigned int requested = 0;
                if (arg2 & 4) requested |= CYK_PERMISSION_READ;
                if (arg2 & 2) requested |= CYK_PERMISSION_WRITE;
                if (arg2 & 1) requested |= CYK_PERMISSION_EXEC;
                regs->eax = (unsigned int)permissions_check(absolute_path, requested);
            }
            break;
        }

        case SYS_GETUID: regs->eax = permissions_uid(); break;
        case SYS_GETGID: regs->eax = permissions_gid(); break;
        case SYS_SETUID: regs->eax = (unsigned int)permissions_set_uid(arg1); break;
        case SYS_SETGID: regs->eax = (unsigned int)permissions_set_gid(arg1); break;

        case SYS_FSTAT: {
            int fd = (int)arg1;
            unsigned int* statbuf = (unsigned int*)arg2;
            if (!statbuf) {
                regs->eax = (unsigned int)-1;
                break;
            }
            if (fd >= 0 && fd <= 2) {
                memset(statbuf, 0, 13 * sizeof(unsigned int));
                statbuf[2] = 0020666; // S_IFCHR
                regs->eax = 0;
                break;
            }
            if (fd < 3 || fd >= MAX_FDS || !g_fds[fd].in_use) {
                regs->eax = (unsigned int)-1;
                break;
            }
            memset(statbuf, 0, 13 * sizeof(unsigned int));
            unsigned int mode = 0, uid = 0, gid = 0;
            if (permissions_get(g_fds[fd].path, &mode, &uid, &gid) != 0) {
                regs->eax = (unsigned int)-1;
                break;
            }
            statbuf[1] = g_fds[fd].entry.first_cluster;
            statbuf[2] = mode;
            statbuf[3] = 1;
            statbuf[4] = uid;
            statbuf[5] = gid;
            statbuf[7] = g_fds[fd].entry.size;
            statbuf[8] = 512;
            statbuf[9] = (g_fds[fd].entry.size + 511) / 512;
            regs->eax = 0;
            break;
        }

        case SYS_LSEEK: {
            int fd = (int)arg1;
            int offset = (int)arg2;
            unsigned int base;
            if (fd < 3 || fd >= MAX_FDS || !g_fds[fd].in_use) {
                regs->eax = (unsigned int)-1;
                break;
            }
            if ((int)arg3 == 0) base = 0;
            else if ((int)arg3 == 1) base = g_fds[fd].cursor;
            else if ((int)arg3 == 2) base = g_fds[fd].entry.size;
            else {
                regs->eax = (unsigned int)-1;
                break;
            }
            unsigned int magnitude = 0U - (unsigned int)offset;
            if (offset < 0 && magnitude > base) {
                regs->eax = (unsigned int)-1;
                break;
            }
            unsigned int next = offset < 0 ? base - magnitude : base + (unsigned int)offset;
            if (next < base && offset >= 0) {
                regs->eax = (unsigned int)-1;
                break;
            }
            g_fds[fd].cursor = next;
            regs->eax = next;
            break;
        }

        case SYS_SOUND_TONE: {
            cyk_sound_driver_t* snd = modload_get_sound_driver();
            if (snd && snd->tone) {
                snd->tone(arg1, arg2);
                regs->eax = 0;
            } else if (arg1 > 0 && arg2 > 0) {
                // Kernel fallback PC speaker driver
                unsigned int frequency = arg1;
                unsigned int milliseconds = arg2;
                if (frequency < 20u)  frequency = 20u;
                if (frequency > 20000u)  frequency = 20000u;
                unsigned int divisor = 1193182U / frequency;
                if (divisor == 0)       divisor = 1;
                if (divisor > 0xFFFF)   divisor = 0xFFFF;
                outb(0x43, 0xB6);
                outb(0x42, (unsigned char)(divisor & 0xFF));
                outb(0x42, (unsigned char)(divisor >> 8));
                outb(0x61, (unsigned char)(inb(0x61) | 0x03));
                msecwait(milliseconds);
                outb(0x61, (unsigned char)(inb(0x61) & 0xFC));
                regs->eax = 0;
            } else {
                regs->eax = (unsigned int)-1;
            }
            break;
        }

        case SYS_SOUND_CHIME: {
            cyk_sound_driver_t* snd = modload_get_sound_driver();
            if (snd && snd->chime) {
                snd->chime();
                regs->eax = 0;
            } else {
                // Kernel fallback chime: 784 Hz (180ms) then 988 Hz (240ms)
                unsigned int d1 = 1193182U / 784;
                outb(0x43, 0xB6);
                outb(0x42, (unsigned char)(d1 & 0xFF));
                outb(0x42, (unsigned char)(d1 >> 8));
                outb(0x61, (unsigned char)(inb(0x61) | 0x03));
                msecwait(180);
                outb(0x61, (unsigned char)(inb(0x61) & 0xFC));
                msecwait(60);
                unsigned int d2 = 1193182U / 988;
                outb(0x43, 0xB6);
                outb(0x42, (unsigned char)(d2 & 0xFF));
                outb(0x42, (unsigned char)(d2 >> 8));
                outb(0x61, (unsigned char)(inb(0x61) | 0x03));
                msecwait(240);
                outb(0x61, (unsigned char)(inb(0x61) & 0xFC));
                regs->eax = 0;
            }
            break;
        }

        case SYS_SOUND_SET_VOLUME: {
            cyk_sound_driver_t* snd = modload_get_sound_driver();
            if (snd && snd->set_volume) {
                regs->eax = (unsigned int)snd->set_volume(arg1);
            } else {
                regs->eax = (unsigned int)-1;
            }
            break;
        }

        case SYS_SOUND_PLAY_SAMPLE: {
            cyk_sound_driver_t* snd = modload_get_sound_driver();
            if (snd && snd->play_sample) {
                regs->eax = (unsigned int)snd->play_sample(
                    (const unsigned char*)arg1, arg2, regs->edx);
            } else {
                regs->eax = (unsigned int)-1;
            }
            break;
        }

        case SYS_SOUND_STOP: {
            cyk_sound_driver_t* snd = modload_get_sound_driver();
            if (snd && snd->stop) {
                regs->eax = (unsigned int)snd->stop();
            } else {
                // Kernel fallback: silence the PC speaker
                outb(0x61, (unsigned char)(inb(0x61) & 0xFC));
                regs->eax = 0;
            }
            break;
        }

        case SYS_SOUND_GET_CAPS: {
            cyk_sound_driver_t* snd = modload_get_sound_driver();
            if (snd && snd->get_caps) {
                regs->eax = (unsigned int)snd->get_caps();
            } else {
                regs->eax = (unsigned int)SOUND_CAP_NONE;
            }
            break;
        }

        case SYS_GFX_MODE: {
            cyk_display_driver_t* dpy = modload_get_display_driver();
            if (!dpy) {
                regs->eax = (unsigned int)-1;
            } else if ((int)arg1 == -1) {
                regs->eax = (unsigned int)dpy->get_mode();
            } else {
                regs->eax = (unsigned int)dpy->set_mode((int)arg1);
                if ((int)arg1 == 0 && (int)regs->eax == 0)
                    syscall_flush_deferred_console();
            }
            break;
        }

        case SYS_GFX_PUTPIXEL: {
            cyk_display_driver_t* dpy = modload_get_display_driver();
            if (dpy && dpy->put_pixel) {
                dpy->put_pixel((int)arg1, (int)arg2, (unsigned char)arg3);
                regs->eax = 0;
            } else {
                regs->eax = (unsigned int)-1;
            }
            break;
        }

        case SYS_GFX_RECT: {
            cyk_display_driver_t* dpy = modload_get_display_driver();
            if (dpy && dpy->fill_rect) {
                int x = (int)((short)(arg1 >> 16));
                int y = (int)((short)(arg1 & 0xFFFF));
                int w = (int)((short)(arg2 >> 16));
                int h = (int)((short)(arg2 & 0xFFFF));
                dpy->fill_rect(x, y, w, h, (unsigned char)arg3);
                regs->eax = 0;
            } else {
                regs->eax = (unsigned int)-1;
            }
            break;
        }

        case SYS_GFX_CLEAR: {
            cyk_display_driver_t* dpy = modload_get_display_driver();
            if (dpy && dpy->clear) {
                dpy->clear((unsigned char)arg1);
                regs->eax = 0;
            } else {
                regs->eax = (unsigned int)-1;
            }
            break;
        }

        case SYS_GFX_FRAMEBUF: {
            cyk_display_driver_t* dpy = modload_get_display_driver();
            if (dpy && dpy->get_framebuffer) {
                regs->eax = (unsigned int)dpy->get_framebuffer();
            } else {
                regs->eax = 0;
            }
            break;
        }

        case SYS_GFX_INFO: {
            cyk_display_driver_t* dpy = modload_get_display_driver();
            if (dpy && dpy->get_info && arg1) {
                regs->eax = (unsigned int)dpy->get_info((cyk_display_info_t*)arg1);
            } else {
                regs->eax = (unsigned int)-1;
            }
            break;
        }

        case SYS_GFX_BITMAP: {
            cyk_display_driver_t* dpy = modload_get_display_driver();
            if (dpy && dpy->draw_bitmap_ex && arg2) {
                int x = (int)(short)(arg1 >> 16);
                int y = (int)(short)(arg1 & 0xFFFF);
                regs->eax = (unsigned int)dpy->draw_bitmap_ex(x, y, (const cyk_bitmap_t*)arg2);
            } else {
                regs->eax = (unsigned int)-1;
            }
            break;
        }

        case SYS_GFX_PALETTE: {
            cyk_display_driver_t* dpy = modload_get_display_driver();
            unsigned int first = arg1 >> 16;
            unsigned int count = arg1 & 0xFFFF;
            if (dpy && dpy->set_palette && arg2 && count) {
                regs->eax = (unsigned int)dpy->set_palette(
                    first, count, (const unsigned char*)arg2);
            } else {
                regs->eax = (unsigned int)-1;
            }
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
