#include "module.h"
#include "cykastd.h"
#include "pci.h"
#include "paging.h"

#define INTEL_VENDOR_ID     0x8086u
#define PCI_CLASS_NETWORK   0x02u
#define PCI_SUBCLASS_ETH    0x00u

/* Intel e1000 MMIO register offsets */
#define E1000_REG_CTRL      0x0000u
#define E1000_REG_STATUS    0x0008u
#define E1000_REG_EERD      0x0014u
#define E1000_REG_ICR       0x00C0u
#define E1000_REG_IMC       0x00D8u
#define E1000_REG_RCTL      0x0100u
#define E1000_REG_TCTL      0x0400u
#define E1000_REG_TIPG      0x0410u
#define E1000_REG_RDBAL     0x2800u
#define E1000_REG_RDBAH     0x2804u
#define E1000_REG_RDLEN     0x2808u
#define E1000_REG_RDH       0x2810u
#define E1000_REG_RDT       0x2818u
#define E1000_REG_TDBAL     0x3800u
#define E1000_REG_TDBAH     0x3804u
#define E1000_REG_TDLEN     0x3808u
#define E1000_REG_TDH       0x3810u
#define E1000_REG_TDT       0x3818u
#define E1000_REG_MTA       0x5200u
#define E1000_REG_RAL0      0x5400u
#define E1000_REG_RAH0      0x5404u

#define E1000_CTRL_SLU      (1u << 6)
#define E1000_CTRL_RST      (1u << 26)
#define E1000_STATUS_LU     (1u << 1)

#define E1000_EERD_START    (1u << 0)
#define E1000_EERD_DONE     (1u << 4)

#define E1000_RCTL_EN       (1u << 1)
#define E1000_RCTL_SBP      (1u << 2)
#define E1000_RCTL_UPE      (1u << 3)
#define E1000_RCTL_MPE      (1u << 4)
#define E1000_RCTL_BAM      (1u << 15)
#define E1000_RCTL_SECRC    (1u << 26)

#define E1000_TCTL_EN       (1u << 1)
#define E1000_TCTL_PSP      (1u << 3)
#define E1000_TCTL_CT_SHIFT 4u
#define E1000_TCTL_COLD_SHIFT 12u

#define E1000_TX_CMD_EOP    (1u << 0)
#define E1000_TX_CMD_IFCS   (1u << 1)
#define E1000_TX_CMD_RS     (1u << 3)
#define E1000_DESC_DD       0x01u

#define E1000_RX_DESC_COUNT 32u
#define E1000_TX_DESC_COUNT 16u
#define E1000_PKT_BUF_SIZE  2048u

typedef struct __attribute__((packed)) {
    unsigned int addr_low;
    unsigned int addr_high;
    unsigned short length;
    unsigned short checksum;
    unsigned char status;
    unsigned char errors;
    unsigned short special;
} e1000_rx_desc_t;

typedef struct __attribute__((packed)) {
    unsigned int addr_low;
    unsigned int addr_high;
    unsigned short length;
    unsigned char cso;
    unsigned char cmd;
    unsigned char status;
    unsigned char css;
    unsigned short special;
} e1000_tx_desc_t;

static volatile unsigned int* g_mmio = (void*)0;
static unsigned char g_mac[6];
static e1000_rx_desc_t* g_rx_descs = (void*)0;
static e1000_tx_desc_t* g_tx_descs = (void*)0;
static unsigned char* g_rx_bufs[E1000_RX_DESC_COUNT];
static unsigned char* g_tx_bufs[E1000_TX_DESC_COUNT];
static unsigned int g_rx_cur = 0;
static unsigned int g_tx_cur = 0;
static int g_initialized = 0;

static inline unsigned int e1000_read32(unsigned int reg) {
    return g_mmio[reg >> 2];
}

static inline void e1000_write32(unsigned int reg, unsigned int val) {
    g_mmio[reg >> 2] = val;
}

static int e1000_is_supported_device(unsigned short vendor, unsigned short device) {
    if (vendor != INTEL_VENDOR_ID) return 0;
    switch (device) {
        case 0x100Eu: /* 82540EM (QEMU default e1000) */
        case 0x100Fu: /* 82545EM */
        case 0x1004u: /* 82543GC */
        case 0x100Cu: /* 82544GC */
        case 0x1019u: /* 82547EI */
        case 0x10D3u: /* 82574L (e1000e) */
        case 0x1502u: /* 82579LM */
            return 1;
        default:
            return 0;
    }
}

