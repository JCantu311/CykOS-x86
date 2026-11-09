#include "usercmd.h"
#include "fat16.h"
#include "cykastd.h"

#define USERCMDS_FILE     "USERCMDS.LST"    // List (LST) of userland commands
#define USERCMDS_FALLBACK "USERCMDS.TXT"    // Fallback compatibility
#define USER_APP_BASE     0x50000   // Conventional user application memory base
#define USER_APP_MAX_SIZE 0x30000   // 192 KB maximum application binary size

// Internal helper to convert character to uppercase
static char uc(char c) {
    if (c >= 'a' && c <= 'z') return c - ('a' - 'A');
    return c;
}

// Case-insensitive null-terminated string comparison
static int str_casecmp(const char* a, const char* b) {
    while (*a && *b) {
        char ca = uc(*a);
        char cb = uc(*b);
        if (ca != cb) return (unsigned char)ca - (unsigned char)cb;
        a++; b++;
    }
    return (unsigned char)uc(*a) - (unsigned char)uc(*b);
}

// Skip whitespace and comment lines, extract next token into out_token
static const char* next_token(const char* p, char* out, int max) {
    // Skip whitespace
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;

    if (*p == '\0') return (void*)0;

    // Skip comment lines
    if (*p == '#') {
        while (*p != '\0' && *p != '\n') p++;
        return next_token(p, out, max);
    }

    int i = 0;
    while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && i < max - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return p;
}

// Read USERCMDS.TXT and search for an alias matching cmd_name.
// If found, writes the binary filename into out_bin and returns 1.
static int find_alias(const char* cmd_name, char* out_bin, int out_max) {
    fat16_entry_t cfg;
    if (fat16_find_file(USERCMDS_FILE, &cfg) != 0) {
        if (fat16_find_file(USERCMDS_FALLBACK, &cfg) != 0) {
            return 0; // Config file not present on disk
        }
    }

    char buf[1024];
    memset(buf, 0, sizeof(buf));
    int bytes = fat16_read_file(&cfg, buf, sizeof(buf) - 1);
    if (bytes <= 0) return 0;

    const char* cursor = buf;
    char alias[16];
    char binary[16];

    while ((cursor = next_token(cursor, alias, sizeof(alias))) != (void*)0) {
        cursor = next_token(cursor, binary, sizeof(binary));
        if (!cursor) break;

        if (str_casecmp(alias, cmd_name) == 0) {
            // Copy matched binary filename to output
            int i = 0;
            while (binary[i] != '\0' && i < out_max - 1) {
                out_bin[i] = binary[i];
                i++;
            }
            out_bin[i] = '\0';
            return 1;
        }
    }
    return 0;
}

