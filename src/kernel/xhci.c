#include "xhci.h"
#include "cykastd.h"
#include "paging.h"
#include "idt.h"

#define XHCI_MAX_SLOTS 32u
#define XHCI_MAX_SCRATCHPADS 256u
#define XHCI_COMMANDS 64u
#define XHCI_EVENTS 64u
#define XHCI_TRB_CYCLE 1u
#define XHCI_TRB_LINK (6u << 10)

typedef struct __attribute__((aligned(16))) {
    volatile unsigned int parameter_low;
    volatile unsigned int parameter_high;
    volatile unsigned int status;
    volatile unsigned int control;
} xhci_trb_t;

typedef struct __attribute__((aligned(64))) {
    unsigned int address_low;
    unsigned int address_high;
    unsigned int size;
    unsigned int reserved;
} xhci_erst_entry_t;

typedef struct __attribute__((aligned(64))) {
    unsigned int device_context[ XHCI_MAX_SLOTS + 1u ][32u * 16u] __attribute__((aligned(64)));
    unsigned int input_context[33u * 16u] __attribute__((aligned(64)));
    xhci_trb_t* ep0_rings[XHCI_MAX_SLOTS + 1u];
    xhci_trb_t* transfer_rings[XHCI_MAX_SLOTS + 1u][32];
    unsigned char ring_index[XHCI_MAX_SLOTS + 1u][32];
    unsigned char ring_cycle[XHCI_MAX_SLOTS + 1u][32];
    unsigned char ring_enabled[XHCI_MAX_SLOTS + 1u][32];
} xhci_dma_state_t;

static volatile unsigned int* g_op;
static volatile unsigned int* g_runtime;
static volatile unsigned int* g_doorbells;
static unsigned int g_ports;
static unsigned int g_slots;
static unsigned int g_context_words = 8u;
static unsigned int g_command_index;
static unsigned int g_command_cycle = 1;
static unsigned int g_event_index;
static unsigned int g_event_cycle = 1;
static xhci_trb_t* g_command_ring;
static xhci_trb_t* g_event_ring;
static unsigned char* g_ring_pool;
static xhci_erst_entry_t g_erst __attribute__((aligned(64)));
static unsigned int g_dcbaa[256][2] __attribute__((aligned(64)));
static unsigned int g_scratchpad_array[XHCI_MAX_SCRATCHPADS][2] __attribute__((aligned(64)));
static unsigned char* g_scratchpad_pages;
static xhci_dma_state_t* g_dma_state;
#define g_device_context (g_dma_state->device_context)
#define g_input_context (g_dma_state->input_context)
#define g_ep0_rings (g_dma_state->ep0_rings)
#define g_transfer_rings (g_dma_state->transfer_rings)
#define g_ring_index (g_dma_state->ring_index)
#define g_ring_cycle (g_dma_state->ring_cycle)
#define g_ring_enabled (g_dma_state->ring_enabled)
static unsigned char g_transfer_bounce[4096] __attribute__((aligned(4096)));
static unsigned int g_address_to_slot[128];
static unsigned int g_current_slot;
static int g_ready;

static unsigned int xhci_pointer(const void* p) { return (unsigned int)p; }

static xhci_trb_t* xhci_alloc_ring(unsigned int* offset, unsigned int entries) {
    xhci_trb_t* ring = (xhci_trb_t*)(g_ring_pool + *offset);
    *offset += entries * sizeof(xhci_trb_t);
    return ring;
}

static int xhci_ring_fits_64k(const void* ring, unsigned int bytes) {
    unsigned int address = xhci_pointer(ring);
    return bytes && bytes <= 0x10000u &&
           (address & 0xFFFFu) <= 0x10000u - bytes;
}

static void xhci_write64(volatile unsigned int* reg, unsigned int low, unsigned int high) {
    reg[1] = high;
    reg[0] = low;
}