static int e1000_find_pci(pci_device_t* out_dev) {
    for (unsigned int bus = 0; bus < 16u; bus++) {
        for (unsigned char slot = 0; slot < 32u; slot++) {
            unsigned short vendor0 = pci_read_config_word((unsigned char)bus, slot, 0, PCI_VENDOR_ID);
            if (vendor0 == 0xFFFFu || vendor0 == 0u) continue;
            unsigned char funcs = (pci_read_config_byte((unsigned char)bus, slot, 0, 0x0Eu) & 0x80u) ? 8u : 1u;
            for (unsigned char func = 0; func < funcs; func++) {
                unsigned short vendor = (func == 0) ? vendor0 :
                    pci_read_config_word((unsigned char)bus, slot, func, PCI_VENDOR_ID);
                if (vendor == 0xFFFFu || vendor == 0u) continue;
                unsigned char cls = pci_read_config_byte((unsigned char)bus, slot, func, PCI_CLASS);
                unsigned char sub = pci_read_config_byte((unsigned char)bus, slot, func, PCI_SUBCLASS);
                if (cls != PCI_CLASS_NETWORK || sub != PCI_SUBCLASS_ETH) continue;
                unsigned short dev_id = pci_read_config_word((unsigned char)bus, slot, func, PCI_DEVICE_ID);
                if (!e1000_is_supported_device(vendor, dev_id)) continue;
                out_dev->bus = (unsigned char)bus;
                out_dev->slot = slot;
                out_dev->func = func;
                out_dev->vendor_id = vendor;
                out_dev->device_id = dev_id;
                out_dev->class_id = cls;
                out_dev->subclass_id = sub;
                out_dev->prog_if = pci_read_config_byte((unsigned char)bus, slot, func, PCI_PROG_IF);
                out_dev->bar0 = pci_read_config_dword((unsigned char)bus, slot, func, PCI_BAR0);
                out_dev->bar1 = pci_read_config_dword((unsigned char)bus, slot, func, PCI_BAR1);
                out_dev->irq = pci_read_config_byte((unsigned char)bus, slot, func, PCI_INTERRUPT_LINE);
                return 1;
            }
        }
    }
    return 0;
}

static int e1000_read_eeprom(unsigned char addr, unsigned short* out_val) {
    e1000_write32(E1000_REG_EERD, E1000_EERD_START | ((unsigned int)addr << 8));
    for (unsigned int i = 0; i < 10000u; i++) {
        unsigned int val = e1000_read32(E1000_REG_EERD);
        if (val & E1000_EERD_DONE) {
            *out_val = (unsigned short)(val >> 16);
            return 0;
        }
    }
    return -1;
}

static void e1000_read_mac_address(void) {
    unsigned short w0 = 0, w1 = 0, w2 = 0;
    if (e1000_read_eeprom(0, &w0) == 0 &&
        e1000_read_eeprom(1, &w1) == 0 &&
        e1000_read_eeprom(2, &w2) == 0 &&
        (w0 != 0xFFFFu || w1 != 0xFFFFu || w2 != 0xFFFFu)) {
        g_mac[0] = (unsigned char)(w0 & 0xFFu);
        g_mac[1] = (unsigned char)(w0 >> 8);
        g_mac[2] = (unsigned char)(w1 & 0xFFu);
        g_mac[3] = (unsigned char)(w1 >> 8);
        g_mac[4] = (unsigned char)(w2 & 0xFFu);
        g_mac[5] = (unsigned char)(w2 >> 8);
    } else {
        unsigned int ral = e1000_read32(E1000_REG_RAL0);
        unsigned int rah = e1000_read32(E1000_REG_RAH0);
        g_mac[0] = (unsigned char)(ral & 0xFFu);
        g_mac[1] = (unsigned char)((ral >> 8) & 0xFFu);
        g_mac[2] = (unsigned char)((ral >> 16) & 0xFFu);
        g_mac[3] = (unsigned char)((ral >> 24) & 0xFFu);
        g_mac[4] = (unsigned char)(rah & 0xFFu);
        g_mac[5] = (unsigned char)((rah >> 8) & 0xFFu);
    }

    unsigned int ral = (unsigned int)g_mac[0] |
                       ((unsigned int)g_mac[1] << 8) |
                       ((unsigned int)g_mac[2] << 16) |
                       ((unsigned int)g_mac[3] << 24);
    unsigned int rah = (unsigned int)g_mac[4] |
                       ((unsigned int)g_mac[5] << 8) |
                       0x80000000u;
    e1000_write32(E1000_REG_RAL0, ral);
    e1000_write32(E1000_REG_RAH0, rah);
}

