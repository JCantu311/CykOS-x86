#include "nvme.h"
#include "cykastd.h"
#include "pci.h"
#include "paging.h"

#define NVME_CLASS          0x01u
#define NVME_SUBCLASS       0x08u
#define NVME_PROGIF         0x02u
#define NVME_TIMEOUT        4000000u
#define NVME_QUEUE_DEPTH    16u
#define NVME_PAGE_SIZE      4096u
#define NVME_SECTOR_SIZE    512u

typedef struct __attribute__((packed, aligned(8))) {
    unsigned char opcode;
    unsigned char flags;
    unsigned short command_id;
    unsigned int namespace_id;
    unsigned long long reserved;
    unsigned long long metadata;
    unsigned long long prp1;
    unsigned long long prp2;
    unsigned int cdw10, cdw11, cdw12, cdw13, cdw14, cdw15;
} nvme_command_t;

typedef struct __attribute__((packed)) {
    unsigned int result;
    unsigned int reserved;
    unsigned short submission_head;
    unsigned short submission_queue;
    unsigned short command_id;
    unsigned short status;
} nvme_completion_t;

typedef struct __attribute__((aligned(4096))) {
    nvme_command_t commands[NVME_QUEUE_DEPTH];
} nvme_submission_queue_t;

typedef struct __attribute__((aligned(4096))) {
    nvme_completion_t entries[NVME_QUEUE_DEPTH];
} nvme_completion_queue_t;

static unsigned int g_nvme_registers;
static unsigned int g_nvme_sectors;
static unsigned int g_nvme_doorbell_stride;
static unsigned short g_nvme_admin_tail, g_nvme_admin_head;
static unsigned short g_nvme_io_tail, g_nvme_io_head;
static unsigned short g_nvme_next_cid;
static unsigned char g_nvme_admin_phase = 1, g_nvme_io_phase = 1;
static int g_nvme_initialized;
static int g_nvme_attempted;
static nvme_submission_queue_t g_nvme_admin_sq __attribute__((aligned(4096)));
static nvme_completion_queue_t g_nvme_admin_cq __attribute__((aligned(4096)));
static nvme_submission_queue_t g_nvme_io_sq __attribute__((aligned(4096)));
static nvme_completion_queue_t g_nvme_io_cq __attribute__((aligned(4096)));
static unsigned int g_nvme_prp_list[1024] __attribute__((aligned(4096)));
static unsigned char g_nvme_identify[4096] __attribute__((aligned(4096)));
static unsigned char g_nvme_namespace[4096] __attribute__((aligned(4096)));

static volatile unsigned int* nvme_reg32(unsigned int offset) {
    return (volatile unsigned int*)(g_nvme_registers + offset);
}

static volatile unsigned long long* nvme_reg64(unsigned int offset) {
    return (volatile unsigned long long*)(g_nvme_registers + offset);
}

static volatile unsigned int* nvme_doorbell(unsigned int queue, int completion) {
    unsigned int index = queue * 2u + (completion ? 1u : 0u);
    return nvme_reg32(0x1000u + index * g_nvme_doorbell_stride);
}

static int nvme_address(const void* pointer, unsigned int* address) {
    unsigned int value = (unsigned int)pointer;
    if (!pointer || value > 0xFFFFFFFFu - NVME_PAGE_SIZE) return -1;
    *address = value;
    return 0;
}

static int nvme_mmio_region_is_reserved(unsigned int address, unsigned int length) {
    volatile unsigned int* count_ptr = (volatile unsigned int*)0x04FCu;
    const unsigned int* map = (const unsigned int*)0x0500u;
    unsigned int count = *count_ptr;
    if (!count || count > 20u || address > 0xFFFFFFFFu - length) return 0;
    unsigned int end = address + length;
    for (unsigned int i = 0; i < count; ++i) {
        const unsigned int* entry = map + i * 6u;
        unsigned int base = entry[0], base_high = entry[1];
        unsigned int bytes = entry[2], bytes_high = entry[3];
        if (entry[4] != 1u || base_high != 0) continue;
        unsigned int region_end = bytes_high || bytes > 0xFFFFFFFFu - base
            ? 0xFFFFFFFFu : base + bytes;
        if (base < end && address < region_end) return 0;
    }
    return 1;
}