static int xhci_wait_reg(volatile unsigned int* reg, unsigned int mask,
                         unsigned int value, unsigned int timeout_ms) {
    unsigned int start = idt_get_millis();
    while ((*reg & mask) != value) {
        if ((unsigned int)(idt_get_millis() - start) >= timeout_ms) return -1;
        __asm__ volatile("pause");
    }
    return 0;
}

static int xhci_next_event(xhci_trb_t* out, unsigned int timeout_ms) {
    unsigned int start = idt_get_millis();
    for (;;) {
        xhci_trb_t* event = &g_event_ring[g_event_index];
        unsigned int control = event->control;
        if ((control & XHCI_TRB_CYCLE) == g_event_cycle) {
            out->parameter_low = event->parameter_low;
            out->parameter_high = event->parameter_high;
            out->status = event->status;
            out->control = control;
            ++g_event_index;
            if (g_event_index == XHCI_EVENTS) {
                g_event_index = 0;
                g_event_cycle ^= 1u;
            }
            volatile unsigned int* erdp = &g_runtime[0x38u / 4u];
            xhci_write64(erdp, xhci_pointer(&g_event_ring[g_event_index]) | (1u << 3), 0);
            return 0;
        }
        if ((unsigned int)(idt_get_millis() - start) >= timeout_ms) return -1;
        __asm__ volatile("pause");
    }
}

static void xhci_ring_command_doorbell(void) { g_doorbells[0] = 0; }

static int xhci_command(unsigned int parameter_low, unsigned int parameter_high,
                        unsigned int status, unsigned int control, unsigned int* result) {
    if (!g_ready) return -1;
    unsigned int index = g_command_index;
    xhci_trb_t* trb = &g_command_ring[index];
    trb->parameter_low = parameter_low;
    trb->parameter_high = parameter_high;
    trb->status = status;
    trb->control = (control & ~XHCI_TRB_CYCLE) | g_command_cycle;
    ++g_command_index;
    if (g_command_index == XHCI_COMMANDS - 1u) {
        xhci_trb_t* link = &g_command_ring[XHCI_COMMANDS - 1u];
        link->parameter_low = xhci_pointer(g_command_ring);
        link->parameter_high = 0;
        link->status = 0;
        link->control = XHCI_TRB_LINK | (1u << 1) | g_command_cycle;
        g_command_index = 0;
        g_command_cycle ^= 1u;
    }
    xhci_ring_command_doorbell();
    unsigned int start = idt_get_millis();
    for (;;) {
        xhci_trb_t event;
        if (xhci_next_event(&event, 1u) != 0) {
            if ((unsigned int)(idt_get_millis() - start) > 1000u) return -1;
            continue;
        }
        if (((event.control >> 10) & 0x3Fu) != 33u) continue;
        if (event.parameter_low != xhci_pointer(trb) || event.parameter_high != 0) continue;
        if (((event.status >> 24) & 0xFFu) != 1u) {
            return -1;
        }
        if (result) *result = event.control >> 24;
        return 0;
    }
}

static int xhci_handoff_legacy(unsigned int base, unsigned int hccparams) {
    unsigned int offset = (hccparams >> 16) & 0xFFFFu;
    if (!offset) return 0;
    if (offset < 0x40u || offset >= 0x20000u || (offset & 3u)) return -1;
    unsigned int hops;
    for (hops = 0; hops < 64; ++hops) {
        if (offset < 0x40u || offset > 0x1FFF8u || (offset & 3u)) return -1;
        volatile unsigned int* cap = (volatile unsigned int*)(base + offset);
        unsigned int header = *cap;
        unsigned int id = header & 0xFFu;
        unsigned int next = (header >> 8) & 0xFFu;
        if (id == 1u) {
            *cap = header | (1u << 24);
            unsigned int start = idt_get_millis();
            while (*cap & (1u << 16)) {
                if ((unsigned int)(idt_get_millis() - start) > 500u) return -1;
                __asm__ volatile("pause");
            }
            *(volatile unsigned int*)(base + offset + 4u) = 0;
            return 0;
        }
        if (!next) break;
        /* The Next xHCI Extended Capability Pointer is measured in DWORDs,
         * while xECP above is already a byte offset from the MMIO base. */
        offset += next * 4u;
    }
    if (hops == 64u) return -1;
    if (offset && (offset < 0x40u || offset >= 0x20000u)) return -1;
    return 0;
}

