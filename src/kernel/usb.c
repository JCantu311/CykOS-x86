#include "usb.h"
#include "pci.h"
#include "cykastd.h"
#include "module.h"
#include "idt.h"
#include "paging.h"
#include "xhci.h"

#define PCI_CLASS_SERIAL_BUS 0x0C
#define PCI_SUBCLASS_USB     0x03
#define USB_MAX_CONTROLLERS  32
#define USB_MAX_KEYBOARDS    8
#define USB_MAX_STORAGE      USB_STORAGE_MAX_DEVICES
#define USB_KEY_REPEAT_DELAY_MS 500u
#define USB_KEY_REPEAT_RATE_MS   40u

#define UHCI_CMD       0x00
#define UHCI_STS       0x02
#define UHCI_INTR      0x04
#define UHCI_FRNUM     0x06
#define UHCI_FLBASE    0x08
#define UHCI_SOFMOD    0x0C
#define UHCI_PORTSC1   0x10
#define UHCI_PORTSC2   0x12
#define UHCI_CMD_RUN   0x0001
#define UHCI_CMD_CF    0x0040
#define UHCI_CMD_MAXP  0x0080
#define UHCI_TD_ACTIVE 0x00800000u
#define UHCI_TD_ERROR  0x00760000u
#define UHCI_PTR_TERM  0x00000001u
#define UHCI_PTR_QH    0x00000002u
#define UHCI_PTR_DEPTH 0x00000004u

#define OHCI_CONTROL        0x04u
#define OHCI_COMMAND        0x08u
#define OHCI_INTERRUPT      0x14u
#define OHCI_HCCA           0x18u
#define OHCI_CONTROL_HEAD   0x20u
#define OHCI_BULK_HEAD      0x28u
#define OHCI_DONE_HEAD      0x30u
#define OHCI_FRAME_INTERVAL 0x34u
#define OHCI_PERIODIC_START 0x40u
#define OHCI_LOW_SPEED_MAX  0x44u
#define OHCI_RH_DESC_A      0x48u
#define OHCI_RH_PORT_BASE   0x54u
#define OHCI_HCR            0x00000001u
#define OHCI_CONTROL_CLE    0x00000010u
#define OHCI_CONTROL_BLE    0x00000020u
#define OHCI_CONTROL_PLE    0x00000004u
#define OHCI_CONTROL_OPERATIONAL 0x00000080u
#define OHCI_CC_NOT_ACCESSED 0xFu
#define OHCI_CC_NO_ERROR     0x0u
#define OHCI_CC_DATA_UNDERRUN 0x9u

#define EHCI_USBCMD       0x00u
#define EHCI_USBSTS       0x04u
#define EHCI_USBINTR      0x08u
#define EHCI_CTRLDSSEGMENT 0x10u
#define EHCI_PERIODICBASE 0x14u
#define EHCI_ASYNCBASE    0x18u
#define EHCI_CONFIGFLAG   0x40u
#define EHCI_PORTSC_BASE  0x44u
#define EHCI_CMD_RUN      (1u << 0)
#define EHCI_CMD_RESET    (1u << 1)
#define EHCI_CMD_PSE      (1u << 4)
#define EHCI_CMD_ASE      (1u << 5)
#define EHCI_STS_HALTED   (1u << 12)
#define EHCI_STS_PSS      (1u << 14)
#define EHCI_STS_ASS      (1u << 15)
#define EHCI_QTD_ACTIVE   (1u << 7)

void usb_stack_poll(void);
int usb_stack_storage_present(unsigned int device);

typedef struct {
    pci_device_t pci;
    unsigned char interface;
    unsigned short io_base;
    unsigned int mmio_base;
    unsigned char port_count;
    unsigned char initialized;
} usb_controller_t;

typedef struct __attribute__((aligned(16))) {
    volatile unsigned int link;
    volatile unsigned int status;
    volatile unsigned int token;
    volatile unsigned int buffer;
} uhci_td_t;

typedef struct __attribute__((aligned(16))) {
    volatile unsigned int head;
    volatile unsigned int element;
} uhci_qh_t;

typedef struct __attribute__((aligned(16))) {
    volatile unsigned int control;
    volatile unsigned int current_buffer;
    volatile unsigned int next;
    volatile unsigned int buffer_end;
} ohci_td_t;

typedef struct __attribute__((aligned(16))) {
    volatile unsigned int control;
    volatile unsigned int tail;
    volatile unsigned int head;
    volatile unsigned int next;
} ohci_ed_t;

typedef struct __attribute__((aligned(256))) {
    volatile unsigned int interrupt_table[32];
    volatile unsigned short frame_number;
    volatile unsigned short pad;
    volatile unsigned int done_head;
    unsigned char reserved[120];
} ohci_hcca_t;

typedef struct __attribute__((aligned(32))) {
    volatile unsigned int horizontal;
    volatile unsigned int endpoint;
    volatile unsigned int capabilities;
    volatile unsigned int current_qtd;
    volatile unsigned int next_qtd;
    volatile unsigned int alternate_qtd;
    volatile unsigned int token;
    volatile unsigned int buffer[5];
} ehci_qh_t;

typedef struct __attribute__((aligned(32))) {
    volatile unsigned int next;
    volatile unsigned int alternate;
    volatile unsigned int token;
    volatile unsigned int buffer[5];
} ehci_qtd_t;

typedef struct {
    unsigned char controller_index;
    unsigned char address;
    unsigned char endpoint;
    unsigned char interface_number;
    unsigned char low_speed;
    unsigned char max_packet;
    unsigned char toggle;
    unsigned char interval;
    unsigned int last_poll;
    unsigned char previous[8];
    unsigned int repeat_at[256];
} usb_keyboard_t;

typedef struct {
    unsigned char toggle_in, toggle_out;
} usb_msc_transport_t;

typedef struct {
    unsigned char controller_index;
    unsigned char address, interface_number, endpoint_in, endpoint_out;
    unsigned short max_packet_in, max_packet_out;
    unsigned char lun, transport_index;
    unsigned char control_packet;
    unsigned char valid;
    unsigned int last_lba;
    unsigned int tag;
} usb_storage_t;

static usb_controller_t g_usb_controllers[USB_MAX_CONTROLLERS];
static unsigned int g_usb_controller_count;
static unsigned short g_uhci_base;
static unsigned int g_uhci_ready;
static unsigned int g_active_interface;
static unsigned int g_active_controller_index;
static volatile unsigned int* g_ohci_registers;
static unsigned int g_ohci_ready;
static unsigned int g_ohci_ports;
static volatile unsigned int* g_ehci_registers;
static unsigned int g_ehci_ready;
static unsigned int g_ehci_ports;
static unsigned int g_xhci_ready;
static unsigned int g_xhci_initialized;
static unsigned int g_next_address = 1;
static uhci_td_t g_uhci_tds[66] __attribute__((aligned(16)));
static uhci_qh_t g_uhci_qh __attribute__((aligned(16)));
static unsigned int g_uhci_frames[1024] __attribute__((aligned(4096)));
static unsigned char g_uhci_bounce[4096] __attribute__((aligned(4096)));
static unsigned char g_uhci_control[512] __attribute__((aligned(4096)));
static usb_keyboard_t g_usb_keyboards[USB_MAX_KEYBOARDS];
static unsigned int g_usb_keyboard_count;
static usb_storage_t g_usb_storage[USB_MAX_STORAGE];
static unsigned int g_usb_storage_count;
static usb_msc_transport_t g_usb_msc_transports[USB_MAX_STORAGE];
static unsigned int g_usb_msc_transport_count;
static ohci_hcca_t g_ohci_hcca __attribute__((aligned(256)));
static ohci_ed_t g_ohci_ed __attribute__((aligned(16)));
static ohci_td_t g_ohci_tds[66] __attribute__((aligned(16)));
static ohci_td_t g_ohci_dummy __attribute__((aligned(16)));
static ehci_qh_t g_ehci_qh __attribute__((aligned(32)));
static ehci_qtd_t g_ehci_qtd __attribute__((aligned(32)));
static unsigned int g_ehci_frames[1024] __attribute__((aligned(4096)));

static int ehci_transfer(unsigned char pid, unsigned char address, unsigned char endpoint,
                         unsigned char toggle, void* buffer, unsigned short length,
                         unsigned short max_packet, unsigned short* actual,
                         unsigned int transfer_type);
static int ehci_bulk_transfer(unsigned char address, unsigned char endpoint, int input,
                              unsigned char* toggle, unsigned short max_packet,
                              void* buffer, unsigned short length, unsigned short* actual);
static int ehci_interrupt_transfer(unsigned char address, unsigned char endpoint,
                                   unsigned char toggle, unsigned char max_packet,
                                   void* buffer, unsigned short length,
                                   unsigned short* actual);

static void usb_select_controller(unsigned int index) {
    if (index >= g_usb_controller_count) return;
    usb_controller_t* controller = &g_usb_controllers[index];
    g_active_controller_index = index;
    g_active_interface = controller->interface;
    g_uhci_base = controller->io_base;
    g_uhci_ready = controller->interface == 0x00 && controller->initialized;
    g_ohci_registers = controller->interface == 0x10
        ? (volatile unsigned int*)controller->mmio_base : (volatile unsigned int*)0;
    g_ohci_ready = controller->interface == 0x10 && controller->initialized;
    g_ohci_ports = controller->port_count;
    g_ehci_registers = controller->interface == 0x20
        ? (volatile unsigned int*)controller->mmio_base : (volatile unsigned int*)0;
    g_ehci_ready = controller->interface == 0x20 && controller->initialized;
    g_ehci_ports = controller->port_count;
    g_xhci_ready = controller->interface == 0x30 && controller->initialized;
}