int exec_user_command(const char* cmd_name, const char* args) {
    if (!cmd_name || cmd_name[0] == '\0') return 0;

    char target_file[16];
    target_file[0] = '\0';

    // Look up alias in USERCMDS.TXT
    if (!find_alias(cmd_name, target_file, sizeof(target_file))) {
        // Fallback: check if cmd_name is a direct file or has .BIN / .COM extension
        fat16_entry_t probe;
        if (fat16_find_file(cmd_name, &probe) == 0) {
            int i = 0;
            while (cmd_name[i] && i < (int)sizeof(target_file) - 1) {
                target_file[i] = cmd_name[i];
                i++;
            }
            target_file[i] = '\0';
        } else {
            char name_buf[16];
            int i = 0;
            while (cmd_name[i] && cmd_name[i] != '.' && i < 8) {
                name_buf[i] = cmd_name[i];
                i++;
            }
            name_buf[i] = '\0';

            // Try <cmd>.BIN
            char test_bin[16];
            int j = 0;
            while (name_buf[j]) { test_bin[j] = name_buf[j]; j++; }
            test_bin[j++] = '.';
            test_bin[j++] = 'B';
            test_bin[j++] = 'I';
            test_bin[j++] = 'N';
            test_bin[j] = '\0';

            if (fat16_find_file(test_bin, &probe) == 0) {
                for (int k = 0; k <= j; k++) target_file[k] = test_bin[k];
            } else {
                // Try <cmd>.COM
                test_bin[j - 3] = 'C';
                test_bin[j - 2] = 'O';
                test_bin[j - 1] = 'M';
                if (fat16_find_file(test_bin, &probe) == 0) {
                    for (int k = 0; k <= j; k++) target_file[k] = test_bin[k];
                } else {
                    return 0; // Not an external command
                }
            }
        }
    }

    // Locate the binary on FAT16
    fat16_entry_t app_entry;
    if (fat16_find_file(target_file, &app_entry) != 0) {
        print_color_string("CykShell: executable not found: ", VGA_RED);
        print_string(target_file);
        print_string("\n");
        return 1; // Handled (error shown)
    }

    // Check if executable file is empty
    if (app_entry.file_size == 0) {
        print_color_string("CykShell: executable is empty (0 bytes): ", VGA_RED);
        print_string(target_file);
        print_string("\n");
        return 1;
    }

    // Load binary into application memory
    void* dest = (void*)USER_APP_BASE;

    // Zero out the app memory region first (clean BSS, prevent stale data)
    memset(dest, 0, USER_APP_MAX_SIZE);

    int bytes_read = fat16_read_file(&app_entry, dest, USER_APP_MAX_SIZE);
    if (bytes_read <= 0) {
        print_color_string("CykShell: failed to load: ", VGA_RED);
        print_string(target_file);
        print_string("\n");
        return 1;
    }

    // Check for ELF header (common mistake: binary is ELF not flat)
    unsigned char* dbg = (unsigned char*)dest;
    if (dbg[0] == 0x7F && dbg[1] == 'E' && dbg[2] == 'L' && dbg[3] == 'F') {
        print_color_string("CykShell: executable is ELF format, flat binary required: ", VGA_RED);
        print_string(target_file);
        print_string("\n");
        return 1;
    }

    // Tokenize arguments into standard (int argc, char** argv)
    #define MAX_APP_ARGS 16
    static char s_arg_buf[256];
    static char* s_argv[MAX_APP_ARGS + 1];
    int argc = 0;

    // argv[0] is the command name as invoked
    s_argv[argc++] = (char*)cmd_name;

    if (args && args[0] != '\0') {
        int i = 0;
        while (args[i] != '\0' && i < (int)sizeof(s_arg_buf) - 1) {
            s_arg_buf[i] = args[i];
            i++;
        }
        s_arg_buf[i] = '\0';

        char* p = s_arg_buf;
        while (*p != '\0' && argc < MAX_APP_ARGS) {
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
                *p = '\0';
                p++;
            }
            if (*p == '\0') break;

            if (*p == '"' || *p == '\'') {
                char quote = *p++;
                s_argv[argc++] = p;
                while (*p != '\0' && *p != quote) p++;
                if (*p != '\0') {
                    *p = '\0';
                    p++;
                }
            } else {
                s_argv[argc++] = p;
                while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
                    p++;
                }
                if (*p != '\0') {
                    *p = '\0';
                    p++;
                }
            }
        }
    }
    s_argv[argc] = (void*)0; // NULL termination

    print_string("[CykShell] Launching ");
    print_string(target_file);
    print_string("...\n");

    // Execute as standard 32-bit function: int main(int argc, char** argv)
    typedef int (*app_main_fn)(int, char**);
    app_main_fn entry = (app_main_fn)dest;
    entry(argc, s_argv);

    return 1;
}

void list_user_commands(void) {
    fat16_entry_t cfg;
    const char* filename = USERCMDS_FILE;
    if (fat16_find_file(USERCMDS_FILE, &cfg) != 0) {
        if (fat16_find_file(USERCMDS_FALLBACK, &cfg) != 0) {
            return; // No config present
        }
        filename = USERCMDS_FALLBACK;
    }

    char buf[1024];
    memset(buf, 0, sizeof(buf));
    int bytes = fat16_read_file(&cfg, buf, sizeof(buf) - 1);
    if (bytes <= 0) return;

    print_string("\nExternal Commands (");
    print_string(filename);
    print_string("):\n");

    const char* cursor = buf;
    char alias[16];
    char binary[16];

    while ((cursor = next_token(cursor, alias, sizeof(alias))) != (void*)0) {
        cursor = next_token(cursor, binary, sizeof(binary));
        if (!cursor) break;

        print_string("  ");
        print_string(alias);
        int pad = (int)strlen(alias);
        while (pad < 12) {
            print_char(' ');
            pad++;
        }
        print_string("- ");
        print_string(binary);
        print_string("\n");
    }
}