int xhci_init(unsigned int capability_base) {
    g_ready = 0;
    if (!capability_base || paging_map_mmio_range(capability_base, 0x20000u) != 0) return -1;
    if (!g_dma_state) {
        unsigned int allocation = (unsigned int)malloc(sizeof(xhci_dma_state_t) + 63u);
        if (!allocation) return -1;
        g_dma_state = (xhci_dma_state_t*)((allocation + 63u) & 0xFFFFFFC0u);
    }
    if (!g_ring_pool) {
        /* 1,091 rings x 256 bytes fit in five 64 KiB aligned slabs. */
        unsigned int allocation = (unsigned int)malloc(5u * 65536u + 65535u);
        if (!allocation) return -1;
        g_ring_pool = (unsigned char*)((allocation + 65535u) & 0xFFFF0000u);
    }
    volatile unsigned char* cap8 = (volatile unsigned char*)capability_base;
    unsigned int cap_length = cap8[0];
    if (cap_length < 0x20u || cap_length > 0x100u) return -1;
    volatile unsigned int* cap = (volatile unsigned int*)capability_base;
    unsigned int hcs1 = cap[1];
    unsigned int hcs2 = cap[2];
    unsigned int hcc1 = cap[3];
    unsigned int dboff = cap[4] & ~3u;
    unsigned int rtsoff = cap[5] & ~0x1Fu;
    g_context_words = (hcc1 & (1u << 2)) ? 16u : 8u;
    g_ports = hcs1 >> 24;
    g_slots = hcs1 & 0xFFu;
    if (!g_ports || !g_slots) return -1;
    if (g_slots > XHCI_MAX_SLOTS) g_slots = XHCI_MAX_SLOTS;
    unsigned int scratch_count = (((hcs2 >> 21) & 0x1Fu) << 5) | ((hcs2 >> 27) & 0x1Fu);
    if (scratch_count > XHCI_MAX_SCRATCHPADS) return -1;

    if (xhci_handoff_legacy(capability_base, hcc1) != 0) return -1;
    g_op = (volatile unsigned int*)(capability_base + cap_length);
    g_runtime = (volatile unsigned int*)(capability_base + rtsoff);
    g_doorbells = (volatile unsigned int*)(capability_base + dboff);
    if (paging_map_mmio_range((unsigned int)g_op, 0x1000u) != 0 ||
        paging_map_mmio_range((unsigned int)g_runtime, 0x1000u) != 0 ||
        paging_map_mmio_range((unsigned int)g_doorbells, 0x1000u) != 0) return -1;

    g_op[0] &= ~1u;
    if (xhci_wait_reg(&g_op[1], 1u, 1u, 1000u) != 0) return -1;
    g_op[0] |= 1u << 1;
    if (xhci_wait_reg(&g_op[0], 1u << 1, 0, 1000u) != 0) return -1;
    /* CNR may remain set briefly after HCRST clears. Do not program the
       controller's DMA registers until the controller is ready. */
    if (xhci_wait_reg(&g_op[1], 1u << 11, 0, 1000u) != 0) return -1;

    memset(g_dcbaa, 0, sizeof(g_dcbaa));
    memset(g_dma_state, 0, sizeof(*g_dma_state));
    memset(g_ring_pool, 0, 5u * 65536u);
    unsigned int ring_offset = 0;
    g_command_ring = xhci_alloc_ring(&ring_offset, XHCI_COMMANDS);
    g_event_ring = xhci_alloc_ring(&ring_offset, XHCI_EVENTS);
    for (unsigned int slot = 0; slot <= XHCI_MAX_SLOTS; ++slot) {
        g_ep0_rings[slot] = xhci_alloc_ring(&ring_offset, 16u);
        for (unsigned int endpoint = 0; endpoint < 32u; ++endpoint)
            g_transfer_rings[slot][endpoint] = xhci_alloc_ring(&ring_offset, 16u);
    }
    if (ring_offset > 5u * 65536u ||
        !xhci_ring_fits_64k(g_command_ring, XHCI_COMMANDS * sizeof(xhci_trb_t)) ||
        !xhci_ring_fits_64k(g_event_ring, XHCI_EVENTS * sizeof(xhci_trb_t)))
        return -1;
    for (unsigned int slot = 0; slot <= XHCI_MAX_SLOTS; ++slot) {
        if (!xhci_ring_fits_64k(g_ep0_rings[slot], 16u * sizeof(xhci_trb_t)))
            return -1;
        for (unsigned int endpoint = 0; endpoint < 32u; ++endpoint)
            if (!xhci_ring_fits_64k(g_transfer_rings[slot][endpoint],
                                    16u * sizeof(xhci_trb_t))) return -1;
    }
    memset(g_command_ring, 0, XHCI_COMMANDS * sizeof(xhci_trb_t));
    memset(g_event_ring, 0, XHCI_EVENTS * sizeof(xhci_trb_t));
    memset(g_address_to_slot, 0, sizeof(g_address_to_slot));
    g_current_slot = 0;
    memset(g_scratchpad_array, 0, sizeof(g_scratchpad_array));
    g_scratchpad_pages = 0;
    if (scratch_count) {
        unsigned int bytes = scratch_count * 4096u + 4095u;
        unsigned int allocation = (unsigned int)malloc(bytes);
        if (!allocation) return -1;
        g_scratchpad_pages = (unsigned char*)((allocation + 4095u) & 0xFFFFF000u);
        memset(g_scratchpad_pages, 0, scratch_count * 4096u);
    }
    for (unsigned int i = 0; i < scratch_count; ++i) {
        g_scratchpad_array[i][0] = xhci_pointer(g_scratchpad_pages + i * 4096u);
        g_dcbaa[i + 1][0] = 0;
    }
    if (scratch_count) g_dcbaa[0][0] = xhci_pointer(g_scratchpad_array);
    g_command_index = 0;
    g_command_cycle = 1;
    g_event_index = 0;
    g_event_cycle = 1;
    g_command_ring[XHCI_COMMANDS - 1u].parameter_low = xhci_pointer(g_command_ring);
    g_command_ring[XHCI_COMMANDS - 1u].control = XHCI_TRB_LINK | (1u << 1) | 1u;
    g_erst.address_low = xhci_pointer(g_event_ring);
    g_erst.address_high = 0;
    g_erst.size = XHCI_EVENTS;
    g_erst.reserved = 0;

    /* PAGESIZE is read-only; bit zero advertises 4 KiB support. */
    if (!(g_op[2] & 1u)) return -1;
    xhci_write64(&g_op[0x30u / 4u], xhci_pointer(g_dcbaa), 0);
    xhci_write64(&g_op[0x18u / 4u], xhci_pointer(g_command_ring) | 1u, 0);
    g_op[0x38u / 4u] = g_slots;
    volatile unsigned int* intr0 = &g_runtime[0x20u / 4u];
    intr0[0] = 0; /* Disable interrupts; completion is polled. */
    intr0[2] = 1;
    xhci_write64(&intr0[6], xhci_pointer(g_event_ring), 0);
    xhci_write64(&intr0[4], xhci_pointer(&g_erst), 0);
    g_op[0] &= ~((1u << 2) | (1u << 3));
    g_op[0] |= 1u;
    if (xhci_wait_reg(&g_op[1], 1u, 0, 1000u) != 0) return -1;
    g_ready = 1;
    return 0;
}

