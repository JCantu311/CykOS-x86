#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

static void open_library(lua_State* state, const char* name, lua_CFunction open_fn) {
    luaL_requiref(state, name, open_fn, 1);
    lua_pop(state, 1);
}

static int run_loaded(lua_State* state) {
    int status = lua_pcall(state, 0, LUA_MULTRET, 0);
    if (status != LUA_OK) {
        const char* message = lua_tostring(state, -1);
        fprintf(stderr, "lua: %s\n", message ? message : "unknown error");
        lua_pop(state, 1);
    }
    return status;
}

static int run_string(lua_State* state, const char* source, const char* name) {
    int status = luaL_loadbufferx(state, source, strlen(source), name, "t");
    if (status != LUA_OK) {
        const char* message = lua_tostring(state, -1);
        fprintf(stderr, "lua: %s\n", message ? message : "load error");
        lua_pop(state, 1);
        return status;
    }
    return run_loaded(state);
}

static void set_script_arguments(lua_State* state, int argc, char** argv,
                                 int script_index) {
    lua_createtable(state, argc - script_index, 0);
    for (int i = script_index; i < argc; i++) {
        lua_pushstring(state, argv[i]);
        lua_rawseti(state, -2, i - script_index);
    }
    lua_setglobal(state, "arg");
}

static int repl(lua_State* state) {
    char line[512];
    char chunk[520];
    for (;;) {
        fputs("lua> ", stdout);
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;

        size_t length = strlen(line);
        while (length && (line[length - 1] == '\n' || line[length - 1] == '\r'))
            line[--length] = '\0';
        if (length == 0) continue;

        const char* source = line;
        const char* name = "=stdin";
        if (line[0] == '=') {
            chunk[0] = 'r'; chunk[1] = 'e'; chunk[2] = 't'; chunk[3] = 'u';
            chunk[4] = 'r'; chunk[5] = 'n'; chunk[6] = ' ';
            memcpy(chunk + 7, line + 1, length);
            chunk[length + 7] = '\0';
            source = chunk;
        }

        int status = luaL_loadbufferx(state, source, strlen(source), name, "t");
        if (status != LUA_OK && line[0] != '=') {
            lua_pop(state, 1);
            chunk[0] = 'r'; chunk[1] = 'e'; chunk[2] = 't'; chunk[3] = 'u';
            chunk[4] = 'r'; chunk[5] = 'n'; chunk[6] = ' ';
            memcpy(chunk + 7, line, length + 1);
            status = luaL_loadbufferx(state, chunk, strlen(chunk), name, "t");
            if (status != LUA_OK) {
                lua_pop(state, 1);
                status = luaL_loadbufferx(state, line, length, name, "t");
            }
        }
        if (status != LUA_OK) {
            const char* message = lua_tostring(state, -1);
            fprintf(stderr, "lua: %s\n", message ? message : "syntax error");
            lua_pop(state, 1);
            continue;
        }
        int base = lua_gettop(state) - 1;
        if (run_loaded(state) == LUA_OK) {
            int top = lua_gettop(state);
            for (int i = base + 1; i <= top; i++) {
                size_t value_length;
                const char* value = luaL_tolstring(state, i, &value_length);
                if (i > base + 1) fputs("\t", stdout);
                fwrite(value, 1, value_length, stdout);
                lua_pop(state, 1);
            }
            if (top > base) fputc('\n', stdout);
            lua_settop(state, base);
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    lua_State* state = luaL_newstate();
    if (!state) {
        fputs("lua: not enough memory\n", stderr);
        return 1;
    }

    open_library(state, LUA_GNAME, luaopen_base);
    open_library(state, LUA_COLIBNAME, luaopen_coroutine);
    open_library(state, LUA_STRLIBNAME, luaopen_string);
    open_library(state, LUA_TABLIBNAME, luaopen_table);
    open_library(state, LUA_UTF8LIBNAME, luaopen_utf8);
    int script_index = 0;
    int interactive = argc == 1;
    int status = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--") == 0) {
            if (i + 1 < argc) script_index = i + 1;
            break;
        }
        if (strcmp(argv[i], "-v") == 0) {
            printf("Lua %s for CykOS\n", LUA_VERSION);
        } else if (strcmp(argv[i], "-i") == 0) {
            interactive = 1;
        } else if (strcmp(argv[i], "-e") == 0 && i + 1 < argc) {
            status = run_string(state, argv[++i], "=(command line)");
            if (status != LUA_OK) break;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "lua: unsupported option: %s\n", argv[i]);
            status = LUA_ERRSYNTAX;
            break;
        } else {
            script_index = i;
            break;
        }
    }

    if (status == LUA_OK && script_index) {
        set_script_arguments(state, argc, argv, script_index);
        status = luaL_loadfilex(state, argv[script_index], "t");
        if (status == LUA_OK) status = run_loaded(state);
        else {
            const char* message = lua_tostring(state, -1);
            fprintf(stderr, "lua: %s\n", message ? message : "file load error");
            lua_pop(state, 1);
        }
    }

    if (status == LUA_OK && interactive) status = repl(state);
    lua_close(state);
    return status == LUA_OK ? 0 : 1;
}
