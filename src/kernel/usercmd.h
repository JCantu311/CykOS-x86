#ifndef USERCMD_H
#define USERCMD_H

#include "idt.h"

/**
 * Searches /ETC/USERCMDS.LST for a command alias mapping.
 * Format of USERCMDS.LST:  one line per alias = "<alias> <binary_filename>"
 *   e.g.  "edit /USR/BIN/EDIT.BIN"
 * Lines starting with '#' are comments.
 *
 * If the alias is found (or if an executable matching cmd_name exists directly
 * on disk, e.g. /USR/BIN/FOO.BIN), loads the binary from disk into application RAM
 * (0x180000) and executes it as a flat 32-bit function.
 *
 * @param cmd_name  Null-terminated command name the user typed
 * @param args      Null-terminated argument string (may be empty)
 * @return          1 if the command was found and handled, 0 if not found
 */
int exec_user_command(const char* cmd_name, const char* args);

/* Load and relocate an ELF32 ET_DYN kernel module into caller-owned RAM. */
int module_elf32_load(const unsigned char* file, unsigned int file_size,
                      void* load_area, unsigned int capacity);

/** Load and invoke the standalone Level 3 shell image from /BIN/SHELL.BIN. */
int exec_shell_binary(void);

/**
 * Prints all aliases currently defined in /ETC/USERCMDS.LST (called by 'help').
 */
void list_user_commands(void);

// Userland execution context helpers for SYS_EXIT and Ctrl+C abort
void usercmd_terminate(int exit_code);
__attribute__((noreturn)) void usercmd_abort_from_irq(void);
__attribute__((noreturn)) void usercmd_abort_from_exception(unsigned int vector,
                                                             unsigned int eip,
                                                             unsigned int error_code,
                                                             registers_t* regs);
int usercmd_is_running(void);
int usercmd_is_app_started(void);
void usercmd_request_cancel(void);
void usercmd_request_app_abort(void);
void usercmd_apply_pending_app_abort(registers_t* regs);
int usercmd_redirect_user_irq(registers_t* regs);
unsigned int usercmd_current_pid(void);
void* usercmd_app_sbrk(int increment);
unsigned int usercmd_app_brk(unsigned int requested);
/** Reap the most recently exited foreground app; returns its pid or -1. */
int usercmd_wait(int* exit_code);

#endif // USERCMD_H