unsigned int xhci_port_count(void) { return g_ready ? g_ports : 0; }

unsigned int xhci_port_status(unsigned int port) {
    if (!g_ready || port >= g_ports) return 0;
    volatile unsigned int* portsc = &g_op[(0x400u + port * 0x10u) / 4u];
    return *portsc;
}

unsigned int xhci_port_speed(unsigned int port) {
    return (xhci_port_status(port) >> 10) & 0x0Fu;
}

static void xhci_init_ring(xhci_trb_t* ring, unsigned int entries) {
    memset(ring, 0, entries * sizeof(xhci_trb_t));
    ring[entries - 1u].parameter_low = xhci_pointer(ring);
    ring[entries - 1u].parameter_high = 0;
    ring[entries - 1u].status = 0;
    ring[entries - 1u].control = XHCI_TRB_LINK | (1u << 1) | 1u;
}

int xhci_prepare_port(unsigned int port) {
    if (!g_ready || port >= g_ports || !(xhci_port_status(port) & (1u << 1))) return -1;
    unsigned int speed = xhci_port_speed(port);
    unsigned int max_packet = speed == 3u ? 64u : (speed >= 4u ? 512u : 8u);
    unsigned int slot = 0;
    if (xhci_command(0, 0, 0, 9u << 10, &slot) != 0 || !slot) return -1;
    if (slot > g_slots || slot > XHCI_MAX_SLOTS) {
        (void)xhci_command(0, 0, 0, (slot << 24) | (10u << 10), 0);
        return -1;
    }

    memset(g_device_context[slot], 0, sizeof(g_device_context[slot]));
    memset(g_input_context, 0, sizeof(g_input_context));
    xhci_init_ring(g_ep0_rings[slot], 16);
    if (!xhci_ring_fits_64k(g_ep0_rings[slot], 16u * sizeof(xhci_trb_t))) return -1;
    g_ring_index[slot][1] = 0;
    g_ring_cycle[slot][1] = 1;
    g_ring_enabled[slot][1] = 1;
    g_dcbaa[slot][0] = xhci_pointer(g_device_context[slot]);
    g_dcbaa[slot][1] = 0;

    /* Input Control Context: add Slot Context and Default Control Endpoint. */
    g_input_context[1] = 3u;
    unsigned int* input_slot = &g_input_context[g_context_words];
    input_slot[0] = (speed << 20) | (1u << 27);
    input_slot[1] = (port + 1u) << 16;
    unsigned int* input_ep0 = &g_input_context[g_context_words * 2u];
    input_ep0[1] = (3u << 1) | (4u << 3);
    input_ep0[2] = max_packet << 16;
    input_ep0[3] = xhci_pointer(g_ep0_rings[slot]) | 1u;
    input_ep0[4] = 0;
    input_ep0[5] = max_packet;

    if (xhci_command(xhci_pointer(g_input_context), 0, 0,
                     (11u << 10) | (slot << 24), 0) != 0) {
        unsigned int command = (slot << 24) | (10u << 10);
        (void)xhci_command(0, 0, 0, command, 0);
        return -1;
    }
    g_current_slot = slot;
    g_address_to_slot[0] = slot;
    return 0;
}

