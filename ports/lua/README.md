# Lua on CykOS

CykOS builds the interpreter from the official Lua source in `third_party/lua`
and the CykOS-specific host in this directory. The upstream tree has one
guarded CykOS setting in `luaconf.h` for 32-bit Lua integers. Run
`lua script.lua` for a script, `lua -e 'print(1 + 2)'` for a
one-line command, or `lua` for the interactive prompt.
After rebuilding the disk image, run `lua /USR/BIN/TESTS/LUASMOKE.LUA` for the
bundled smoke test.

The first port intentionally opens Lua's base, coroutine, string, table, and
UTF-8 libraries. The package, debug, I/O, OS, and math libraries are omitted
while their platform dependencies are assessed. Lua's base `dofile` and
`loadfile` functions use CykOS stdio file access. The small prompt does not yet
support multiline input, and the current C library does not provide floating
point `printf` formatting, so formatted float output needs follow-up work.

The CykOS compatibility layer supplies the numeric and locale functions needed
by the Lua core. Floating-point conversion is currently a small C-locale
implementation intended to get the port running; it does not promise the full
range and precision of a hosted C library's `strtod`.

Lua is distributed under the Lua license. See `third_party/lua/lua.h` and the
official source repository at <https://github.com/lua/lua>.
