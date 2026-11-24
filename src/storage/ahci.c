#include "ahci.h"
#include "cykastd.h"
#include "pci.h"
#include "paging.h"

#define AHCI_PCI_CLASS       0x01u
#define AHCI_PCI_SUBCLASS    0x06u
#define AHCI_PCI_PROGIF      0x01u
#define AHCI_REG_GHC         0x04u
#define AHCI_REG_PI          0x0Cu
#define AHCI_PORT_BASE       0x100u
#define AHCI_PORT_STRIDE     0x80u
#define AHCI_PORT_CLB        0x00u
#define AHCI_PORT_CLBU       0x04u
#define AHCI_PORT_FB         0x08u
#define AHCI_PORT_FBU        0x0Cu
#define AHCI_PORT_IS         0x10u
#define AHCI_PORT_IE         0x14u
#define AHCI_PORT_CMD        0x18u
#define AHCI_PORT_TFD        0x20u
#define AHCI_PORT_SIG        0x24u
#define AHCI_PORT_SSTS       0x28u
#define AHCI_PORT_SERR       0x30u
#define AHCI_PORT_CI         0x38u
#define AHCI_CMD_ST          (1u << 0)
#define AHCI_CMD_FRE         (1u << 4)
#define AHCI_CMD_FR          (1u << 14)
#define AHCI_CMD_CR          (1u << 15)
#define AHCI_TIMEOUT         2000000u
#define AHCI_SECTOR_BYTES    512u

typedef struct __attribute__((packed, aligned(32))) {
    unsigned short flags;
    unsigned short prdt_length;
    unsigned int transferred;
    unsigned int table_base;
    unsigned int table_base_upper;
    unsigned char reserved[16];
} ahci_command_header_t;

typedef struct __attribute__((packed)) {
    unsigned int base;
    unsigned int base_upper;
    unsigned int reserved;
    unsigned int byte_count_and_interrupt;
} ahci_prdt_t;

typedef struct __attribute__((packed, aligned(128))) {
    unsigned char command_fis[64];
    unsigned char atapi_command[16];
    unsigned char reserved[48];
    ahci_prdt_t prdt;
} ahci_command_table_t;

static unsigned int g_ahci_mmio;
static unsigned int g_ahci_port;
static unsigned int g_ahci_sectors;
static int g_ahci_initialized;
static int g_ahci_attempted;
static ahci_command_header_t g_ahci_commands[32] __attribute__((aligned(1024)));
static unsigned char g_ahci_received_fis[256] __attribute__((aligned(256)));
static ahci_command_table_t g_ahci_table __attribute__((aligned(128)));
static unsigned short g_ahci_identify[256] __attribute__((aligned(2)));

static volatile unsigned int* ahci_reg(unsigned int offset) {
    return (volatile unsigned int*)(g_ahci_mmio + offset);
}

static volatile unsigned int* ahci_port_reg(unsigned int port, unsigned int offset) {
    return ahci_reg(AHCI_PORT_BASE + port * AHCI_PORT_STRIDE + offset);
}

static int ahci_stop_port(unsigned int port) {
    volatile unsigned int* command = ahci_port_reg(port, AHCI_PORT_CMD);
    *command &= ~(AHCI_CMD_ST | AHCI_CMD_FRE);
    for (unsigned int timeout = 0; timeout < AHCI_TIMEOUT; ++timeout) {
        unsigned int value = *command;
        if (!(value & (AHCI_CMD_CR | AHCI_CMD_FR))) return 0;
    }
    return -1;
}

static int ahci_dma_address(const void* pointer, unsigned int* address) {
    unsigned int value = (unsigned int)pointer;
    if (!pointer || value > 0xFFFFFFFFu - 4096u) return -1;
    *address = value;
    return 0;
}

static int ahci_issue(unsigned char opcode, unsigned int lba,
                      unsigned short count, void* buffer, int write) {
    if (!g_ahci_initialized || !count) return -1;
    unsigned int byte_count = (unsigned int)count * AHCI_SECTOR_BYTES;
    unsigned int buffer_address = 0;
    if (buffer && ahci_dma_address(buffer, &buffer_address) != 0) return -1;

    memset(&g_ahci_table, 0, sizeof(g_ahci_table));
    unsigned char* fis = g_ahci_table.command_fis;
    fis[0] = 0x27; /* Register host-to-device FIS. */
    fis[1] = 0x80;
    fis[2] = opcode;
    fis[4] = (unsigned char)lba;
    fis[5] = (unsigned char)(lba >> 8);
    fis[6] = (unsigned char)(lba >> 16);
    fis[7] = 1u << 6; /* LBA mode. */
    fis[8] = (unsigned char)(lba >> 24);
    fis[9] = 0;
    fis[10] = 0;
    fis[12] = (unsigned char)count;
    fis[13] = (unsigned char)(count >> 8);

    ahci_command_header_t* header = &g_ahci_commands[0];
    memset(header, 0, sizeof(*header));
    unsigned int table_address;
    if (ahci_dma_address(&g_ahci_table, &table_address) != 0) return -1;
    header->flags = 5u | (write ? (1u << 6) : 0u);
    header->prdt_length = buffer ? 1 : 0;
    header->table_base = table_address;
    if (buffer) {
        g_ahci_table.prdt.base = buffer_address;
        g_ahci_table.prdt.byte_count_and_interrupt = (byte_count - 1u) | (1u << 31);
    }

    volatile unsigned int* status = ahci_port_reg(g_ahci_port, AHCI_PORT_TFD);
    volatile unsigned int* issue = ahci_port_reg(g_ahci_port, AHCI_PORT_CI);
    for (unsigned int timeout = 0; timeout < AHCI_TIMEOUT; ++timeout) {
        if (!(*status & 0x88u)) break;
        if (timeout + 1u == AHCI_TIMEOUT) return -1;
    }
    *ahci_port_reg(g_ahci_port, AHCI_PORT_IS) = 0xFFFFFFFFu;
    *issue = 1u;
    for (unsigned int timeout = 0; timeout < AHCI_TIMEOUT; ++timeout) {
        if (!(*issue & 1u)) {
            unsigned int task = *status;
            return (task & 1u) ? -1 : 0;
        }
        if (*ahci_port_reg(g_ahci_port, AHCI_PORT_IS) & (1u << 30)) return -1;
    }
    return -1;
}