int xhci_release_device(unsigned char address) {
    unsigned int slot = xhci_slot_for_address(address);
    if (!g_ready || !slot) return -1;
    unsigned int command = (slot << 24) | (10u << 10); /* Disable Slot */
    int result = xhci_command(0, 0, 0, command, 0);
    if (result != 0) return result;
    for (unsigned int i = 0; i < 128u; ++i)
        if (g_address_to_slot[i] == slot) g_address_to_slot[i] = 0;
    for (unsigned int endpoint = 0; endpoint < 32u; ++endpoint)
        g_ring_enabled[slot][endpoint] = 0;
    if (g_current_slot == slot) g_current_slot = 0;
    return 0;
}

void xhci_bind_address(unsigned char address) {
    if (address < 128u && g_current_slot) g_address_to_slot[address] = g_current_slot;
}

unsigned int xhci_slot_for_address(unsigned char address) {
    if (address >= 128u) return 0;
    if (address == 0 && g_current_slot) return g_current_slot;
    return g_address_to_slot[address];
}

int xhci_update_ep0_packet(unsigned char address, unsigned short max_packet) {
    unsigned int slot = xhci_slot_for_address(address);
    if (!g_ready || !slot || !max_packet) return -1;
    memset(g_input_context, 0, sizeof(g_input_context));
    memcpy(&g_input_context[g_context_words], g_device_context[slot],
           g_context_words * sizeof(unsigned int));
    memcpy(&g_input_context[g_context_words * 2u],
           &g_device_context[slot][g_context_words],
           g_context_words * sizeof(unsigned int));
    g_input_context[1] = 1u << 1;
    g_input_context[g_context_words * 2u + 2u] =
                              (g_input_context[g_context_words * 2u + 2u] & 0x0000FFFFu) |
                              ((unsigned int)max_packet << 16);
    return xhci_command(xhci_pointer(g_input_context), 0, 0,
                        (13u << 10) | (slot << 24), 0);
}

