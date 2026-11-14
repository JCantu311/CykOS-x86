#ifndef SYSCALL_H
#define SYSCALL_H

#include "idt.h"

// CykOS System Call Numbers (Linux-compatible where applicable)
#define SYS_EXIT          1   // Terminate current process / return to shell
#define SYS_READ          3   // Read from file descriptor or stdin (keyboard)
#define SYS_WRITE         4   // Write to file descriptor or stdout (console)
#define SYS_OPEN          5   // Open file on FAT16
#define SYS_CLOSE         6   // Close file descriptor
#define SYS_UNLINK        10  // POSIX unlink(path)
#define SYS_CHDIR         12  // POSIX chdir(path)
#define SYS_LSEEK         19  // POSIX lseek(fd, offset, whence)
#define SYS_MKDIR         39  // POSIX mkdir(path, mode)
#define SYS_RMDIR         40  // POSIX rmdir(path)
#define SYS_BRK           45  // Dynamic memory expansion (heap management)
#define SYS_CLEAR         100 // Clear screen
#define SYS_SET_CURSOR    101 // Position text cursor (col, row)
#define SYS_GET_KEY       102 // Read single character (blocking)
#define SYS_HAS_KEY       103 // Poll if key is pressed
#define SYS_SLEEP         104 // Sleep for specified seconds
#define SYS_FAT_READ      105 // Convenience read file by name
#define SYS_FAT_WRITE     106 // Convenience write file by name
#define SYS_FAT_DELETE    107 // Delete file by name
#define SYS_STAT          108 // POSIX stat(path, statbuf)
#define SYS_FSTAT         109 // POSIX fstat(fd, statbuf)
#define SYS_CYK_MKDIR     118 // Legacy CykOS mkdir(path)
#define SYS_CYK_RMDIR     119 // Legacy CykOS rmdir(path)
#define SYS_CYK_CHDIR     120 // Legacy CykOS chdir(path)
#define SYS_CYK_GETCWD    121 // Legacy CykOS getcwd(buf, max), returns length
#define SYS_SBRK          112 // CykOS sbrk(increment) extension
#define SYS_GETCWD        183 // Linux i386 getcwd(buf, size), returns buf
#define SYS_SOUND_TONE    184 // CykOS sound_tone(freq, ms)
#define SYS_SOUND_CHIME   185 // CykOS sound_chime()
#define SYS_MSLEEP        186 // CykOS msecwait(ms)
#define SYS_GFX_MODE      187 // CykOS gfx set/get mode (arg1: -1 = get, 0 = text, 1 = 320x200 graphics)
#define SYS_GFX_PUTPIXEL  188 // CykOS gfx put pixel (arg1 = x, arg2 = y, arg3 = color)
#define SYS_GFX_RECT      189 // CykOS gfx fill rect (arg1 = packed (x<<16)|y, arg2 = packed (w<<16)|h, arg3 = color)
#define SYS_GFX_CLEAR     190 // CykOS gfx clear screen (arg1 = color)
#define SYS_GFX_FRAMEBUF  191 // CykOS gfx get framebuffer pointer (returns 0xA0000 in graphics mode)

// File descriptor constants
#define FD_STDIN          0
#define FD_STDOUT         1
#define FD_STDERR         2

void syscall_init(void);
void syscall_pipe_begin(const char* input, unsigned int input_len, char* output, unsigned int output_capacity);
unsigned int syscall_pipe_end(void);
unsigned int syscall_pipe_write(const char* data, unsigned int length);
int syscall_pipe_read(char* data, unsigned int length);
int syscall_pipe_active(void);
int syscall_pipe_output_active(void);

#endif // SYSCALL_H
