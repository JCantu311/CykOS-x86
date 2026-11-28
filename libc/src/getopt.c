#include <getopt.h>
#include <string.h>

char* optarg = (void*)0;
int optind = 1;
int opterr = 1;
int optopt = 0;

static int g_optpos = 1;

int getopt(int argc, char* const argv[], const char* optstring) {
    optarg = (void*)0;
    if (optind >= argc || !argv[optind] || argv[optind][0] != '-' || argv[optind][1] == '\0') {
        return -1;
    }
    if (strcmp(argv[optind], "--") == 0) {
        optind++;
        return -1;
    }

    char c = argv[optind][g_optpos];
    const char* p = strchr(optstring, c);
    if (!p || c == ':') {
        optopt = (unsigned char)c;
        g_optpos++;
        if (argv[optind][g_optpos] == '\0') { optind++; g_optpos = 1; }
        return '?';
    }

    if (p[1] == ':') {
        if (argv[optind][g_optpos + 1] != '\0') {
            optarg = &argv[optind][g_optpos + 1];
            optind++;
            g_optpos = 1;
        } else if (optind + 1 < argc) {
            optarg = argv[optind + 1];
            optind += 2;
            g_optpos = 1;
        } else {
            optopt = (unsigned char)c;
            optind++;
            g_optpos = 1;
            return (optstring[0] == ':') ? ':' : '?';
        }
    } else {
        g_optpos++;
        if (argv[optind][g_optpos] == '\0') {
            optind++;
            g_optpos = 1;
        }
    }
    return (unsigned char)c;
}

int getopt_long(int argc, char* const argv[], const char* optstring,
                const struct option* longopts, int* longindex) {
    optarg = (void*)0;
    if (optind >= argc || !argv[optind]) return -1;
    if (argv[optind][0] == '-' && argv[optind][1] == '-' && argv[optind][2] != '\0' && longopts) {
        const char* arg = argv[optind] + 2;
        const char* eq = strchr(arg, '=');
        size_t len = eq ? (size_t)(eq - arg) : strlen(arg);
        for (int i = 0; longopts[i].name; i++) {
            if (strlen(longopts[i].name) == len && strncmp(longopts[i].name, arg, len) == 0) {
                if (longindex) *longindex = i;
                if (longopts[i].has_arg == required_argument) {
                    if (eq) {
                        optarg = (char*)(eq + 1);
                        optind++;
                    } else if (optind + 1 < argc) {
                        optarg = argv[optind + 1];
                        optind += 2;
                    } else {
                        optind++;
                        return '?';
                    }
                } else if (longopts[i].has_arg == optional_argument) {
                    optarg = eq ? (char*)(eq + 1) : (void*)0;
                    optind++;
                } else {
                    optind++;
                }
                if (longopts[i].flag) {
                    *longopts[i].flag = longopts[i].val;
                    return 0;
                }
                return longopts[i].val;
            }
        }
        optind++;
        return '?';
    }
    return getopt(argc, argv, optstring);
}