static int ahci_start_port(unsigned int port) {
    volatile unsigned int* command = ahci_port_reg(port, AHCI_PORT_CMD);
    if (ahci_stop_port(port) != 0) return -1;
    unsigned int command_list, fis_buffer;
    if (ahci_dma_address(g_ahci_commands, &command_list) != 0 ||
        ahci_dma_address(g_ahci_received_fis, &fis_buffer) != 0) return -1;
    *ahci_port_reg(port, AHCI_PORT_CLB) = command_list;
    *ahci_port_reg(port, AHCI_PORT_CLBU) = 0;
    *ahci_port_reg(port, AHCI_PORT_FB) = fis_buffer;
    *ahci_port_reg(port, AHCI_PORT_FBU) = 0;
    *ahci_port_reg(port, AHCI_PORT_IS) = 0xFFFFFFFFu;
    *ahci_port_reg(port, AHCI_PORT_SERR) = 0xFFFFFFFFu;
    *command |= AHCI_CMD_FRE;
    *command |= AHCI_CMD_ST;
    return 0;
}

int ahci_probe(unsigned int* sector_count) {
    if (g_ahci_attempted) {
        if (sector_count) *sector_count = g_ahci_sectors;
        return g_ahci_initialized;
    }
    g_ahci_attempted = 1;
    pci_device_t controller;
    if (!pci_find_class_device(AHCI_PCI_CLASS, AHCI_PCI_SUBCLASS,
                               AHCI_PCI_PROGIF, &controller)) return 0;
    unsigned int abar = pci_read_config_dword(controller.bus, controller.slot,
                                               controller.func, 0x24);
    if ((abar & 1u) || (abar & 6u) == 4u) return 0;
    unsigned int mmio = abar & ~0x0Fu;
    if (!mmio || mmio > 0xFFFFEFFFu) return 0;
    unsigned short command = pci_read_config_word(controller.bus, controller.slot,
                                                   controller.func, 0x04);
    pci_write_config_word(controller.bus, controller.slot, controller.func, 0x04,
                          command | 0x0006u); /* memory decode and bus mastering */
    if (paging_map_mmio_range(mmio, 0x1200u) != 0) return 0;
    g_ahci_mmio = mmio;
    *ahci_reg(AHCI_REG_GHC) |= 1u << 31;

    unsigned int implemented = *ahci_reg(AHCI_REG_PI);
    for (unsigned int port = 0; port < 32; ++port) {
        if (!(implemented & (1u << port))) continue;
        unsigned int ssts = *ahci_port_reg(port, AHCI_PORT_SSTS);
        unsigned int signature = *ahci_port_reg(port, AHCI_PORT_SIG);
        if ((ssts & 0x0Fu) != 3u || ((ssts >> 8) & 0x0Fu) != 1u ||
            signature != 0x00000101u) continue;
        if (ahci_start_port(port) != 0) continue;
        g_ahci_port = port;
        g_ahci_initialized = 1;
        if (ahci_issue(0xEC, 0, 1, g_ahci_identify, 0) != 0) {
            g_ahci_initialized = 0;
            continue;
        }
        unsigned int sectors = (unsigned int)g_ahci_identify[60] |
                               ((unsigned int)g_ahci_identify[61] << 16);
        if (g_ahci_identify[83] & (1u << 10)) {
            unsigned int lba48 = (unsigned int)g_ahci_identify[100] |
                ((unsigned int)g_ahci_identify[101] << 16);
            if (lba48) sectors = lba48;
        }
        if (!sectors) {
            g_ahci_initialized = 0;
            continue;
        }
        g_ahci_sectors = sectors;
        if (sector_count) *sector_count = sectors;
        print_string("[AHCI] SATA disk initialized through native AHCI DMA.\n");
        return 1;
    }
    return 0;
}

int ahci_read_sectors(unsigned int lba, unsigned char count, unsigned short* buffer) {
    if (!buffer || !count || lba >= g_ahci_sectors ||
        (unsigned int)count > g_ahci_sectors - lba) return -1;
    return ahci_issue(0x25, lba, count, buffer, 0);
}

int ahci_write_sectors(unsigned int lba, unsigned char count,
                       const unsigned short* buffer) {
    if (!buffer || !count || lba >= g_ahci_sectors ||
        (unsigned int)count > g_ahci_sectors - lba) return -1;
    if (ahci_issue(0x35, lba, count, (void*)buffer, 1) != 0) return -1;
    return ahci_issue(0xEA, 0, 1, 0, 0); /* FLUSH CACHE EXT */
}