static unsigned int xhci_endpoint_id(unsigned char endpoint_address) {
    unsigned int endpoint = endpoint_address & 0x0Fu;
    if (!endpoint) return 1u;
    return endpoint * 2u + ((endpoint_address & 0x80u) ? 1u : 0u);
}

static int xhci_ring_push(unsigned int slot, unsigned int endpoint, xhci_trb_t* value) {
    if (slot > XHCI_MAX_SLOTS || endpoint > 31u || !g_ring_enabled[slot][endpoint]) return -1;
    unsigned int index = g_ring_index[slot][endpoint];
    unsigned int cycle = g_ring_cycle[slot][endpoint];
    xhci_trb_t* ring = g_transfer_rings[slot][endpoint];
    if (endpoint == 1u) ring = g_ep0_rings[slot];
    if (index >= 15u) return -1;
    ring[index].parameter_low = value->parameter_low;
    ring[index].parameter_high = value->parameter_high;
    ring[index].status = value->status;
    ring[index].control = (value->control & ~XHCI_TRB_CYCLE) | cycle;
    ++index;
    if (index == 15u) {
        ring[15].parameter_low = xhci_pointer(ring);
        ring[15].parameter_high = 0;
        ring[15].status = 0;
        ring[15].control = XHCI_TRB_LINK | (1u << 1) | cycle;
        index = 0;
        cycle ^= 1u;
    }
    g_ring_index[slot][endpoint] = (unsigned char)index;
    g_ring_cycle[slot][endpoint] = (unsigned char)cycle;
    return 0;
}

static int xhci_wait_transfer(unsigned int slot, unsigned int endpoint,
                              unsigned int event_count, unsigned int* residual) {
    for (unsigned int received = 0; received < event_count;) {
        xhci_trb_t event;
        if (xhci_next_event(&event, 1000u) != 0) return -1;
        if (((event.control >> 10) & 0x3Fu) != 32u) continue;
        if ((event.control >> 24) != slot || ((event.control >> 16) & 0x1Fu) != endpoint)
            continue;
        unsigned int completion = event.status >> 24;
        if (completion != 1u && completion != 13u) return -1;
        if (residual && received == 0) *residual = event.status & 0xFFFFFFu;
        ++received;
    }
    return 0;
}