static int nvme_bar_conflicts(unsigned int address, unsigned int length,
                              const pci_device_t* nvme_device) {
    unsigned int end = address + length;
    for (unsigned int bus = 0; bus < 256; ++bus) {
        for (unsigned char slot = 0; slot < 32; ++slot) {
            unsigned short vendor0 = pci_read_config_word((unsigned char)bus,
                slot, 0, PCI_VENDOR_ID);
            if (vendor0 == 0xFFFF || vendor0 == 0) continue;
            unsigned char functions =
                (pci_read_config_byte((unsigned char)bus, slot, 0, 0x0E) & 0x80u)
                    ? 8u : 1u;
            for (unsigned char function = 0; function < functions; ++function) {
                unsigned short vendor = function == 0 ? vendor0 :
                    pci_read_config_word((unsigned char)bus, slot, function, PCI_VENDOR_ID);
                if (vendor == 0xFFFF || vendor == 0) continue;
                if (bus == nvme_device->bus && slot == nvme_device->slot &&
                    function == nvme_device->func) continue;
                for (unsigned int bar = 0; bar < 6; ++bar) {
                    unsigned char offset = (unsigned char)(0x10u + bar * 4u);
                    unsigned int value = pci_read_config_dword((unsigned char)bus,
                        slot, function, offset);
                    if (value & 1u) continue;
                    unsigned int base = value & ~0x0Fu;
                    if (base && base >= address && base < end) return 1;
                    if (((value >> 1) & 3u) == 2u) ++bar;
                }
            }
        }
    }
    return 0;
}

static unsigned int nvme_choose_low_mmio(const pci_device_t* device) {
    static const unsigned int candidates[] = {
        0xE0000000u, 0xD0000000u, 0xC0000000u, 0xB0000000u
    };
    for (unsigned int i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        unsigned int address = candidates[i];
        if (nvme_mmio_region_is_reserved(address, 0x4000u) &&
            !nvme_bar_conflicts(address, 0x4000u, device)) return address;
    }
    return 0;
}

static int nvme_submit(nvme_submission_queue_t* sq, nvme_completion_queue_t* cq,
                       unsigned short* tail, unsigned short* head,
                       unsigned char* phase, volatile unsigned int* sq_doorbell,
                       volatile unsigned int* cq_doorbell,
                       const nvme_command_t* command) {
    nvme_command_t* slot = &sq->commands[*tail];
    *slot = *command;
    slot->command_id = g_nvme_next_cid++;
    unsigned short issued_id = slot->command_id;
    __asm__ volatile("" ::: "memory");
    *tail = (unsigned short)((*tail + 1u) % NVME_QUEUE_DEPTH);
    *sq_doorbell = *tail;

    for (unsigned int timeout = 0; timeout < NVME_TIMEOUT; ++timeout) {
        nvme_completion_t* completion = &cq->entries[*head];
        unsigned short status = completion->status;
        if ((status & 1u) == *phase) {
            __asm__ volatile("" ::: "memory");
            int success = completion->command_id == issued_id &&
                          ((status >> 1) & 0x7FFu) == 0;
            *head = (unsigned short)((*head + 1u) % NVME_QUEUE_DEPTH);
            if (*head == 0) *phase ^= 1u;
            *cq_doorbell = *head;
            return success ? 0 : -1;
        }
        if (*nvme_reg32(0x1Cu) & 2u) return -1; /* CSTS.CFS */
    }
    return -1;
}

static int nvme_admin(nvme_command_t* command) {
    return nvme_submit(&g_nvme_admin_sq, &g_nvme_admin_cq,
                       &g_nvme_admin_tail, &g_nvme_admin_head,
                       &g_nvme_admin_phase, nvme_doorbell(0, 0),
                       nvme_doorbell(0, 1), command);
}

static int nvme_identify(unsigned int namespace_id, unsigned int cns, void* buffer) {
    unsigned int address;
    if (nvme_address(buffer, &address) != 0) return -1;
    nvme_command_t command;
    memset(&command, 0, sizeof(command));
    command.opcode = 0x06;
    command.namespace_id = namespace_id;
    command.prp1 = address;
    command.cdw10 = cns;
    return nvme_admin(&command);
}

static int nvme_build_prps(unsigned int address, unsigned int bytes,
                           unsigned int* prp1, unsigned int* prp2) {
    if (!bytes || address > 0xFFFFFFFFu - bytes) return -1;
    *prp1 = address;
    *prp2 = 0;
    unsigned int first_bytes = NVME_PAGE_SIZE - (address & (NVME_PAGE_SIZE - 1u));
    if (bytes <= first_bytes) return 0;
    unsigned int next_page = (address + first_bytes + NVME_PAGE_SIZE - 1u) &
                             ~(NVME_PAGE_SIZE - 1u);
    unsigned int remaining = bytes - first_bytes;
    if (remaining <= NVME_PAGE_SIZE) {
        *prp2 = next_page;
        return 0;
    }
    unsigned int list_address;
    if (nvme_address(g_nvme_prp_list, &list_address) != 0) return -1;
    unsigned int entries = (remaining + NVME_PAGE_SIZE - 1u) / NVME_PAGE_SIZE;
    if (entries > sizeof(g_nvme_prp_list) / sizeof(g_nvme_prp_list[0])) return -1;
    for (unsigned int i = 0; i < entries; ++i)
        g_nvme_prp_list[i] = next_page + i * NVME_PAGE_SIZE;
    *prp2 = list_address;
    return 0;
}

