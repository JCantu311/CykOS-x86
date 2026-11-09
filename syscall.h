#ifndef SYSCALL_H
#define SYSCALL_H

#include "idt.h"

// CykOS System Call Numbers (Linux-compatible where applicable)
#define SYS_EXIT          1   // Terminate current process / return to shell
#define SYS_READ          3   // Read from file descriptor or stdin (keyboard)
#define SYS_WRITE         4   // Write to file descriptor or stdout (console)
#define SYS_OPEN          5   // Open file on FAT16
#define SYS_CLOSE         6   // Close file descriptor
#define SYS_BRK           45  // Dynamic memory expansion (heap management)
#define SYS_CLEAR         100 // Clear screen
#define SYS_SET_CURSOR    101 // Position text cursor (col, row)
#define SYS_GET_KEY       102 // Read single character (blocking)
#define SYS_HAS_KEY       103 // Poll if key is pressed
#define SYS_SLEEP         104 // Sleep for specified seconds
#define SYS_FAT_READ      105 // Convenience read file by name
#define SYS_FAT_WRITE     106 // Convenience write file by name
#define SYS_FAT_DELETE    107 // Delete file by name
#define SYS_MKDIR         108 // Create directory (path)
#define SYS_RMDIR         109 // Remove directory (path)
#define SYS_CHDIR         110 // Change directory (path)
#define SYS_GETCWD        111 // Get current working directory (buf, max)

// File descriptor constants
#define FD_STDIN          0
#define FD_STDOUT         1
#define FD_STDERR         2

void syscall_init(void);

#endif // SYSCALL_H