static void* alloc_aligned16(unsigned int bytes) {
    unsigned int raw = (unsigned int)malloc(bytes + 15u);
    if (!raw) return (void*)0;
    unsigned int aligned = (raw + 15u) & ~15u;
    memset((void*)aligned, 0, bytes);
    return (void*)aligned;
}

static int e1000_module_init(void) {
    pci_device_t dev;
    if (!e1000_find_pci(&dev)) {
        print_string("[e1000] No Intel e1000 NIC found on PCI.\n");
        return -1;
    }

    if (dev.bar0 & 1u) {
        print_color_string("[e1000] BAR0 is I/O space; MMIO required.\n", VGA_RED);
        return -1;
    }

    unsigned int mmio_base = dev.bar0 & ~0xFu;
    if (!mmio_base || paging_map_mmio_range(mmio_base, 0x20000u) != 0) {
        print_color_string("[e1000] Failed to map MMIO BAR0.\n", VGA_RED);
        return -1;
    }
    g_mmio = (volatile unsigned int*)mmio_base;

    /* Enable PCI Memory Space and Bus Mastering */
    unsigned short cmd = pci_read_config_word(dev.bus, dev.slot, dev.func, PCI_COMMAND);
    cmd |= 0x0006u;
    pci_write_config_word(dev.bus, dev.slot, dev.func, PCI_COMMAND, cmd);

    /* Reset NIC */
    e1000_write32(E1000_REG_IMC, 0xFFFFFFFFu);
    e1000_write32(E1000_REG_CTRL, e1000_read32(E1000_REG_CTRL) | E1000_CTRL_RST);
    msecwait(10);
    e1000_write32(E1000_REG_IMC, 0xFFFFFFFFu);
    (void)e1000_read32(E1000_REG_ICR);

    /* Set Link Up */
    unsigned int ctrl = e1000_read32(E1000_REG_CTRL);
    ctrl |= E1000_CTRL_SLU;
    ctrl &= ~(1u << 31); /* Clear PHY_RST if set */
    e1000_write32(E1000_REG_CTRL, ctrl);

    e1000_read_mac_address();

    /* Clear Multicast Table Array */
    for (unsigned int i = 0; i < 128u; i++) {
        e1000_write32(E1000_REG_MTA + i * 4u, 0);
    }

    /* Allocate RX ring & buffers */
    g_rx_descs = (e1000_rx_desc_t*)alloc_aligned16(E1000_RX_DESC_COUNT * sizeof(e1000_rx_desc_t));
    g_tx_descs = (e1000_tx_desc_t*)alloc_aligned16(E1000_TX_DESC_COUNT * sizeof(e1000_tx_desc_t));
    if (!g_rx_descs || !g_tx_descs) {
        print_color_string("[e1000] Failed to allocate descriptor rings.\n", VGA_RED);
        return -1;
    }

    for (unsigned int i = 0; i < E1000_RX_DESC_COUNT; i++) {
        g_rx_bufs[i] = (unsigned char*)alloc_aligned16(E1000_PKT_BUF_SIZE);
        if (!g_rx_bufs[i]) return -1;
        g_rx_descs[i].addr_low = (unsigned int)g_rx_bufs[i];
        g_rx_descs[i].addr_high = 0;
        g_rx_descs[i].status = 0;
    }

    for (unsigned int i = 0; i < E1000_TX_DESC_COUNT; i++) {
        g_tx_bufs[i] = (unsigned char*)alloc_aligned16(E1000_PKT_BUF_SIZE);
        if (!g_tx_bufs[i]) return -1;
        g_tx_descs[i].addr_low = (unsigned int)g_tx_bufs[i];
        g_tx_descs[i].addr_high = 0;
        g_tx_descs[i].cmd = 0;
        g_tx_descs[i].status = E1000_DESC_DD;
    }

    g_rx_cur = 0;
    g_tx_cur = 0;

    /* Program RX registers */
    e1000_write32(E1000_REG_RDBAL, (unsigned int)g_rx_descs);
    e1000_write32(E1000_REG_RDBAH, 0);
    e1000_write32(E1000_REG_RDLEN, E1000_RX_DESC_COUNT * sizeof(e1000_rx_desc_t));
    e1000_write32(E1000_REG_RDH, 0);
    e1000_write32(E1000_REG_RDT, E1000_RX_DESC_COUNT - 1u);
    e1000_write32(E1000_REG_RCTL,
                  E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SECRC);

    /* Program TX registers */
    e1000_write32(E1000_REG_TDBAL, (unsigned int)g_tx_descs);
    e1000_write32(E1000_REG_TDBAH, 0);
    e1000_write32(E1000_REG_TDLEN, E1000_TX_DESC_COUNT * sizeof(e1000_tx_desc_t));
    e1000_write32(E1000_REG_TDH, 0);
    e1000_write32(E1000_REG_TDT, 0);
    e1000_write32(E1000_REG_TCTL,
                  E1000_TCTL_EN | E1000_TCTL_PSP |
                  (0x10u << E1000_TCTL_CT_SHIFT) |
                  (0x40u << E1000_TCTL_COLD_SHIFT));
    e1000_write32(E1000_REG_TIPG, 0x0060200Au);

    g_initialized = 1;
    print_color_string("[e1000] Intel 8254x NIC initialized.\n", VGA_GREEN);
    return 0;
}