int xhci_control_transfer(unsigned char address, const unsigned char setup[8],
                          void* buffer, unsigned short length, unsigned short* actual) {
    unsigned int slot = xhci_slot_for_address(address);
    if (!g_ready || !slot || !setup || length > sizeof(g_transfer_bounce) ||
        (length && !buffer)) return -1;
    unsigned int setup_low = (unsigned int)setup[0] | ((unsigned int)setup[1] << 8) |
        ((unsigned int)setup[2] << 16) | ((unsigned int)setup[3] << 24);
    unsigned int setup_high = (unsigned int)setup[4] | ((unsigned int)setup[5] << 8) |
        ((unsigned int)setup[6] << 16) | ((unsigned int)setup[7] << 24);
    unsigned int input = (setup[0] & 0x80u) != 0;
    if (length) {
        if (input) memset(g_transfer_bounce, 0, length);
        else memcpy(g_transfer_bounce, buffer, length);
    }
    xhci_trb_t trb;
    trb.parameter_low = setup_low;
    trb.parameter_high = setup_high;
    trb.status = 8u;
    trb.control = (2u << 10) | (1u << 6) | (1u << 4) |
                  ((length ? (input ? 3u : 2u) : 0u) << 16);
    if (xhci_ring_push(slot, 1, &trb) != 0) return -1;
    if (length) {
        trb.parameter_low = xhci_pointer(g_transfer_bounce);
        trb.parameter_high = 0;
        trb.status = length;
        trb.control = (3u << 10) | (1u << 4) | (1u << 2) |
                      (input ? (1u << 16) : 0u);
        if (xhci_ring_push(slot, 1, &trb) != 0) return -1;
    }
    trb.parameter_low = 0;
    trb.parameter_high = 0;
    trb.status = 0;
    trb.control = (4u << 10) | (1u << 5) | (input || !length ? (1u << 16) : 0u);
    if (xhci_ring_push(slot, 1, &trb) != 0) return -1;
    g_doorbells[slot] = 1u;
    unsigned int residual = 0;
    /* A control transfer is one TD chain. With IOC on the Status TRB, xHCI
       reports one Transfer Event for the entire Setup/Data/Status sequence. */
    if (xhci_wait_transfer(slot, 1, 1u, length ? &residual : 0) != 0) return -1;
    if (length) {
        unsigned int transferred = length - residual;
        if (transferred > length) return -1;
        if (actual) *actual = (unsigned short)transferred;
        if (input && transferred) memcpy(buffer, g_transfer_bounce, transferred);
    } else if (actual) *actual = 0;
    return 0;
}

int xhci_configure_endpoint(unsigned char address, unsigned char endpoint_address,
                            unsigned char transfer_type, unsigned short max_packet,
                            unsigned char interval, unsigned char max_burst) {
    unsigned int slot = xhci_slot_for_address(address);
    unsigned int endpoint = xhci_endpoint_id(endpoint_address);
    if (!g_ready || !slot || endpoint < 2u || endpoint > 31u ||
        (transfer_type != 2u && transfer_type != 3u) || !max_packet) return -1;
    unsigned int direction_in = (endpoint_address & 0x80u) != 0;
    unsigned int endpoint_type = transfer_type == 2u ? (direction_in ? 6u : 2u)
                                                      : (direction_in ? 7u : 3u);
    xhci_init_ring(g_transfer_rings[slot][endpoint], 16);
    if (!xhci_ring_fits_64k(g_transfer_rings[slot][endpoint],
                            16u * sizeof(xhci_trb_t))) return -1;
    g_ring_index[slot][endpoint] = 0;
    g_ring_cycle[slot][endpoint] = 1;
    g_ring_enabled[slot][endpoint] = 1;
    memset(g_input_context, 0, sizeof(g_input_context));
    memcpy(&g_input_context[g_context_words], g_device_context[slot],
           g_context_words * sizeof(unsigned int));
    unsigned int* input_slot = &g_input_context[g_context_words];
    unsigned int context_entries = input_slot[0] >> 27;
    if (endpoint > context_entries) input_slot[0] = (input_slot[0] & 0x07FFFFFFu) |
                                                     (endpoint << 27);
    g_input_context[1] = 1u | (1u << endpoint);
    unsigned int* ep = &g_input_context[(endpoint + 1u) * g_context_words];
    unsigned int speed = (g_device_context[slot][0] >> 20) & 0x0Fu;
    unsigned int xhci_interval = interval ? interval : 1u;
    if (transfer_type == 3u && speed <= 2u) {
        unsigned int microframes = xhci_interval * 8u;
        xhci_interval = 0;
        while (xhci_interval < 15u && (1u << (xhci_interval + 1u)) <= microframes)
            ++xhci_interval;
        if (xhci_interval < 3u) xhci_interval = 3u;
        if (xhci_interval > 10u) xhci_interval = 10u;
    } else if (transfer_type == 3u) {
        if (xhci_interval > 16u) xhci_interval = 16u;
        --xhci_interval;
    }
    ep[0] = transfer_type == 3u ? (xhci_interval << 16) : 0;
    ep[1] = (3u << 1) | (endpoint_type << 3) | ((unsigned int)max_burst << 8);
    ep[2] = (unsigned int)max_packet << 16;
    ep[3] = xhci_pointer(g_transfer_rings[slot][endpoint]) | 1u;
    ep[5] = max_packet;
    if (xhci_command(xhci_pointer(g_input_context), 0, 0,
                     (12u << 10) | (slot << 24), 0) != 0) {
        g_ring_enabled[slot][endpoint] = 0;
        return -1;
    }
    return 0;
}

