#include "usercmd.h"
#include "fat16.h"
#include "cykastd.h"

#define USERCMDS_FILE     "USERCMDS.TXT"
#define USER_APP_BASE     0x50000   // Conventional user application memory base
#define USER_APP_MAX_SIZE 0x10000   // 64 KB maximum application binary size

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
        return 0; // File not present on disk
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
        return 0; // Not an external command
    }

    // Locate the binary on FAT16
    fat16_entry_t app_entry;
    if (fat16_find_file(target_file, &app_entry) != 0) {
        print_color_string("CykShell: executable not found: ", VGA_RED);
        print_string(target_file);
        print_string("\n");
        return 1; // Handled (error shown)
    }

    // Load binary into application memory
    void* dest = (void*)USER_APP_BASE;
    int bytes_read = fat16_read_file(&app_entry, dest, USER_APP_MAX_SIZE);
    if (bytes_read <= 0) {
        print_color_string("CykShell: failed to load: ", VGA_RED);
        print_string(target_file);
        print_string("\n");
        return 1;
    }

    // Execute as a flat 32-bit function: int main(const char* args)
    typedef int (*app_fn)(const char*);
    app_fn entry = (app_fn)dest;
    entry(args ? args : "");

    return 1;
}

void list_user_commands(void) {
    fat16_entry_t cfg;
    if (fat16_find_file(USERCMDS_FILE, &cfg) != 0) {
        return; // No USERCMDS.TXT present
    }

    char buf[1024];
    memset(buf, 0, sizeof(buf));
    int bytes = fat16_read_file(&cfg, buf, sizeof(buf) - 1);
    if (bytes <= 0) return;

    print_string("\nExternal Commands (USERCMDS.TXT):\n");

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