static int e1000_send_frame(const unsigned char* data, unsigned int len) {
    if (!g_initialized || !data || len == 0 || len > 1518u) return -1;

    volatile e1000_tx_desc_t* desc = &g_tx_descs[g_tx_cur];
    unsigned int spin = 0;
    while (!(desc->status & E1000_DESC_DD)) {
        if (++spin > 100000u) return -1;
    }

    memcpy(g_tx_bufs[g_tx_cur], data, len);
    desc->length = (unsigned short)len;
    desc->cso = 0;
    desc->css = 0;
    desc->special = 0;
    desc->cmd = E1000_TX_CMD_EOP | E1000_TX_CMD_IFCS | E1000_TX_CMD_RS;
    desc->status = 0;

    g_tx_cur = (g_tx_cur + 1u) % E1000_TX_DESC_COUNT;
    e1000_write32(E1000_REG_TDT, g_tx_cur);
    return 0;
}

static int e1000_receive_frame(unsigned char* out_buf, unsigned int max_len) {
    if (!g_initialized || !out_buf || max_len == 0) return -1;

    volatile e1000_rx_desc_t* desc = &g_rx_descs[g_rx_cur];
    if (!(desc->status & E1000_DESC_DD)) {
        return 0;
    }

    unsigned int pkt_len = desc->length;
    if (pkt_len > max_len) pkt_len = max_len;
    if (pkt_len > 0 && !(desc->errors & 0x97u)) {
        memcpy(out_buf, g_rx_bufs[g_rx_cur], pkt_len);
    } else {
        pkt_len = 0;
    }

    desc->status = 0;
    desc->errors = 0;
    unsigned int old_cur = g_rx_cur;
    g_rx_cur = (g_rx_cur + 1u) % E1000_RX_DESC_COUNT;
    e1000_write32(E1000_REG_RDT, old_cur);
    return (int)pkt_len;
}

static void e1000_get_mac(unsigned char out_mac[6]) {
    if (!out_mac) return;
    memcpy(out_mac, g_mac, 6u);
}

static int e1000_link_up(void) {
    if (!g_initialized) return 0;
    return (e1000_read32(E1000_REG_STATUS) & E1000_STATUS_LU) ? 1 : 0;
}

__attribute__((section(".header"), used))
const cyk_net_driver_t cyk_net_driver = {
    .header = {
        .magic = CYKOS_MODULE_MAGIC,
        .name = "cyknet",
        .version = 0x00010000,
        .type = MODULE_TYPE_NET,
        .load_address = MODULE_LOAD_ADDRESS,
        .init = e1000_module_init,
    },
    .send_frame = e1000_send_frame,
    .receive_frame = e1000_receive_frame,
    .get_mac = e1000_get_mac,
    .link_up = e1000_link_up,
    .hardware_name = "Intel e1000 (82540EM)",
};
