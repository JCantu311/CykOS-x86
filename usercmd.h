#ifndef USERCMD_H
#define USERCMD_H

/**
 * Searches USERCMDS.LST on the FAT16 volume for a command alias mapping.
 * Format of USERCMDS.LST:  one line per alias = "<alias> <binary_filename>"
 *   e.g.  "edit EDIT.BIN"
 * Lines starting with '#' are comments.
 *
 * If the alias is found (or if an executable matching cmd_name exists directly
 * on disk, e.g. FOO.BIN), loads the binary from disk into application RAM
 * (0x50000) and executes it as a flat 32-bit function.
 *
 * @param cmd_name  Null-terminated command name the user typed
 * @param args      Null-terminated argument string (may be empty)
 * @return          1 if the command was found and handled, 0 if not found
 */
int exec_user_command(const char* cmd_name, const char* args);

/**
 * Prints all aliases currently defined in USERCMDS.LST (called by 'help').
 */
void list_user_commands(void);

#endif // USERCMD_H
