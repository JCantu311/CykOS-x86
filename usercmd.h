#ifndef USERCMD_H
#define USERCMD_H

/**
 * Searches USERCMDS.TXT on the FAT16 volume for a command alias mapping.
 * Format of USERCMDS.TXT:  one line per alias = "<alias> <binary_filename>"
 *   e.g.  "hello HELLO.COM"
 * Lines starting with '#' are comments.
 *
 * If the alias is found, loads the binary from disk into application RAM
 * (0x50000) and executes it as a flat 32-bit function.
 *
 * @param cmd_name  Null-terminated command name the user typed
 * @param args      Null-terminated argument string (may be empty)
 * @return          1 if the command was found and handled, 0 if not found
 */
int exec_user_command(const char* cmd_name, const char* args);

/**
 * Prints all aliases currently defined in USERCMDS.TXT (called by 'help').
 */
void list_user_commands(void);

#endif // USERCMD_H
