/* Shared reserved physical-memory regions for linker scripts and loaders. */
SHELL_IMAGE_BASE = 0x00290000;
/* Ends exactly at USER_DYNAMIC_APP_BASE (0x00300000). */
SHELL_IMAGE_SIZE = 0x00070000;
SHELL_IMAGE_END = SHELL_IMAGE_BASE + SHELL_IMAGE_SIZE;