static int nvme_io(unsigned char opcode, unsigned int lba,
                   unsigned char count, void* buffer) {
    nvme_command_t command;
    memset(&command, 0, sizeof(command));
    command.opcode = opcode;
    command.namespace_id = 1;
    if (buffer) {
        unsigned int address, second;
        unsigned int bytes = (unsigned int)count * NVME_SECTOR_SIZE;
        if (nvme_address(buffer, &address) != 0 ||
            nvme_build_prps(address, bytes, &address, &second) != 0) return -1;
        command.prp1 = address;
        command.prp2 = second;
    }
    command.cdw10 = lba;
    command.cdw11 = 0;
    if (opcode == 0x01 || opcode == 0x02)
        command.cdw12 = (unsigned int)count - 1u;
    return nvme_submit(&g_nvme_io_sq, &g_nvme_io_cq,
                       &g_nvme_io_tail, &g_nvme_io_head,
                       &g_nvme_io_phase, nvme_doorbell(1, 0),
                       nvme_doorbell(1, 1), &command);
}

int nvme_probe(unsigned int* sector_count) {
    if (g_nvme_attempted) {
        if (sector_count) *sector_count = g_nvme_sectors;
        return g_nvme_initialized;
    }
    g_nvme_attempted = 1;
    pci_device_t controller;
    if (!pci_find_class_device(NVME_CLASS, NVME_SUBCLASS, NVME_PROGIF, &controller))
        return 0;
    unsigned int bar_low = pci_read_config_dword(controller.bus, controller.slot,
                                                  controller.func, 0x10);
    unsigned int bar_high = pci_read_config_dword(controller.bus, controller.slot,
                                                   controller.func, 0x14);
    if ((bar_low & 1u) || (bar_low & 6u) != 4u) return 0;
    unsigned int mmio = bar_low & ~0x0Fu;
    if (bar_high != 0) {
        /* This kernel has 32-bit physical mappings. Reassign a 64-bit BAR
         * into a reserved PCI MMIO hole when firmware placed it above 4 GiB. */
        unsigned int relocated = nvme_choose_low_mmio(&controller);
        if (!relocated) return 0;
        unsigned short old_command = pci_read_config_word(controller.bus,
            controller.slot, controller.func, 0x04);
        pci_write_config_word(controller.bus, controller.slot, controller.func,
                              0x04, old_command & ~0x0002u);
        pci_write_config_dword(controller.bus, controller.slot, controller.func,
                               0x10, relocated | (bar_low & 0x0Fu));
        pci_write_config_dword(controller.bus, controller.slot, controller.func,
                               0x14, 0);
        pci_write_config_word(controller.bus, controller.slot, controller.func,
                              0x04, old_command);
        bar_low = pci_read_config_dword(controller.bus, controller.slot,
                                         controller.func, 0x10);
        bar_high = pci_read_config_dword(controller.bus, controller.slot,
                                          controller.func, 0x14);
        if (bar_high != 0 || (bar_low & ~0x0Fu) != relocated) return 0;
        mmio = relocated;
    }
    if (!mmio || mmio > 0xFFFFDFFFu) return 0;
    unsigned short command_register = pci_read_config_word(controller.bus,
        controller.slot, controller.func, 0x04);
    pci_write_config_word(controller.bus, controller.slot, controller.func, 0x04,
                          command_register | 0x0006u);
    if (paging_map_mmio_range(mmio, 0x2000u) != 0) return 0;
    g_nvme_registers = mmio;

    unsigned long long capability = *nvme_reg64(0x00);
    unsigned int max_queue_entries = (unsigned int)(capability & 0xFFFFu) + 1u;
    unsigned int min_page_size = (unsigned int)((capability >> 48) & 0x0Fu);
    unsigned int max_page_size = (unsigned int)((capability >> 52) & 0x0Fu);
    if (max_queue_entries < NVME_QUEUE_DEPTH || min_page_size != 0 ||
        max_page_size < min_page_size)
        return 0;
    g_nvme_doorbell_stride = 4u << ((capability >> 32) & 0x0Fu);
    if (g_nvme_doorbell_stride > 128u) return 0;

    unsigned int status = *nvme_reg32(0x1Cu);
    if (status & 1u) {
        *nvme_reg32(0x14u) &= ~1u;
        unsigned int timeout;
        for (timeout = 0; timeout < NVME_TIMEOUT; ++timeout)
            if (!(*nvme_reg32(0x1Cu) & 1u)) break;
        if (timeout == NVME_TIMEOUT) return 0;
    }

    unsigned int admin_sq, admin_cq;
    if (nvme_address(&g_nvme_admin_sq, &admin_sq) != 0 ||
        nvme_address(&g_nvme_admin_cq, &admin_cq) != 0) return 0;
    *nvme_reg32(0x24u) = (NVME_QUEUE_DEPTH - 1u) |
                          ((NVME_QUEUE_DEPTH - 1u) << 16);
    *nvme_reg64(0x28u) = admin_sq;
    *nvme_reg64(0x30u) = admin_cq;
    *nvme_reg32(0x14u) = (6u << 16) | (4u << 20) | 1u;
    unsigned int timeout;
    for (timeout = 0; timeout < NVME_TIMEOUT; ++timeout) {
        status = *nvme_reg32(0x1Cu);
        if (status & 2u) return 0;
        if (status & 1u) break;
    }
    if (timeout == NVME_TIMEOUT) return 0;

    memset(g_nvme_identify, 0, sizeof(g_nvme_identify));
    if (nvme_identify(0, 1, g_nvme_identify) != 0) return 0;
    unsigned int namespace_count = *(unsigned int*)(g_nvme_identify + 516);
    if (namespace_count == 0) return 0;
    memset(g_nvme_namespace, 0, sizeof(g_nvme_namespace));
    if (nvme_identify(1, 0, g_nvme_namespace) != 0) return 0;
    unsigned long long namespace_size = *(unsigned long long*)g_nvme_namespace;
    unsigned int format = g_nvme_namespace[26] & 0x0Fu;
    unsigned int exponent = g_nvme_namespace[128u + format * 4u + 2u];
    if (!namespace_size || exponent != 9u) return 0;

    nvme_command_t create_queue;
    memset(&create_queue, 0, sizeof(create_queue));
    unsigned int io_cq;
    if (nvme_address(&g_nvme_io_cq, &io_cq) != 0) return 0;
    create_queue.opcode = 0x05;
    create_queue.prp1 = io_cq;
    create_queue.cdw10 = ((NVME_QUEUE_DEPTH - 1u) << 16) | 1u;
    create_queue.cdw11 = 1u; /* Physically contiguous, interrupts disabled. */
    if (nvme_admin(&create_queue) != 0) return 0;
    memset(&create_queue, 0, sizeof(create_queue));
    unsigned int io_sq;
    if (nvme_address(&g_nvme_io_sq, &io_sq) != 0) return 0;
    create_queue.opcode = 0x01;
    create_queue.prp1 = io_sq;
    create_queue.cdw10 = ((NVME_QUEUE_DEPTH - 1u) << 16) | 1u;
    create_queue.cdw11 = (1u << 16) | 1u;
    if (nvme_admin(&create_queue) != 0) return 0;

    g_nvme_sectors = namespace_size > 0xFFFFFFFFull
        ? 0xFFFFFFFFu : (unsigned int)namespace_size;
    g_nvme_initialized = g_nvme_sectors != 0;
    if (sector_count) *sector_count = g_nvme_sectors;
    if (g_nvme_initialized)
        print_string("[NVMe] Namespace 1 initialized through native NVMe queues.\n");
    return g_nvme_initialized;
}

int nvme_read_sectors(unsigned int lba, unsigned char count, unsigned short* buffer) {
    if (!g_nvme_initialized || !buffer || !count || lba >= g_nvme_sectors ||
        (unsigned int)count > g_nvme_sectors - lba) return -1;
    return nvme_io(0x02, lba, count, buffer);
}

int nvme_write_sectors(unsigned int lba, unsigned char count,
                       const unsigned short* buffer) {
    if (!g_nvme_initialized || !buffer || !count || lba >= g_nvme_sectors ||
        (unsigned int)count > g_nvme_sectors - lba ||
        nvme_io(0x01, lba, count, (void*)buffer) != 0) return -1;
    return nvme_io(0x00, 0, 0, 0); /* Flush namespace writes. */
}
