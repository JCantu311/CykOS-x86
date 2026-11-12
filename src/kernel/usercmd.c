#include "usercmd.h"
#include "vfs.h"
#include "cykastd.h"
#include "syscall.h"

#define USERCMDS_FILE     "/ETC/USERCMDS.LST"
#define USERCMDS_FALLBACK "/ETC/USERCMDS.TXT"
#define USERCMDS_LEGACY   "/CYKOS/USERCMDS.LST"
#define USERCMDS_OLD_FALLBACK "/CYKOS/USERCMDS.TXT"
#define USER_BIN_DIR      "/USR/BIN/"
#define LEGACY_BIN_DIR    "/CYKOS/"
#define USER_APP_BASE     0x50000   // Conventional user application memory base
#define USER_APP_MAX_SIZE 0x30000   // 192 KB maximum application binary size
#define USER_PATH_MAX     64

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

static int build_path(const char* directory, const char* filename, char* output, int capacity) {
    int index = 0;
    while (directory[index] && index < capacity - 1) {
        output[index] = directory[index];
        index++;
    }
    int source_index = 0;
    while (filename[source_index] && index < capacity - 1) {
        output[index++] = filename[source_index++];
    }
    output[index] = '\0';
    return filename[source_index] == '\0' ? 0 : -1;
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

// Read the external command list and search for an alias matching cmd_name.
// If found, writes the binary filename into out_bin and returns 1.
static int find_alias(const char* cmd_name, char* out_bin, int out_max) {
    vfs_file_t cfg;
    if (vfs_find_file(USERCMDS_FILE, &cfg) != 0) {
        if (vfs_find_file(USERCMDS_FALLBACK, &cfg) != 0 &&
            vfs_find_file(USERCMDS_LEGACY, &cfg) != 0 &&
            vfs_find_file(USERCMDS_OLD_FALLBACK, &cfg) != 0) {
            return 0;
        }
    }

    char buf[1024];
    memset(buf, 0, sizeof(buf));
    int bytes = vfs_read_at(&cfg, 0, buf, sizeof(buf) - 1);
    if (bytes <= 0) return 0;

    const char* cursor = buf;
    char alias[32];
    char binary[USER_PATH_MAX];

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

static void shell_output_string(const char* text) {
    if (syscall_pipe_output_active()) {
        syscall_pipe_write(text, strlen(text));
    } else {
        print_string(text);
    }
}

static void shell_output_char(char ch) {
    if (syscall_pipe_output_active()) {
        syscall_pipe_write(&ch, 1);
    } else {
        print_char(ch);
    }
}

int exec_user_command(const char* cmd_name, const char* args) {
    if (!cmd_name || cmd_name[0] == '\0') return 0;

    char target_file[USER_PATH_MAX];
    target_file[0] = '\0';

    // Look up alias in USERCMDS.TXT
    if (!find_alias(cmd_name, target_file, sizeof(target_file))) {
        // Fallback: check if cmd_name is a direct file or has .BIN / .COM extension
        vfs_file_t probe;
        if (vfs_find_file(cmd_name, &probe) == 0) {
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

            char app_target[USER_PATH_MAX];
            char legacy_target[USER_PATH_MAX];
            build_path(USER_BIN_DIR, test_bin, app_target, sizeof(app_target));
            build_path(LEGACY_BIN_DIR, test_bin, legacy_target, sizeof(legacy_target));

            if (vfs_find_file(test_bin, &probe) == 0) {
                for (int k = 0; k <= j; k++) target_file[k] = test_bin[k];
            } else if (vfs_find_file(app_target, &probe) == 0) {
                memcpy(target_file, app_target, strlen(app_target) + 1);
            } else if (vfs_find_file(legacy_target, &probe) == 0) {
                memcpy(target_file, legacy_target, strlen(legacy_target) + 1);
            } else {
                // Try <cmd>.COM
                test_bin[j - 3] = 'C';
                test_bin[j - 2] = 'O';
                test_bin[j - 1] = 'M';
                if (vfs_find_file(test_bin, &probe) == 0) {
                    for (int k = 0; k <= j; k++) target_file[k] = test_bin[k];
                } else {
                    build_path(USER_BIN_DIR, test_bin, app_target, sizeof(app_target));
                    build_path(LEGACY_BIN_DIR, test_bin, legacy_target, sizeof(legacy_target));
                    if (vfs_find_file(app_target, &probe) == 0) {
                        memcpy(target_file, app_target, strlen(app_target) + 1);
                    } else if (vfs_find_file(legacy_target, &probe) == 0) {
                        memcpy(target_file, legacy_target, strlen(legacy_target) + 1);
                    } else {
                        return 0;
                    }
                }
            }
        }
    }

    // Locate the binary through the mounted filesystem.
    vfs_file_t app_entry;
    if (vfs_find_file(target_file, &app_entry) != 0) {
        // If not found in current directory and not an absolute path, check root directory
        int found_in_root = 0;
        if (target_file[0] != '/' && target_file[0] != '\\') {
            char root_target[USER_PATH_MAX];
            root_target[0] = '/';
            int t = 0;
            while (target_file[t] && t < (int)sizeof(root_target) - 2) {
                root_target[t + 1] = target_file[t];
                t++;
            }
            root_target[t + 1] = '\0';
            if (vfs_find_file(root_target, &app_entry) == 0) {
                found_in_root = 1;
            }
        }
        if (!found_in_root) {
            print_color_string("CykShell: executable not found: ", VGA_RED);
            print_string(target_file);
            print_string("\n");
            return 1; // Handled (error shown)
        }
    }

    // Check if executable file is empty
    if (app_entry.size == 0) {
        print_color_string("CykShell: executable is empty (0 bytes): ", VGA_RED);
        print_string(target_file);
        print_string("\n");
        return 1;
    }

    // Load binary into application memory
    void* dest = (void*)USER_APP_BASE;

    // Zero out the app memory region first (clean BSS, prevent stale data)
    memset(dest, 0, USER_APP_MAX_SIZE);

    int bytes_read = vfs_read_at(&app_entry, 0, dest, USER_APP_MAX_SIZE);
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

    if (!syscall_pipe_active()) {
        print_string("[CykShell] Launching ");
        print_string(target_file);
        print_string("...\n");
    }

    // Execute as standard 32-bit function: int main(int argc, char** argv)
    typedef int (*app_main_fn)(int, char**);
    app_main_fn entry = (app_main_fn)dest;
    entry(argc, s_argv);

    return 1;
}

int exec_shell_binary(void) {
    vfs_file_t shell_entry;
    const char* shell_path = "/BIN/SHELL.BIN";
    if (vfs_find_file(shell_path, &shell_entry) != 0) {
        shell_path = "/CYKOS/SHELL.BIN";
        if (vfs_find_file(shell_path, &shell_entry) != 0) return 0;
    }
    if (shell_entry.size == 0 || shell_entry.size > USER_APP_MAX_SIZE) {
        print_color_string("CykShell: invalid shell image size\n", VGA_RED);
        return -1;
    }

    void* shell_base = (void*)0x100000;
    memset(shell_base, 0, USER_APP_MAX_SIZE);
    int bytes_read = vfs_read_at(&shell_entry, 0, shell_base, USER_APP_MAX_SIZE);
    if (bytes_read <= 0) {
        print_color_string("CykShell: failed to load standalone shell image\n", VGA_RED);
        return -1;
    }

    unsigned char* image = (unsigned char*)shell_base;
    if (image[0] == 0x7F && image[1] == 'E' && image[2] == 'L' && image[3] == 'F') {
        print_color_string("CykShell: shell image must be a flat binary\n", VGA_RED);
        return -1;
    }

    typedef int (*shell_main_fn)(int, char**);
    ((shell_main_fn)shell_base)(0, (char**)0);
    return 1;
}

void list_user_commands(void) {
    vfs_file_t cfg;
    const char* filename = USERCMDS_FILE;
    if (vfs_find_file(USERCMDS_FILE, &cfg) != 0) {
        if (vfs_find_file(USERCMDS_FALLBACK, &cfg) != 0) {
            filename = USERCMDS_LEGACY;
            if (vfs_find_file(filename, &cfg) != 0) {
                filename = USERCMDS_OLD_FALLBACK;
                if (vfs_find_file(filename, &cfg) != 0) return;
            }
        } else {
            filename = USERCMDS_FALLBACK;
        }
    }

    char buf[1024];
    memset(buf, 0, sizeof(buf));
    int bytes = vfs_read_at(&cfg, 0, buf, sizeof(buf) - 1);
    if (bytes <= 0) return;

    shell_output_string("\nExternal Commands (");
    shell_output_string(filename);
    shell_output_string("):\n");

    const char* cursor = buf;
    char alias[32];
    char binary[USER_PATH_MAX];

    while ((cursor = next_token(cursor, alias, sizeof(alias))) != (void*)0) {
        cursor = next_token(cursor, binary, sizeof(binary));
        if (!cursor) break;

        shell_output_string("  ");
        shell_output_string(alias);
        int pad = (int)strlen(alias);
        while (pad < 12) {
            shell_output_char(' ');
            pad++;
        }
        shell_output_string("- ");
        shell_output_string(binary);
        shell_output_string("\n");
    }
}