int xhci_transfer(unsigned char address, unsigned char endpoint_address, int input,
                  void* buffer, unsigned short length, unsigned short* actual) {
    unsigned int slot = xhci_slot_for_address(address);
    unsigned int endpoint = xhci_endpoint_id(endpoint_address);
    if (!g_ready || !slot || endpoint < 2u || endpoint > 31u ||
        !g_ring_enabled[slot][endpoint] || !buffer || !length ||
        length > sizeof(g_transfer_bounce)) return -1;
    if (!!(endpoint_address & 0x80u) != !!input) return -1;
    if (input) memset(g_transfer_bounce, 0, length);
    else memcpy(g_transfer_bounce, buffer, length);
    xhci_trb_t trb;
    trb.parameter_low = xhci_pointer(g_transfer_bounce);
    trb.parameter_high = 0;
    trb.status = length;
    trb.control = (1u << 10) | (1u << 5);
    if (xhci_ring_push(slot, endpoint, &trb) != 0) return -1;
    g_doorbells[slot] = endpoint;
    unsigned int residual = 0;
    if (xhci_wait_transfer(slot, endpoint, 1u, &residual) != 0) return -1;
    unsigned int transferred = length - residual;
    if (transferred > length) return -1;
    if (input && transferred) memcpy(buffer, g_transfer_bounce, transferred);
    if (actual) *actual = (unsigned short)transferred;
    return 0;
}

int xhci_reset_port(unsigned int port) {
    if (!g_ready || port >= g_ports) return -1;
    volatile unsigned int* portsc = &g_op[(0x400u + port * 0x10u) / 4u];
    unsigned int status = *portsc;
    if (!(status & 1u)) return -1;
    unsigned int speed = (status >> 10) & 0x0Fu;
    unsigned int reset_bit = speed == 4u ? (1u << 31) : (1u << 4);
    unsigned int change_bits = status & (0x7Fu << 17);
    *portsc = (status & 0x0E00C3E0u) | change_bits | reset_bit;
    unsigned int start = idt_get_millis();
    while (*portsc & reset_bit) {
        if ((unsigned int)(idt_get_millis() - start) > 1000u) return -1;
        __asm__ volatile("pause");
    }
    if (!(*portsc & (1u << 1))) return -1;
    status = *portsc;
    change_bits = status & (0x7Fu << 17);
    if (change_bits) *portsc = (status & 0x0E00C3E0u) | change_bits;
    msecwait(10);
    return 0;
}
