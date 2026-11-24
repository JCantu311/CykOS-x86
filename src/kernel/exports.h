#ifndef CYKOS_EXPORTS_H
#define CYKOS_EXPORTS_H

/* Resolve a public kernel API symbol for a dynamically linked application. */
int kernel_export_resolve(const char* name, unsigned int* address);
void kernel_module_services_keepalive(void);

#endif
