/*
 * Retired linker script.
 *
 * Driver modules are now linked as ELF32 ET_DYN images by drivers/build.sh.
 * Their PT_LOAD segments and relocations are mapped into heap allocations by
 * module_elf32_load() at boot; no fixed module base address is used.
 */