static inline unsigned char usb_inb(unsigned short port) {
    unsigned char value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static inline unsigned short usb_inw(unsigned short port) {
    unsigned short value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static inline unsigned int usb_inl(unsigned short port) {
    unsigned int value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static inline void usb_outb(unsigned short port, unsigned char value) {
    __asm__ volatile("outb %0, %1" :: "a"(value), "Nd"(port));
}
static inline void usb_outw(unsigned short port, unsigned short value) {
    __asm__ volatile("outw %0, %1" :: "a"(value), "Nd"(port));
}
static inline void usb_outl(unsigned short port, unsigned int value) {
    __asm__ volatile("outl %0, %1" :: "a"(value), "Nd"(port));
}

static const char* usb_controller_name(unsigned char interface) {
    switch (interface) {
        case 0x00: return "UHCI";
        case 0x10: return "OHCI";
        case 0x20: return "EHCI";
        case 0x30: return "xHCI";
        default: return "unknown USB HCI";
    }
}

/* This kernel and its DMA interfaces are 32-bit; never truncate a 64-bit BAR. */
static int usb_mmio_bar0_base(const usb_controller_t* controller, unsigned int* base) {
    unsigned int bar = pci_read_config_dword(controller->pci.bus, controller->pci.slot,
                                              controller->pci.func, PCI_BAR0);
    if (!base || (bar & 1u)) return -1;
    unsigned int type = (bar >> 1) & 3u;
    if (type == 2u && pci_read_config_dword(controller->pci.bus, controller->pci.slot,
                                             controller->pci.func, PCI_BAR1) != 0)
        return -1;
    if (type != 0u && type != 2u) return -1;
    *base = bar & 0xFFFFFFF0u;
    return *base ? 0 : -1;
}

static void usb_print_hex_byte(unsigned char value) {
    static const char digits[] = "0123456789ABCDEF";
    print_char(digits[(value >> 4) & 0x0F]);
    print_char(digits[value & 0x0F]);
}
static void usb_print_hex_word(unsigned short value) {
    usb_print_hex_byte((unsigned char)(value >> 8));
    usb_print_hex_byte((unsigned char)value);
}

static unsigned int uhci_phys(const void* pointer) {
    return (unsigned int)pointer;
}

static int ohci_transfer_chain(unsigned char pid, unsigned char address,
                               unsigned char endpoint, unsigned char toggle,
                               unsigned char low_speed, void* buffer,
                               unsigned short length, unsigned short max_packet,
                               unsigned short* actual, unsigned int transfer_type);
static int ohci_bulk_transfer(unsigned char address, unsigned char endpoint, int input,
                              unsigned char* toggle, unsigned short max_packet,
                              void* buffer, unsigned short length, unsigned short* actual);

/* Runs one TD at a time on the UHCI schedule. All DMA objects are static,
 * identity-mapped kernel memory below the 32 MiB DMA aperture. */
static int uhci_transfer(unsigned char pid, unsigned char address, unsigned char endpoint,
                         unsigned char toggle, unsigned char low_speed,
                         void* buffer, unsigned short length, unsigned short max_packet,
                         unsigned short* actual) {
    if (g_active_interface == 0x10u)
        return ohci_transfer_chain(pid, address, endpoint, toggle, low_speed,
                                   buffer, length, max_packet, actual, 0);
    if (g_active_interface == 0x20u)
        return ehci_transfer(pid, address, endpoint, toggle, buffer, length,
                             max_packet, actual, 0);
    if (!g_uhci_ready || !max_packet || length > 512 || (length && !buffer)) return -1;
    unsigned int flags = low_speed ? (1u << 26) : 0;
    flags |= (3u << 27) | (1u << 23); /* retry budget and active */
    unsigned int maxlen = length ? (unsigned int)(length - 1u) : 0x7FFu;
    if (length) {
        if (pid == 0x69) memset(g_uhci_bounce, 0, length);
        else memcpy(g_uhci_bounce, buffer, length);
    }
    g_uhci_qh.head = UHCI_PTR_TERM;
    g_uhci_qh.element = uhci_phys(&g_uhci_tds[0]);
    g_uhci_tds[0].link = UHCI_PTR_TERM;
    g_uhci_tds[0].status = flags;
    g_uhci_tds[0].token = (unsigned int)pid | ((unsigned int)(address & 0x7F) << 8) |
                          ((unsigned int)(endpoint & 0x0F) << 15) |
                          ((unsigned int)(toggle & 1) << 19) | ((maxlen & 0x7FFu) << 21);
    g_uhci_tds[0].buffer = length ? uhci_phys(g_uhci_bounce) : 0;
    for (unsigned int i = 0; i < 1024; ++i)
        g_uhci_frames[i] = uhci_phys(&g_uhci_qh) | UHCI_PTR_QH;

    usb_outw(g_uhci_base + UHCI_CMD, 0);
    usb_outw(g_uhci_base + UHCI_STS, 0x003F);
    usb_outl(g_uhci_base + UHCI_FLBASE, uhci_phys(g_uhci_frames));
    usb_outw(g_uhci_base + UHCI_FRNUM, 0);
    usb_outw(g_uhci_base + UHCI_CMD, UHCI_CMD_RUN | UHCI_CMD_CF | UHCI_CMD_MAXP);

    unsigned int start = idt_get_millis();
    while (g_uhci_tds[0].status & UHCI_TD_ACTIVE) {
        if ((g_uhci_tds[0].status & (UHCI_TD_ERROR | (1u << 19))) ||
            (unsigned int)(idt_get_millis() - start) > 100u) {
            usb_outw(g_uhci_base + UHCI_CMD, 0);
            return -1;
        }
        __asm__ volatile("pause");
    }
    usb_outw(g_uhci_base + UHCI_CMD, 0);
    if (g_uhci_tds[0].status & (UHCI_TD_ERROR | (1u << 19))) return -1;
    if (actual) {
        unsigned int encoded = g_uhci_tds[0].status & 0x7FFu;
        *actual = encoded == 0x7FFu ? 0 : (unsigned short)(encoded + 1u);
    }
    if (length && pid == 0x69 && actual && *actual)
        memcpy(buffer, g_uhci_bounce, *actual < length ? *actual : length);
    return 0;
}

static unsigned int ohci_td_control(unsigned char pid, unsigned char toggle, int rounding) {
    unsigned int direction = pid == 0x2D ? 0u : (pid == 0xE1 ? 1u : 2u);
    unsigned int data_toggle = toggle ? 2u : 1u;
    return (OHCI_CC_NOT_ACCESSED << 28) | (7u << 21) | (direction << 19) |
           (data_toggle << 24) | (rounding ? (1u << 18) : 0);
}

static int ohci_transfer_chain(unsigned char pid, unsigned char address,
                               unsigned char endpoint, unsigned char toggle,
                               unsigned char low_speed, void* buffer,
                               unsigned short length, unsigned short max_packet,
                               unsigned short* actual, unsigned int transfer_type) {
    if (!g_ohci_ready || !g_ohci_registers || !max_packet || max_packet > 64 ||
        length > 4096 || (length && !buffer)) return -1;
    unsigned int count = length ? (length + max_packet - 1u) / max_packet : 1u;
    if (count > sizeof(g_ohci_tds) / sizeof(g_ohci_tds[0])) return -1;
    int input = pid == 0x69;
    if (length) {
        if (input) memset(g_uhci_bounce, 0, length);
        else memcpy(g_uhci_bounce, buffer, length);
    }
    unsigned int offset = 0;
    unsigned char next_toggle = toggle;
    for (unsigned int i = 0; i < count; ++i) {
        unsigned int amount = length - offset;
        if (amount > max_packet) amount = max_packet;
        ohci_td_t* td = &g_ohci_tds[i];
        td->control = ohci_td_control(pid, next_toggle, input);
        td->current_buffer = amount ? uhci_phys(g_uhci_bounce + offset) : 0;
        td->next = uhci_phys(i + 1u < count ? &g_ohci_tds[i + 1u] : &g_ohci_dummy);
        td->buffer_end = amount ? uhci_phys(g_uhci_bounce + offset + amount - 1u) : 0;
        offset += amount;
        next_toggle ^= 1;
    }
    g_ohci_dummy.control = 0;
    g_ohci_dummy.current_buffer = 0;
    g_ohci_dummy.next = 0;
    g_ohci_dummy.buffer_end = 0;
    g_ohci_ed.control = (address & 0x7Fu) | ((endpoint & 0x0Fu) << 7) |
                        (low_speed ? (1u << 13) : 0) |
                        ((unsigned int)max_packet << 16);
    g_ohci_ed.tail = uhci_phys(&g_ohci_dummy);
    g_ohci_ed.head = uhci_phys(&g_ohci_tds[0]);
    g_ohci_ed.next = 0;
    g_ohci_hcca.done_head = 0;

    unsigned int control = g_ohci_registers[OHCI_CONTROL / 4];
    unsigned int list_bit, head_register, command_bit;
    if (transfer_type == 1) {
        list_bit = OHCI_CONTROL_BLE; head_register = OHCI_BULK_HEAD; command_bit = 1u << 2;
    } else if (transfer_type == 2) {
        list_bit = OHCI_CONTROL_PLE; head_register = 0; command_bit = 0;
        for (unsigned int i = 0; i < 32; ++i) g_ohci_hcca.interrupt_table[i] = uhci_phys(&g_ohci_ed);
    } else {
        list_bit = OHCI_CONTROL_CLE; head_register = OHCI_CONTROL_HEAD; command_bit = 1u << 1;
    }
    if (head_register) g_ohci_registers[head_register / 4] = uhci_phys(&g_ohci_ed);
    g_ohci_registers[OHCI_INTERRUPT / 4] = 0xFFFFFFFFu;
    g_ohci_registers[OHCI_CONTROL / 4] = control | list_bit | OHCI_CONTROL_OPERATIONAL;
    if (command_bit) g_ohci_registers[OHCI_COMMAND / 4] = command_bit;

    unsigned int start = idt_get_millis();
    for (;;) {
        int pending = 0;
        for (unsigned int i = 0; i < count; ++i) {
            unsigned int condition = g_ohci_tds[i].control >> 28;
            if (condition == OHCI_CC_NOT_ACCESSED) pending = 1;
            else if (condition != OHCI_CC_NO_ERROR && !(input && condition == OHCI_CC_DATA_UNDERRUN)) {
                g_ohci_registers[OHCI_CONTROL / 4] &= ~list_bit;
                return -1;
            }
        }
        if (!pending) break;
        if ((unsigned int)(idt_get_millis() - start) > 100u) {
            g_ohci_registers[OHCI_CONTROL / 4] &= ~list_bit;
            return -1;
        }
        __asm__ volatile("pause");
    }
    g_ohci_registers[OHCI_CONTROL / 4] &= ~list_bit;
    if (head_register) g_ohci_registers[head_register / 4] = 0;

    offset = 0;
    unsigned int total = 0;
    for (unsigned int i = 0; i < count; ++i) {
        unsigned int requested = length - offset;
        if (requested > max_packet) requested = max_packet;
        unsigned int current = g_ohci_tds[i].current_buffer;
        unsigned int done = requested;
        if (current) {
            unsigned int end = g_ohci_tds[i].buffer_end;
            if (current > end || end - current + 1u > requested) return -1;
            done = requested - (end - current + 1u);
        }
        if (input && done) memcpy((unsigned char*)buffer + offset, g_uhci_bounce + offset, done);
        total += done;
        offset += requested;
        if (done < requested) break;
    }
    if (actual) *actual = (unsigned short)total;
    return 0;
}

static int ohci_bulk_transfer(unsigned char address, unsigned char endpoint, int input,
                              unsigned char* toggle, unsigned short max_packet,
                              void* buffer, unsigned short length, unsigned short* actual) {
    if (!toggle || !length || !max_packet) return -1;
    int result = ohci_transfer_chain(input ? 0x69 : 0xE1, address, endpoint, *toggle,
                                     0, buffer, length, max_packet, actual, 1);
    if (result == 0) {
        unsigned int transferred = actual ? *actual : length;
        unsigned int packets = transferred ? (transferred + max_packet - 1u) / max_packet : 1u;
        *toggle ^= (unsigned char)(packets & 1u);
    }
    return result;
}

static int ohci_reset_controller(usb_controller_t* controller) {
    unsigned int base;
    if (usb_mmio_bar0_base(controller, &base) != 0 ||
        paging_map_mmio_range(base, 0x1000u) != 0) return -1;
    controller->mmio_base = base;
    g_ohci_registers = (volatile unsigned int*)base;
    unsigned short command = pci_read_config_word(controller->pci.bus, controller->pci.slot,
                                                    controller->pci.func, PCI_COMMAND);
    pci_write_config_word(controller->pci.bus, controller->pci.slot, controller->pci.func,
                          PCI_COMMAND, (unsigned short)(command | 0x0006u));

    g_ohci_registers[OHCI_CONTROL / 4] = 0;
    g_ohci_registers[OHCI_COMMAND / 4] = OHCI_HCR;
    unsigned int start = idt_get_millis();
    while (g_ohci_registers[OHCI_COMMAND / 4] & OHCI_HCR) {
        if ((unsigned int)(idt_get_millis() - start) > 100u) return -1;
        __asm__ volatile("pause");
    }
    memset(&g_ohci_hcca, 0, sizeof(g_ohci_hcca));
    memset(&g_ohci_ed, 0, sizeof(g_ohci_ed));
    memset(g_ohci_tds, 0, sizeof(g_ohci_tds));
    g_ohci_registers[OHCI_HCCA / 4] = uhci_phys(&g_ohci_hcca);
    g_ohci_registers[OHCI_INTERRUPT / 4] = 0xFFFFFFFFu;
    g_ohci_registers[0x0C / 4] = 0xFFFFFFFFu;

    unsigned int frame_interval = 0x2EDF;
    unsigned int fsmps = ((frame_interval - 210u) * 6u) / 7u;
    g_ohci_registers[OHCI_FRAME_INTERVAL / 4] = frame_interval | (fsmps << 16);
    g_ohci_registers[OHCI_PERIODIC_START / 4] = (frame_interval * 9u) / 10u;
    g_ohci_registers[OHCI_LOW_SPEED_MAX / 4] = 0x0628u;
    controller->port_count = (unsigned char)(g_ohci_registers[OHCI_RH_DESC_A / 4] & 0xFFu);
    if (controller->port_count > 15) controller->port_count = 15;
    if (!controller->port_count) return -1;
    g_ohci_registers[OHCI_CONTROL / 4] = OHCI_CONTROL_OPERATIONAL;
    controller->initialized = 1;
    g_ohci_ready = 1;
    g_ohci_ports = controller->port_count;
    return 0;
}

static unsigned int usb_root_port_status(unsigned int port) {
    if (g_active_interface == 0x10u)
        return g_ohci_registers[(OHCI_RH_PORT_BASE + port * 4u) / 4u];
    if (g_active_interface == 0x20u)
        return g_ehci_registers[(EHCI_PORTSC_BASE + port * 4u) / 4u];
    if (g_active_interface == 0x30u) return xhci_port_status(port);
    return usb_inw((unsigned short)(g_uhci_base + UHCI_PORTSC1 + port * 2u));
}

static int usb_root_port_low_speed(unsigned int port) {
    unsigned int status = usb_root_port_status(port);
    if (g_active_interface == 0x10u) return (status & (1u << 9)) != 0;
    if (g_active_interface == 0x20u) return (status & (1u << 26)) != 0;
    if (g_active_interface == 0x30u) return xhci_port_speed(port) == 1u;
    return (status & 0x0100u) != 0;
}

static int usb_bulk_packet_valid(unsigned int port, int low_speed, unsigned short packet) {
    if (!packet) return 0;
    if (g_active_interface == 0x20u) return packet == 512u;
    if (g_active_interface == 0x30u) {
        unsigned int speed = xhci_port_speed(port);
        if (speed == 1u) return 0; /* Low-speed devices have no bulk endpoints. */
        if (speed == 2u) return packet == 8u || packet == 16u ||
                                 packet == 32u || packet == 64u;
        if (speed == 3u) return packet == 512u;
        if (speed >= 4u) return packet == 1024u;
        return 0;
    }
    if (low_speed) return 0;
    /* UHCI and OHCI handle full-speed bulk endpoints. */
    return packet == 8u || packet == 16u || packet == 32u || packet == 64u;
}

static unsigned int usb_root_port_count(void) {
    if (g_active_interface == 0x10u) return g_ohci_ports;
    if (g_active_interface == 0x20u) return g_ehci_ports;
    if (g_active_interface == 0x30u) return g_xhci_ready ? xhci_port_count() : 0;
    return 2u;
}

static void ohci_reset_root_port(unsigned int port) {
    volatile unsigned int* reg = &g_ohci_registers[(OHCI_RH_PORT_BASE + port * 4u) / 4u];
    unsigned int status = *reg;
    if (!(status & 1u)) return;
    *reg = (1u << 16) | (1u << 17) | (1u << 18) | (1u << 19) | (1u << 20);
    *reg = 1u << 4;
    msecwait(50);
    status = *reg;
    if (status & (1u << 20)) *reg = 1u << 20;
    status = *reg;
    if ((status & 1u) && !(status & 2u)) *reg = 2u;
    msecwait(10);
}

static int ehci_transfer(unsigned char pid, unsigned char address, unsigned char endpoint,
                         unsigned char toggle, void* buffer, unsigned short length,
                         unsigned short max_packet, unsigned short* actual,
                         unsigned int transfer_type) {
    if (!g_ehci_ready || !g_ehci_registers || !max_packet || max_packet > 512 ||
        length > sizeof(g_uhci_bounce) || (length && !buffer)) return -1;
    int input = pid == 0x69;
    if (length) {
        if (input) memset(g_uhci_bounce, 0, length);
        else memcpy(g_uhci_bounce, buffer, length);
    }
    unsigned int pid_code = pid == 0x2D ? 2u : (input ? 1u : 0u);
    g_ehci_qtd.next = 1;
    g_ehci_qtd.alternate = 1;
    g_ehci_qtd.token = EHCI_QTD_ACTIVE | (3u << 10) | (pid_code << 8) |
                       ((unsigned int)length << 16) | ((unsigned int)(toggle & 1u) << 31) |
                       (1u << 15);
    unsigned int dma = length ? uhci_phys(g_uhci_bounce) : 0;
    for (unsigned int page = 0; page < 5; ++page)
        g_ehci_qtd.buffer[page] = dma ? ((dma + page * 4096u) & 0xFFFFF000u) : 0;
    g_ehci_qh.horizontal = uhci_phys(&g_ehci_qh) | 2u;
    g_ehci_qh.endpoint = (address & 0x7Fu) | ((unsigned int)(endpoint & 0x0Fu) << 8) |
                         (2u << 12) | (1u << 14) | ((unsigned int)max_packet << 16) |
                         (transfer_type == 0 ? (1u << 27) : 0);
    g_ehci_qh.capabilities = transfer_type == 2 ? 1u : (1u << 30);
    g_ehci_qh.current_qtd = 0;
    g_ehci_qh.next_qtd = uhci_phys(&g_ehci_qtd);
    g_ehci_qh.alternate_qtd = 1;
    g_ehci_qh.token = 0;
    for (unsigned int i = 0; i < 5; ++i) g_ehci_qh.buffer[i] = 0;

    unsigned int command = g_ehci_registers[EHCI_USBCMD / 4];
    unsigned int schedule_bit, status_bit;
    if (transfer_type == 2) {
        for (unsigned int frame = 0; frame < 1024; ++frame)
            g_ehci_frames[frame] = uhci_phys(&g_ehci_qh) | 2u;
        g_ehci_registers[EHCI_PERIODICBASE / 4] = uhci_phys(g_ehci_frames);
        schedule_bit = EHCI_CMD_PSE;
        status_bit = EHCI_STS_PSS;
    } else {
        g_ehci_registers[EHCI_ASYNCBASE / 4] = uhci_phys(&g_ehci_qh);
        schedule_bit = EHCI_CMD_ASE;
        status_bit = EHCI_STS_ASS;
    }
    g_ehci_registers[EHCI_USBSTS / 4] = 0x3Fu;
    g_ehci_registers[EHCI_USBCMD / 4] = command | EHCI_CMD_RUN | schedule_bit;
    unsigned int start = idt_get_millis();
    while (!(g_ehci_registers[EHCI_USBSTS / 4] & status_bit)) {
        if ((unsigned int)(idt_get_millis() - start) > 100u) return -1;
        __asm__ volatile("pause");
    }
    while (g_ehci_qtd.token & EHCI_QTD_ACTIVE) {
        if ((g_ehci_qtd.token & 0x7Eu) ||
            (unsigned int)(idt_get_millis() - start) > 100u) {
            g_ehci_registers[EHCI_USBCMD / 4] &= ~schedule_bit;
            return -1;
        }
        __asm__ volatile("pause");
    }
    unsigned int token = g_ehci_qtd.token;
    g_ehci_registers[EHCI_USBCMD / 4] &= ~schedule_bit;
    start = idt_get_millis();
    while (g_ehci_registers[EHCI_USBSTS / 4] & status_bit) {
        if ((unsigned int)(idt_get_millis() - start) > 100u) return -1;
        __asm__ volatile("pause");
    }
    if (transfer_type == 2)
        for (unsigned int frame = 0; frame < 1024; ++frame) g_ehci_frames[frame] = 1u;
    if (token & 0x7Eu) return -1;
    unsigned int residual = (token >> 16) & 0x7FFFu;
    unsigned int transferred = length - residual;
    if (transferred > length) return -1;
    if (actual) *actual = (unsigned short)transferred;
    if (input && transferred) memcpy(buffer, g_uhci_bounce, transferred);
    return 0;
}

static int ehci_bulk_transfer(unsigned char address, unsigned char endpoint, int input,
                              unsigned char* toggle, unsigned short max_packet,
                              void* buffer, unsigned short length, unsigned short* actual) {
    if (!toggle || !length || !max_packet) return -1;
    int result = ehci_transfer(input ? 0x69 : 0xE1, address, endpoint, *toggle,
                              buffer, length, max_packet, actual, 1);
    if (result == 0) {
        unsigned int transferred = actual ? *actual : length;
        unsigned int packets = transferred ? (transferred + max_packet - 1u) / max_packet : 1u;
        *toggle ^= (unsigned char)(packets & 1u);
    }
    return result;
}

static int ehci_interrupt_transfer(unsigned char address, unsigned char endpoint,
                                   unsigned char toggle, unsigned char max_packet,
                                   void* buffer, unsigned short length,
                                   unsigned short* actual) {
    return ehci_transfer(0x69, address, endpoint, toggle, buffer, length,
                         max_packet, actual, 2);
}

static int ehci_reset_controller(usb_controller_t* controller) {
    unsigned int cap_base;
    if (usb_mmio_bar0_base(controller, &cap_base) != 0 ||
        paging_map_mmio_range(cap_base, 0x1000u) != 0) return -1;
    volatile unsigned char* capabilities = (volatile unsigned char*)cap_base;
    unsigned char cap_length = capabilities[0];
    if (cap_length < 0x10 || cap_length > 0x40) return -1;
    /* Take ownership from firmware through EHCI's optional PCI legacy cap. */
    unsigned int hccparams = *(volatile unsigned int*)(cap_base + 8u);
    unsigned int legacy = (hccparams >> 8) & 0xFFu;
    if (legacy >= 0x40u && legacy <= 0xF0u) {
        unsigned int legacy_header = pci_read_config_dword(controller->pci.bus,
            controller->pci.slot, controller->pci.func, (unsigned char)legacy);
        if ((legacy_header & 0xFFu) == 1u) {
            pci_write_config_dword(controller->pci.bus, controller->pci.slot,
                controller->pci.func, (unsigned char)legacy, legacy_header | (1u << 24));
            unsigned int start = idt_get_millis();
            while (pci_read_config_dword(controller->pci.bus, controller->pci.slot,
                       controller->pci.func, (unsigned char)legacy) & (1u << 16)) {
                if ((unsigned int)(idt_get_millis() - start) > 500u) break;
                __asm__ volatile("pause");
            }
            pci_write_config_dword(controller->pci.bus, controller->pci.slot,
                controller->pci.func, (unsigned char)(legacy + 4u), 0);
        }
    }
    unsigned int op_base = cap_base + cap_length;
    if (paging_map_mmio_range(op_base, 0x1000u) != 0) return -1;
    controller->mmio_base = op_base;
    g_ehci_registers = (volatile unsigned int*)op_base;
    unsigned int hcsparams = *(volatile unsigned int*)(cap_base + 4);
    controller->port_count = (unsigned char)(hcsparams & 0x0Fu);
    if (!controller->port_count) return -1;
    unsigned short command = pci_read_config_word(controller->pci.bus, controller->pci.slot,
                                                    controller->pci.func, PCI_COMMAND);
    pci_write_config_word(controller->pci.bus, controller->pci.slot, controller->pci.func,
                          PCI_COMMAND, (unsigned short)(command | 0x0006u));
    g_ehci_registers[EHCI_USBCMD / 4] &= ~EHCI_CMD_RUN;
    unsigned int start = idt_get_millis();
    while (!(g_ehci_registers[EHCI_USBSTS / 4] & EHCI_STS_HALTED)) {
        if ((unsigned int)(idt_get_millis() - start) > 100u) return -1;
        __asm__ volatile("pause");
    }
    g_ehci_registers[EHCI_USBCMD / 4] |= EHCI_CMD_RESET;
    start = idt_get_millis();
    while (g_ehci_registers[EHCI_USBCMD / 4] & EHCI_CMD_RESET) {
        if ((unsigned int)(idt_get_millis() - start) > 100u) return -1;
        __asm__ volatile("pause");
    }
    memset(&g_ehci_qh, 0, sizeof(g_ehci_qh));
    memset(&g_ehci_qtd, 0, sizeof(g_ehci_qtd));
    for (unsigned int frame = 0; frame < 1024; ++frame) g_ehci_frames[frame] = 1u;
    g_ehci_registers[EHCI_CTRLDSSEGMENT / 4] = 0;
    g_ehci_registers[EHCI_PERIODICBASE / 4] = uhci_phys(g_ehci_frames);
    g_ehci_registers[EHCI_ASYNCBASE / 4] = uhci_phys(&g_ehci_qh);
    g_ehci_registers[EHCI_CONFIGFLAG / 4] = 1;
    g_ehci_registers[EHCI_USBINTR / 4] = 0;
    g_ehci_registers[EHCI_USBSTS / 4] = 0x3Fu;
    g_ehci_registers[EHCI_USBCMD / 4] = EHCI_CMD_RUN;
    start = idt_get_millis();
    while (g_ehci_registers[EHCI_USBSTS / 4] & EHCI_STS_HALTED) {
        if ((unsigned int)(idt_get_millis() - start) > 100u) return -1;
        __asm__ volatile("pause");
    }
    controller->initialized = 1;
    g_ehci_ready = 1;
    g_ehci_ports = controller->port_count;
    return 0;
}

static void ehci_reset_root_port(unsigned int port) {
    volatile unsigned int* reg = &g_ehci_registers[(EHCI_PORTSC_BASE + port * 4u) / 4u];
    unsigned int status = *reg;
    if (!(status & 1u)) return;
    unsigned int preserve = status & ((1u << 12) | (1u << 13));
    *reg = preserve | (1u << 8);
    msecwait(50);
    *reg = preserve;
    msecwait(10);
    status = *reg;
    /* Full- and low-speed devices are handed to an OHCI/UHCI companion. */
    if ((status & 1u) && !(status & 4u)) *reg = preserve | (1u << 13);
}

static int uhci_control(unsigned char address, unsigned char low_speed,
                        unsigned char max_packet, const unsigned char setup[8],
                        unsigned char* data, unsigned short length) {
    (void)max_packet;
    if (!setup || length > sizeof(g_uhci_control)) return -1;
    if (g_active_interface == 0x30u)
        return xhci_control_transfer(address, setup, data, length, 0);
    unsigned short setup_packet = 8;
    if (uhci_transfer(0x2D, address, 0, 0, low_speed, (void*)setup,
                      setup_packet, 8, 0) != 0) return -1;
    unsigned short done = 0;
    unsigned char toggle = 1;
    unsigned char data_pid = (setup[0] & 0x80) ? 0x69 : 0xE1;
    while (done < length) {
        unsigned short amount = (unsigned short)(length - done);
        if (amount > max_packet) amount = max_packet;
        unsigned short actual = 0;
        if (uhci_transfer(data_pid, address, 0, toggle, low_speed,
                          data + done, amount, max_packet, &actual) != 0) return -1;
        done = (unsigned short)(done + actual);
        toggle ^= 1;
        if (actual < amount) break;
    }
    unsigned char status_pid = (setup[0] & 0x80) ? 0xE1 : 0x69;
    if (uhci_transfer(status_pid, address, 0, 1, low_speed, 0, 0, max_packet, 0) != 0) return -1;
    return 0;
}

static int usb_get_descriptor(unsigned char address, unsigned char low_speed,
                              unsigned char max_packet, unsigned char type,
                              unsigned char index, unsigned char* data, unsigned short length) {
    unsigned char setup[8] = { 0x80, 6, index, type, 0, 0, (unsigned char)length,
                               (unsigned char)(length >> 8) };
    return uhci_control(address, low_speed, max_packet, setup, data, length);
}

static int usb_set_address(unsigned char address, unsigned char low_speed,
                           unsigned char max_packet, unsigned char new_address) {
    if (g_active_interface == 0x30u) {
        (void)address;
        (void)low_speed;
        (void)max_packet;
        xhci_bind_address(new_address);
        return xhci_slot_for_address(new_address) ? 0 : -1;
    }
    unsigned char setup[8] = { 0x00, 5, new_address, 0, 0, 0, 0, 0 };
    return uhci_control(address, low_speed, max_packet, setup, 0, 0);
}

static int usb_set_configuration(unsigned char address, unsigned char low_speed,
                                 unsigned char max_packet, unsigned char configuration) {
    unsigned char setup[8] = { 0x00, 9, configuration, 0, 0, 0, 0, 0 };
    return uhci_control(address, low_speed, max_packet, setup, 0, 0);
}

static int usb_set_boot_protocol(unsigned char address, unsigned char low_speed,
                                 unsigned char max_packet, unsigned char interface_number) {
    unsigned char setup[8] = { 0x21, 0x0B, 0, 0, interface_number, 0, 0, 0 };
    return uhci_control(address, low_speed, max_packet, setup, 0, 0);
}

static int usb_clear_endpoint_halt(unsigned char address, unsigned char low_speed,
                                   unsigned char max_packet, unsigned char endpoint) {
    unsigned char setup[8] = { 0x02, 1, 0, 0, endpoint, 0, 0, 0 };
    return uhci_control(address, low_speed, max_packet, setup, 0, 0);
}

static int usb_mass_storage_reset(usb_storage_t* device) {
    unsigned char setup[8] = { 0x21, 0xFF, 0, 0, device->interface_number, 0, 0, 0 };
    if (uhci_control(device->address, 0, device->control_packet, setup, 0, 0) != 0) return -1;
    (void)usb_clear_endpoint_halt(device->address, 0, device->control_packet,
                                  (unsigned char)(0x80 | device->endpoint_in));
    (void)usb_clear_endpoint_halt(device->address, 0, device->control_packet, device->endpoint_out);
    if (device->transport_index >= g_usb_msc_transport_count) return -1;
    g_usb_msc_transports[device->transport_index].toggle_in = 0;
    g_usb_msc_transports[device->transport_index].toggle_out = 0;
    return 0;
}

static unsigned char usb_mass_storage_max_lun(unsigned char address,
                                              unsigned char max_packet,
                                              unsigned char interface_number) {
    unsigned char setup[8] = {0xA1, 0xFE, 0, 0, interface_number, 0, 1, 0};
    unsigned char max_lun = 0;
    /* A STALL is the USB Mass Storage convention for a single-LUN device. */
    if (uhci_control(address, 0, max_packet, setup, &max_lun, 1) != 0 || max_lun > 15u)
        return 0;
    return max_lun;
}

static int uhci_bulk_transfer(unsigned char address, unsigned char endpoint, int input,
                              unsigned char* toggle, unsigned short max_packet,
                              void* buffer, unsigned short length, unsigned short* actual,
                              unsigned char low_speed) {
    if (!toggle || !buffer || !length || !max_packet) return -1;
    if (g_active_interface == 0x10u) {
        int result = ohci_bulk_transfer(address, endpoint, input, toggle, max_packet,
                                        buffer, length, actual);
        (void)low_speed;
        return result;
    }
    if (g_active_interface == 0x20u) {
        (void)low_speed;
        return ehci_bulk_transfer(address, endpoint, input, toggle, max_packet,
                                  buffer, length, actual);
    }
    if (g_active_interface == 0x30u) {
        (void)low_speed;
        (void)toggle;
        return xhci_transfer(address, (unsigned char)(endpoint | (input ? 0x80u : 0u)),
                             input, buffer, length, actual);
    }
    if (!g_uhci_ready || !toggle || !buffer || !length || length > 4096 ||
        !max_packet || max_packet > 64) return -1;
    unsigned int count = (length + max_packet - 1u) / max_packet;
    if (count > sizeof(g_uhci_tds) / sizeof(g_uhci_tds[0])) return -1;
    unsigned char pid = input ? 0x69 : 0xE1;
    unsigned int offset = 0;
    unsigned char next_toggle = *toggle;
    for (unsigned int i = 0; i < count; ++i) {
        unsigned short amount = (unsigned short)(length - offset);
        if (amount > max_packet) amount = max_packet;
        if (!input) memcpy(g_uhci_bounce + offset, (unsigned char*)buffer + offset, amount);
        g_uhci_tds[i].link = i + 1u < count
            ? uhci_phys(&g_uhci_tds[i + 1u]) | UHCI_PTR_DEPTH : UHCI_PTR_TERM;
        g_uhci_tds[i].status = (low_speed ? (1u << 26) : 0) | (3u << 27) | UHCI_TD_ACTIVE;
        g_uhci_tds[i].token = (unsigned int)pid | ((unsigned int)(address & 0x7Fu) << 8) |
                              ((unsigned int)(endpoint & 0x0Fu) << 15) |
                              ((unsigned int)(next_toggle & 1u) << 19) |
                              ((unsigned int)(amount - 1u) << 21);
        g_uhci_tds[i].buffer = uhci_phys(g_uhci_bounce + offset);
        offset += amount;
        next_toggle ^= 1;
    }
    g_uhci_qh.head = UHCI_PTR_TERM;
    g_uhci_qh.element = uhci_phys(&g_uhci_tds[0]);
    for (unsigned int i = 0; i < 1024; ++i)
        g_uhci_frames[i] = uhci_phys(&g_uhci_qh) | UHCI_PTR_QH;
    usb_outw(g_uhci_base + UHCI_CMD, 0);
    usb_outw(g_uhci_base + UHCI_STS, 0x003F);
    usb_outl(g_uhci_base + UHCI_FLBASE, uhci_phys(g_uhci_frames));
    usb_outw(g_uhci_base + UHCI_FRNUM, 0);
    usb_outw(g_uhci_base + UHCI_CMD, UHCI_CMD_RUN | UHCI_CMD_CF | UHCI_CMD_MAXP);

    unsigned int start = idt_get_millis();
    for (;;) {
        int pending = 0;
        for (unsigned int i = 0; i < count; ++i) {
            unsigned int status = g_uhci_tds[i].status;
            if (status & UHCI_TD_ERROR) {
                usb_outw(g_uhci_base + UHCI_CMD, 0);
                return -1;
            }
            if (status & UHCI_TD_ACTIVE) pending = 1;
        }
        if (!pending) break;
        if ((unsigned int)(idt_get_millis() - start) > 100u) {
            usb_outw(g_uhci_base + UHCI_CMD, 0);
            return -1;
        }
        __asm__ volatile("pause");
    }
    usb_outw(g_uhci_base + UHCI_CMD, 0);

    offset = 0;
    unsigned int total = 0;
    for (unsigned int i = 0; i < count; ++i) {
        unsigned int encoded = g_uhci_tds[i].status & 0x7FFu;
        unsigned int actual = encoded == 0x7FFu ? 0 : encoded + 1u;
        unsigned int requested = length - offset;
        if (requested > max_packet) requested = max_packet;
        if (actual > requested) return -1;
        if (input && actual) memcpy((unsigned char*)buffer + offset, g_uhci_bounce + offset, actual);
        offset += actual;
        total += actual;
        *toggle ^= 1;
        if (actual < requested) break;
    }
    if (actual) *actual = (unsigned short)total;
    return 0;
}

static int usb_msc_command(usb_storage_t* device, const unsigned char* cdb,
                           unsigned char cdb_length, int input, void* data,
                           unsigned int data_length) {
    if (device->transport_index >= g_usb_msc_transport_count) return -1;
    usb_msc_transport_t* transport = &g_usb_msc_transports[device->transport_index];
    unsigned char cbw[31];
    unsigned char csw[13];
    memset(cbw, 0, sizeof(cbw));
    memset(csw, 0, sizeof(csw));
    unsigned int tag = ++device->tag;
    cbw[0] = 0x55; cbw[1] = 0x53; cbw[2] = 0x42; cbw[3] = 0x43;
    cbw[4] = (unsigned char)tag; cbw[5] = (unsigned char)(tag >> 8);
    cbw[6] = (unsigned char)(tag >> 16); cbw[7] = (unsigned char)(tag >> 24);
    cbw[8] = (unsigned char)data_length; cbw[9] = (unsigned char)(data_length >> 8);
    cbw[10] = (unsigned char)(data_length >> 16); cbw[11] = (unsigned char)(data_length >> 24);
    cbw[12] = input && data_length ? 0x80 : 0;
    cbw[13] = device->lun;
    cbw[14] = cdb_length;
    if (cdb_length > 16) return -1;
    memcpy(cbw + 15, cdb, cdb_length);

    unsigned short transferred = 0;
    if (uhci_bulk_transfer(device->address, device->endpoint_out, 0,
                           &transport->toggle_out, device->max_packet_out,
                           cbw, sizeof(cbw), &transferred, 0) != 0 ||
        transferred != sizeof(cbw)) return -1;
    unsigned int done = 0;
    while (done < data_length) {
        unsigned short amount = (unsigned short)(data_length - done);
        unsigned int transfer_limit = (input ? device->max_packet_in : device->max_packet_out) * 64u;
        if (transfer_limit > sizeof(g_uhci_bounce)) transfer_limit = sizeof(g_uhci_bounce);
        if (amount > transfer_limit) amount = (unsigned short)transfer_limit;
        unsigned char* cursor = (unsigned char*)data + done;
        unsigned char* toggle = input ? &transport->toggle_in : &transport->toggle_out;
        unsigned char endpoint = input ? device->endpoint_in : device->endpoint_out;
        unsigned short packet = input ? device->max_packet_in : device->max_packet_out;
        transferred = 0;
        if (uhci_bulk_transfer(device->address, endpoint, input, toggle, packet,
                               cursor, amount, &transferred, 0) != 0 || transferred > amount)
            return -1;
        done += transferred;
        if (transferred < amount) break;
    }
    transferred = 0;
    if (uhci_bulk_transfer(device->address, device->endpoint_in, 1,
                           &transport->toggle_in, device->max_packet_in,
                           csw, sizeof(csw), &transferred, 0) != 0 ||
        transferred != sizeof(csw)) return -1;
    if (csw[0] != 0x55 || csw[1] != 0x53 || csw[2] != 0x42 || csw[3] != 0x53 ||
        csw[4] != (unsigned char)tag || csw[5] != (unsigned char)(tag >> 8) ||
        csw[6] != (unsigned char)(tag >> 16) || csw[7] != (unsigned char)(tag >> 24)) return -1;
    unsigned int residue = (unsigned int)csw[8] | ((unsigned int)csw[9] << 8) |
                           ((unsigned int)csw[10] << 16) | ((unsigned int)csw[11] << 24);
    if (residue > data_length || done != data_length - residue) return -1;
    if (csw[12] != 0) return -2;
    return 0;
}

static int usb_msc_initialize(usb_storage_t* device) {
    if (usb_mass_storage_reset(device) != 0) return -1;
    unsigned char inquiry_cdb[6] = {0x12, 0, 0, 0, 36, 0};
    unsigned char inquiry[36];
    if (usb_msc_command(device, inquiry_cdb, sizeof(inquiry_cdb), 1, inquiry, sizeof(inquiry)) != 0)
        return -1;
    unsigned char ready_cdb[6] = {0};
    unsigned char sense_cdb[6] = {0x03, 0, 0, 0, 18, 0};
    unsigned char sense[18];
    int ready = 0;
    for (unsigned int attempt = 0; attempt < 10u; ++attempt) {
        int result = usb_msc_command(device, ready_cdb, sizeof(ready_cdb), 0, 0, 0);
        if (result == 0) {
            ready = 1;
            break;
        }
        if (result != -2 ||
            usb_msc_command(device, sense_cdb, sizeof(sense_cdb), 1,
                            sense, sizeof(sense)) != 0)
            return -1;
        unsigned char response = sense[0] & 0x7Fu;
        unsigned char sense_key = sense[2] & 0x0Fu;
        unsigned char asc = sense[12];
        if ((response != 0x70u && response != 0x71u) ||
            (sense_key != 0x06u && !(sense_key == 0x02u && asc == 0x04u)))
            return -1;
        msecwait(100);
    }
    if (!ready) return -1;
    unsigned char capacity_cdb[10] = {0x25};
    unsigned char capacity[8];
    if (usb_msc_command(device, capacity_cdb, sizeof(capacity_cdb), 1, capacity, sizeof(capacity)) != 0)
        return -1;
    device->last_lba = ((unsigned int)capacity[0] << 24) | ((unsigned int)capacity[1] << 16) |
                       ((unsigned int)capacity[2] << 8) | capacity[3];
    unsigned int sector_size = ((unsigned int)capacity[4] << 24) | ((unsigned int)capacity[5] << 16) |
                              ((unsigned int)capacity[6] << 8) | capacity[7];
    if (device->last_lba == 0xFFFFFFFFu) {
        /* READ CAPACITY(10) uses all-ones as the sentinel for larger media.
         * The kernel block interface has 32-bit LBAs, so expose the first
         * 2 TiB when READ CAPACITY(16) is supported. */
        unsigned char capacity16_cdb[16] = {0x9E, 0x10};
        unsigned char capacity16[32];
        capacity16_cdb[13] = sizeof(capacity16);
        if (usb_msc_command(device, capacity16_cdb, sizeof(capacity16_cdb), 1,
                            capacity16, sizeof(capacity16)) != 0)
            return -1;
        unsigned int last_lba_high = ((unsigned int)capacity16[0] << 24) |
                                     ((unsigned int)capacity16[1] << 16) |
                                     ((unsigned int)capacity16[2] << 8) | capacity16[3];
        device->last_lba = last_lba_high ? 0xFFFFFFFFu :
            (((unsigned int)capacity16[4] << 24) | ((unsigned int)capacity16[5] << 16) |
             ((unsigned int)capacity16[6] << 8) | capacity16[7]);
        sector_size = ((unsigned int)capacity16[8] << 24) |
                      ((unsigned int)capacity16[9] << 16) |
                      ((unsigned int)capacity16[10] << 8) | capacity16[11];
    }
    if (sector_size != 512) return -1;
    device->valid = 1;
    print_string("[USB] USB mass-storage device initialized (512-byte sectors).\n");
    return 0;
}

static int uhci_reset_controller(const usb_controller_t* controller) {
    unsigned int bar = pci_read_config_dword(controller->pci.bus, controller->pci.slot,
                                               controller->pci.func, PCI_BAR4);
    if (!(bar & 1u)) return -1;
    unsigned short base = (unsigned short)(bar & 0xFFFEu);
    if (!base || base > 0xFFF0u) return -1;

    unsigned short command = pci_read_config_word(controller->pci.bus, controller->pci.slot,
                                                    controller->pci.func, PCI_COMMAND);
    pci_write_config_word(controller->pci.bus, controller->pci.slot, controller->pci.func,
                          PCI_COMMAND, (unsigned short)(command | 0x0005u));
    usb_outw(base + UHCI_CMD, 0x0004u);
    msecwait(50);
    usb_outw(base + UHCI_CMD, 0);
    msecwait(10);
    usb_outw(base + UHCI_STS, 0x003Fu);
    usb_outw(base + UHCI_INTR, 0);
    g_uhci_qh.head = UHCI_PTR_TERM;
    g_uhci_qh.element = UHCI_PTR_TERM;
    for (unsigned int frame = 0; frame < 1024; ++frame)
        g_uhci_frames[frame] = UHCI_PTR_TERM;
    usb_outl(base + UHCI_FLBASE, uhci_phys(g_uhci_frames));
    usb_outw(base + UHCI_FRNUM, 0);
    usb_outb(base + UHCI_SOFMOD, 0x40);
    usb_outw(base + UHCI_CMD, UHCI_CMD_RUN | UHCI_CMD_CF | UHCI_CMD_MAXP);

    g_uhci_base = base;
    g_uhci_ready = 1;
    return 0;
}

static void usb_reset_root_port(unsigned int port) {
    if (g_active_interface == 0x10u) {
        ohci_reset_root_port(port);
        return;
    }
    if (g_active_interface == 0x20u) {
        ehci_reset_root_port(port);
        return;
    }
    if (g_active_interface == 0x30u) {
        (void)xhci_reset_port(port);
        return;
    }
    unsigned short reg = (unsigned short)(g_uhci_base + UHCI_PORTSC1 + port * 2u);
    unsigned short status = usb_inw(reg);
    if (!(status & 1u)) return;
    usb_outw(reg, 0x0002u | 0x0008u);
    usb_outw(reg, 0x0200u);
    msecwait(50);
    usb_outw(reg, 0);
    msecwait(10);
    status = usb_inw(reg);
    if ((status & 1u) && !(status & 4u)) usb_outw(reg, 4u);
    msecwait(10);
}

static unsigned char usb_hid_to_set1(unsigned char key) {
    if (key >= 0x04 && key <= 0x1D) {
        static const unsigned char letters[26] = {
            0x1E,0x30,0x2E,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,
            0x31,0x18,0x19,0x10,0x13,0x1F,0x14,0x16,0x2F,0x11,0x2D,0x15,0x2C
        };
        return letters[key - 0x04];
    }
    if (key >= 0x1E && key <= 0x27) {
        static const unsigned char numbers[10] = { 0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B };
        return numbers[key - 0x1E];
    }
    switch (key) {
        case 0x28: return 0x1C; case 0x29: return 0x01; case 0x2A: return 0x0E;
        case 0x2B: return 0x0F; case 0x2C: return 0x39; case 0x2D: return 0x0C;
        case 0x2E: return 0x0D; case 0x2F: return 0x1A; case 0x30: return 0x1B;
        case 0x31: return 0x2B; case 0x33: return 0x27; case 0x34: return 0x28;
        case 0x35: return 0x29; case 0x36: return 0x33; case 0x37: return 0x34;
        case 0x38: return 0x35; case 0x39: return 0x3A;
        case 0x3A: return 0x3B; case 0x3B: return 0x3C; case 0x3C: return 0x3D;
        case 0x3D: return 0x3E; case 0x3E: return 0x3F; case 0x3F: return 0x40;
        case 0x40: return 0x41; case 0x41: return 0x42; case 0x42: return 0x43;
        case 0x43: return 0x44; case 0x44: return 0x57; case 0x45: return 0x58;
        case 0x4F: return 0x4D; case 0x50: return 0x4B; case 0x51: return 0x50;
        case 0x52: return 0x48; case 0x4A: return 0x47; case 0x4D: return 0x4F;
        case 0x4B: return 0x49; case 0x4E: return 0x51; case 0x4C: return 0x53;
        default: return 0;
    }
}

static void usb_emit_key(unsigned char key, int pressed) {
    unsigned char scan = usb_hid_to_set1(key);
    if (scan) keyboard_irq_inject_scancode((unsigned char)(scan | (pressed ? 0 : 0x80)));
}

static void usb_process_keyboard_report(usb_keyboard_t* keyboard, const unsigned char* report) {
    unsigned char old[8];
    unsigned int now = idt_get_millis();
    memcpy(old, keyboard->previous, sizeof(old));
    /* Modifier state is kept in byte zero; compare against this device's report. */
    for (unsigned int bit = 0; bit < 8; ++bit) {
        unsigned char mask = (unsigned char)(1u << bit);
        int was = (old[0] & mask) != 0, now = (report[0] & mask) != 0;
        if (was == now) continue;
        static const unsigned char mod_scans[8] = {0x1D,0x2A,0x38,0x5B,0x1D,0x36,0x38,0x5C};
        if (bit == 3 || bit == 7) keyboard_irq_inject_scancode(0xE0);
        keyboard_irq_inject_scancode((unsigned char)(mod_scans[bit] | (now ? 0 : 0x80)));
    }
    for (unsigned int i = 2; i < 8; ++i) {
        unsigned char key = old[i];
        if (!key || key < 4) continue;
        int still_down = 0;
        for (unsigned int j = 2; j < 8; ++j) if (report[j] == key) still_down = 1;
        if (!still_down) {
            usb_emit_key(key, 0);
            keyboard->repeat_at[key] = 0;
        }
    }
    for (unsigned int i = 2; i < 8; ++i) {
        unsigned char key = report[i];
        if (!key || key < 4) continue;
        int was_down = 0;
        for (unsigned int j = 2; j < 8; ++j) if (old[j] == key) was_down = 1;
        if (!was_down) {
            usb_emit_key(key, 1);
            /* Lock keys toggle on make and must never typematically repeat. */
            if (key != 0x39u && key != 0x53u) {
                keyboard->repeat_at[key] = now + USB_KEY_REPEAT_DELAY_MS;
            }
        }
    }
    memcpy(keyboard->previous, report, 8);
}

static void usb_repeat_held_keys(usb_keyboard_t* keyboard, unsigned int now) {
    for (unsigned int i = 2; i < 8; ++i) {
        unsigned char key = keyboard->previous[i];
        if (!key || key < 4 || key == 0x39u || key == 0x53u ||
            !keyboard->repeat_at[key]) continue;
        if ((int)(now - keyboard->repeat_at[key]) >= 0) {
            usb_emit_key(key, 1);
            keyboard->repeat_at[key] = now + USB_KEY_REPEAT_RATE_MS;
        }
    }
}

static void usb_try_enumerate_device(unsigned int port) {
    if ((g_usb_keyboard_count >= USB_MAX_KEYBOARDS && g_usb_storage_count >= USB_MAX_STORAGE) ||
        g_next_address > 127) return;
    unsigned int port_status = usb_root_port_status(port);
    if (!(port_status & 1u)) return;
    /* EHCI owns only high-speed devices; companion controllers own FS/LS. */
    if (g_active_interface == 0x20u && !(port_status & 4u)) return;
    unsigned char xhci_slot_active = 0;
    unsigned char address = 0;
#define USB_ENUM_ABORT() do { \
        if (xhci_slot_active) (void)xhci_release_device(address); \
        return; \
    } while (0)
    if (g_active_interface == 0x30u && xhci_prepare_port(port) != 0) return;
    if (g_active_interface == 0x30u) xhci_slot_active = 1;
    unsigned char low_speed = (unsigned char)usb_root_port_low_speed(port);
    unsigned char max_packet = 8;
    unsigned char descriptor[18];
    memset(descriptor, 0, sizeof(descriptor));
    if (usb_get_descriptor(0, low_speed, 8, 1, 0, descriptor, sizeof(descriptor)) != 0 ||
        descriptor[0] < 18 || descriptor[1] != 1) USB_ENUM_ABORT();
    if (g_active_interface == 0x30u) {
        unsigned int speed = xhci_port_speed(port);
        unsigned char ep0_size = descriptor[7];
        if ((speed == 1u && ep0_size != 8u) ||
            (speed == 2u && ep0_size != 8u && ep0_size != 16u &&
             ep0_size != 32u && ep0_size != 64u) ||
            (speed == 3u && ep0_size != 64u) ||
            (speed >= 4u && ep0_size != 9u)) USB_ENUM_ABORT();
    }
    if (descriptor[7] >= 8 && descriptor[7] <= 64) max_packet = descriptor[7];
    address = (unsigned char)g_next_address++;
    if (usb_set_address(0, low_speed, max_packet, address) != 0) USB_ENUM_ABORT();
    if (g_active_interface == 0x30u) {
        unsigned short ep0_packet = descriptor[7];
        if (xhci_port_speed(port) >= 4u && descriptor[7] < 16u)
            ep0_packet = (unsigned short)(1u << descriptor[7]);
        if (xhci_update_ep0_packet(address, ep0_packet) != 0) USB_ENUM_ABORT();
    }
    msecwait(2);

    unsigned char config[9];
    memset(config, 0, sizeof(config));
    if (usb_get_descriptor(address, low_speed, max_packet, 2, 0, config, sizeof(config)) != 0 ||
        config[1] != 2 || config[0] < 9) USB_ENUM_ABORT();
    unsigned short total = (unsigned short)(config[2] | ((unsigned short)config[3] << 8));
    if (total < 9 || total > sizeof(g_uhci_control)) USB_ENUM_ABORT();
    memset(g_uhci_control, 0, total);
    if (usb_get_descriptor(address, low_speed, max_packet, 2, 0, g_uhci_control, total) != 0)
        USB_ENUM_ABORT();

    unsigned char config_value = g_uhci_control[5];
    unsigned char interface_number = 0, endpoint = 0, endpoint_packet = 0, interval = 10;
    unsigned char storage_interface = 0, storage_in = 0, storage_out = 0;
    unsigned char endpoint_burst = 0, storage_in_burst = 0, storage_out_burst = 0;
    unsigned short storage_in_packet = 0, storage_out_packet = 0;
    unsigned char last_endpoint_address = 0xFFu;
    int boot_keyboard = 0, mass_storage = 0;
    for (unsigned int offset = 0; offset + 2 <= total;) {
        unsigned char size = g_uhci_control[offset];
        unsigned char type = g_uhci_control[offset + 1];
        if (size < 2 || offset + size > total) break;
        if (type == 4 && size >= 9) {
            last_endpoint_address = 0xFFu;
            boot_keyboard = g_uhci_control[offset + 5] == 3 &&
                            g_uhci_control[offset + 6] == 1 &&
                            g_uhci_control[offset + 7] == 1;
            mass_storage = g_uhci_control[offset + 5] == 8 &&
                           g_uhci_control[offset + 6] == 6 &&
                           g_uhci_control[offset + 7] == 0x50;
            if (boot_keyboard) interface_number = g_uhci_control[offset + 2];
            if (mass_storage) storage_interface = g_uhci_control[offset + 2];
        } else if (type == 5 && size >= 7) {
            unsigned char transfer_type = g_uhci_control[offset + 3] & 3u;
            unsigned char endpoint_address = g_uhci_control[offset + 2];
            last_endpoint_address = endpoint_address;
            unsigned short packet = (unsigned short)(g_uhci_control[offset + 4] |
                                      ((unsigned short)g_uhci_control[offset + 5] << 8));
            packet &= 0x07FFu;
            if (boot_keyboard && (endpoint_address & 0x80u) && transfer_type == 3) {
                endpoint = endpoint_address & 0x0Fu;
                endpoint_packet = packet;
                interval = g_uhci_control[offset + 6];
            }
            unsigned short packet_limit = 64u;
            if (g_active_interface == 0x20u) packet_limit = 512u;
            if (g_active_interface == 0x30u) {
                unsigned int speed = xhci_port_speed(port);
                packet_limit = speed >= 4u ? 1024u : (speed == 3u ? 512u : 64u);
            }
            if (mass_storage && transfer_type == 2 && packet <= packet_limit &&
                usb_bulk_packet_valid(port, low_speed, packet)) {
                if (endpoint_address & 0x80u) {
                    storage_in = endpoint_address & 0x0Fu;
                    storage_in_packet = packet;
                } else {
                    storage_out = endpoint_address & 0x0Fu;
                    storage_out_packet = packet;
                }
            }
        } else if (type == 48 && size >= 6 && g_active_interface == 0x30u &&
                   last_endpoint_address != 0xFFu) {
            unsigned char burst = g_uhci_control[offset + 2];
            if (boot_keyboard && last_endpoint_address == (unsigned char)(0x80u | endpoint))
                endpoint_burst = burst;
            if (mass_storage && (last_endpoint_address & 0x80u) &&
                (last_endpoint_address & 0x0Fu) == storage_in)
                storage_in_burst = burst;
            if (mass_storage && !(last_endpoint_address & 0x80u) &&
                (last_endpoint_address & 0x0Fu) == storage_out)
                storage_out_burst = burst;
        }
        offset += size;
    }
    int keyboard_found = boot_keyboard && endpoint && endpoint_packet == 8 &&
                         g_usb_keyboard_count < USB_MAX_KEYBOARDS;
    int storage_found = mass_storage && storage_in && storage_out &&
                        storage_in_packet >= 8 && storage_out_packet >= 8 &&
                        g_usb_storage_count < USB_MAX_STORAGE && !low_speed;
    if (!keyboard_found && !storage_found) USB_ENUM_ABORT();
    if (usb_set_configuration(address, low_speed, max_packet, config_value) != 0)
        USB_ENUM_ABORT();

    if (g_active_interface == 0x30u) {
        if (keyboard_found && xhci_configure_endpoint(address,
                (unsigned char)(0x80u | endpoint), 3, endpoint_packet, interval,
                endpoint_burst) != 0)
            keyboard_found = 0;
        if (storage_found &&
            (xhci_configure_endpoint(address, storage_in | 0x80u, 2,
                                     storage_in_packet, 0, storage_in_burst) != 0 ||
             xhci_configure_endpoint(address, storage_out, 2,
                                     storage_out_packet, 0, storage_out_burst) != 0))
            storage_found = 0;
    }
    if (!keyboard_found && !storage_found) USB_ENUM_ABORT();

    int registered = 0;
    if (keyboard_found && usb_set_boot_protocol(address, low_speed, max_packet, interface_number) == 0) {
        usb_keyboard_t* keyboard = &g_usb_keyboards[g_usb_keyboard_count++];
        memset(keyboard, 0, sizeof(*keyboard));
        keyboard->controller_index = (unsigned char)g_active_controller_index;
        keyboard->address = address;
        keyboard->endpoint = endpoint;
        keyboard->interface_number = interface_number;
        keyboard->low_speed = low_speed;
        keyboard->max_packet = endpoint_packet;
        keyboard->interval = interval ? interval : 10;
        keyboard->toggle = 0;
        print_string("[USB] USB HID boot keyboard configured at address ");
        print_char((char)('0' + address / 10));
        print_char((char)('0' + address % 10));
        print_string(".\n");
        registered = 1;
    }
    if (storage_found) {
        if (g_usb_msc_transport_count < USB_MAX_STORAGE) {
            unsigned char transport_index = (unsigned char)g_usb_msc_transport_count++;
            memset(&g_usb_msc_transports[transport_index], 0,
                   sizeof(g_usb_msc_transports[transport_index]));
            unsigned char max_lun = usb_mass_storage_max_lun(address, max_packet,
                                                              storage_interface);
            int storage_registered = 0;
            for (unsigned int lun = 0; lun <= max_lun &&
                 g_usb_storage_count < USB_MAX_STORAGE; ++lun) {
                usb_storage_t* storage = &g_usb_storage[g_usb_storage_count];
                memset(storage, 0, sizeof(*storage));
                storage->controller_index = (unsigned char)g_active_controller_index;
                storage->address = address;
                storage->interface_number = storage_interface;
                storage->endpoint_in = storage_in;
                storage->endpoint_out = storage_out;
                storage->max_packet_in = storage_in_packet;
                storage->max_packet_out = storage_out_packet;
                storage->control_packet = max_packet;
                storage->lun = (unsigned char)lun;
                storage->transport_index = transport_index;
                if (usb_msc_initialize(storage) == 0) {
                    ++g_usb_storage_count;
                    registered = 1;
                    storage_registered = 1;
                }
            }
            if (!storage_registered) --g_usb_msc_transport_count;
        }
    }
    if (!registered && xhci_slot_active) (void)xhci_release_device(address);
#undef USB_ENUM_ABORT
}

static void usb_enumerate_root_ports(void) {
    unsigned int ports = usb_root_port_count();
    for (unsigned int port = 0; port < ports; ++port) {
        usb_reset_root_port(port);
        usb_try_enumerate_device(port);
    }
}

/*
 * PCI class 0C/03 identifies USB host controllers.
 */
int usb_stack_init(void) {
    g_usb_controller_count = 0;
    g_usb_keyboard_count = 0;
    g_usb_storage_count = 0;
    g_usb_msc_transport_count = 0;
    memset(g_usb_msc_transports, 0, sizeof(g_usb_msc_transports));
    g_next_address = 1;
    g_uhci_ready = 0;
    g_ohci_ready = 0;
    g_ehci_ready = 0;
    g_xhci_ready = 0;
    g_xhci_initialized = 0;
    g_active_interface = 0xFFu;

    for (unsigned int bus = 0; bus < 256; ++bus) {
        for (unsigned char slot = 0; slot < 32; ++slot) {
            unsigned short vendor = pci_read_config_word((unsigned char)bus, slot, 0, PCI_VENDOR_ID);
            if (vendor == 0xFFFF || vendor == 0) continue;
            unsigned char header = pci_read_config_byte((unsigned char)bus, slot, 0, 0x0E);
            unsigned char functions = (header & 0x80) ? 8 : 1;
            for (unsigned char func = 0; func < functions; ++func) {
                if (func != 0) {
                    vendor = pci_read_config_word((unsigned char)bus, slot, func, PCI_VENDOR_ID);
                    if (vendor == 0xFFFF || vendor == 0) continue;
                }
                if (pci_read_config_byte((unsigned char)bus, slot, func, PCI_CLASS) != PCI_CLASS_SERIAL_BUS ||
                    pci_read_config_byte((unsigned char)bus, slot, func, PCI_SUBCLASS) != PCI_SUBCLASS_USB)
                    continue;
                if (g_usb_controller_count >= USB_MAX_CONTROLLERS) continue;
                unsigned int controller_index = g_usb_controller_count++;
                usb_controller_t* controller = &g_usb_controllers[controller_index];
                memset(controller, 0, sizeof(*controller));
                controller->pci.bus = (unsigned char)bus;
                controller->pci.slot = slot;
                controller->pci.func = func;
                controller->pci.vendor_id = vendor;
                controller->pci.device_id = pci_read_config_word((unsigned char)bus, slot, func, PCI_DEVICE_ID);
                controller->pci.class_id = PCI_CLASS_SERIAL_BUS;
                controller->pci.subclass_id = PCI_SUBCLASS_USB;
                controller->interface = pci_read_config_byte((unsigned char)bus, slot, func, PCI_PROG_IF);
                controller->pci.prog_if = controller->interface;
                controller->pci.bar0 = pci_read_config_dword((unsigned char)bus, slot, func, PCI_BAR0);
                controller->pci.bar1 = pci_read_config_dword((unsigned char)bus, slot, func, PCI_BAR1);
                controller->pci.irq = pci_read_config_byte((unsigned char)bus, slot, func, PCI_INTERRUPT_LINE);
                if (controller->interface == 0x00) {
                    unsigned int bar4 = pci_read_config_dword((unsigned char)bus, slot, func, PCI_BAR4);
                    if (bar4 & 1u) controller->io_base = (unsigned short)(bar4 & 0xFFFEu);
                    controller->port_count = 2;
                }
                print_string("[USB] PCI controller: ");
                print_string(usb_controller_name(controller->interface));
                print_string(" at ");
                usb_print_hex_byte((unsigned char)bus); print_char(':');
                usb_print_hex_byte(slot); print_char('.'); print_char((char)('0' + func));
                print_string(" (vendor "); usb_print_hex_word(vendor);
                print_string(", device "); usb_print_hex_word(controller->pci.device_id);
                print_string(")\n");

                if (controller->interface == 0x00 && !controller->initialized &&
                    uhci_reset_controller(controller) == 0) {
                    controller->initialized = 1;
                    usb_select_controller(controller_index);
                    print_string("[USB] UHCI controller running; scanning root ports.\n");
                    usb_enumerate_root_ports();
                } else if (controller->interface == 0x10 && !controller->initialized &&
                           ohci_reset_controller(controller) == 0) {
                    usb_select_controller(controller_index);
                    print_string("[USB] OHCI controller running; scanning root ports.\n");
                    usb_enumerate_root_ports();
                } else if (controller->interface == 0x20 && !controller->initialized &&
                           ehci_reset_controller(controller) == 0) {
                    usb_select_controller(controller_index);
                    print_string("[USB] EHCI controller running; scanning root ports.\n");
                    usb_enumerate_root_ports();
                } else if (controller->interface == 0x30 && !controller->initialized &&
                           !g_xhci_initialized) {
                    unsigned int bar = pci_read_config_dword((unsigned char)bus, slot, func, PCI_BAR0);
                    unsigned int bar_upper = 0;
                    if (!(bar & 1u) && (bar & 0x6u) == 0x4u)
                        bar_upper = pci_read_config_dword((unsigned char)bus, slot, func, PCI_BAR1);
                    unsigned int bar_base = bar & 0xFFFFFFF0u;
                    if (!(bar & 1u) && !bar_upper && bar_base) {
                        unsigned short command = pci_read_config_word((unsigned char)bus, slot,
                                                                       func, PCI_COMMAND);
                        pci_write_config_word((unsigned char)bus, slot, func, PCI_COMMAND,
                                              (unsigned short)(command | 0x0006u));
                        if (xhci_init(bar_base) == 0) {
                            controller->port_count = (unsigned char)xhci_port_count();
                            controller->mmio_base = bar_base;
                            controller->initialized = 1;
                            g_xhci_initialized = 1;
                            usb_select_controller(controller_index);
                            print_string("[USB] xHCI controller running; scanning root ports.\n");
                            usb_enumerate_root_ports();
                        }
                    }
                }
            }
        }
    }
    if (!g_usb_controller_count) print_string("[USB] No PCI USB host controller found.\n");
    if (g_usb_controller_count && !g_uhci_ready && !g_ohci_ready && !g_ehci_ready && !g_xhci_ready)
        print_string("[USB] Found host controllers, but no implemented controller engine is available.\n");
    usb_stack_poll();
    return (int)g_usb_controller_count;
}

void usb_stack_poll(void) {
    if (!g_usb_keyboard_count) return;
    unsigned int now = idt_get_millis();
    for (unsigned int i = 0; i < g_usb_keyboard_count; ++i) {
        usb_keyboard_t* keyboard = &g_usb_keyboards[i];
        usb_repeat_held_keys(keyboard, now);
        unsigned int interval = keyboard->interval;
        if (interval < 1) interval = 1;
        if (interval > 20) interval = 20;
        if ((unsigned int)(now - keyboard->last_poll) < interval) continue;
        keyboard->last_poll = now;
        unsigned char report[8] = {0};
        unsigned short actual = 0;
        usb_select_controller(keyboard->controller_index);
        int result = g_active_interface == 0x10u
            ? ohci_transfer_chain(0x69, keyboard->address, keyboard->endpoint, keyboard->toggle,
                                  keyboard->low_speed, report, sizeof(report), keyboard->max_packet,
                                  &actual, 2)
            : (g_active_interface == 0x20u
                ? ehci_interrupt_transfer(keyboard->address, keyboard->endpoint, keyboard->toggle,
                                          keyboard->max_packet, report, sizeof(report), &actual)
                : (g_active_interface == 0x30u
                    ? xhci_transfer(keyboard->address,
                                    (unsigned char)(0x80u | keyboard->endpoint), 1,
                                    report, sizeof(report), &actual)
                    : uhci_transfer(0x69, keyboard->address, keyboard->endpoint, keyboard->toggle,
                                keyboard->low_speed, report, sizeof(report), keyboard->max_packet, &actual)));
        if (result != 0 || actual < sizeof(report)) continue;
        keyboard->toggle ^= 1;
        usb_process_keyboard_report(keyboard, report);
    }
}

int usb_stack_storage_present(unsigned int device) {
    return device < g_usb_storage_count && g_usb_storage[device].valid;
}

int usb_stack_storage_read_sectors(unsigned int device, unsigned int lba,
                                   unsigned char count, void* buffer) {
    if (!usb_stack_storage_present(device) || !buffer || !count) return -1;
    usb_storage_t* storage = &g_usb_storage[device];
    usb_select_controller(storage->controller_index);
    if (lba > storage->last_lba || (unsigned int)count - 1u > storage->last_lba - lba) return -1;
    unsigned char* output = (unsigned char*)buffer;
    unsigned int completed = 0;
    while (completed < count) {
        unsigned int sectors = count - completed;
        if (sectors > 8u) sectors = 8u; /* Keep BOT data within the 4 KiB bounce buffer. */
        unsigned int current = lba + completed;
        unsigned char cdb[10] = {0x28, 0, (unsigned char)(current >> 24),
            (unsigned char)(current >> 16), (unsigned char)(current >> 8),
            (unsigned char)current, 0, (unsigned char)(sectors >> 8),
            (unsigned char)sectors, 0};
        unsigned int bytes = sectors * 512u;
        if (usb_msc_command(storage, cdb, sizeof(cdb), 1,
                            output + completed * 512u, bytes) != 0) {
            (void)usb_mass_storage_reset(storage);
            return -1;
        }
        completed += sectors;
    }
    return 0;
}

int usb_stack_storage_write_sectors(unsigned int device, unsigned int lba,
                                    unsigned char count, const void* buffer) {
    if (!usb_stack_storage_present(device) || !buffer || !count) return -1;
    usb_storage_t* storage = &g_usb_storage[device];
    usb_select_controller(storage->controller_index);
    if (lba > storage->last_lba || (unsigned int)count - 1u > storage->last_lba - lba) return -1;
    const unsigned char* input = (const unsigned char*)buffer;
    unsigned int completed = 0;
    while (completed < count) {
        unsigned int sectors = count - completed;
        if (sectors > 8u) sectors = 8u;
        unsigned int current = lba + completed;
        unsigned char cdb[10] = {0x2A, 0, (unsigned char)(current >> 24),
            (unsigned char)(current >> 16), (unsigned char)(current >> 8),
            (unsigned char)current, 0, (unsigned char)(sectors >> 8),
            (unsigned char)sectors, 0};
        unsigned int bytes = sectors * 512u;
        if (usb_msc_command(storage, cdb, sizeof(cdb), 0,
                            (void*)(input + completed * 512u), bytes) != 0) {
            (void)usb_mass_storage_reset(storage);
            return -1;
        }
        completed += sectors;
    }
    return 0;
}
