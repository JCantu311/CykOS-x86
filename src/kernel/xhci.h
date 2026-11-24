#ifndef CYKOS_XHCI_H
#define CYKOS_XHCI_H

/* Minimal xHCI register/ring layer. Device enumeration uses this backend. */
int xhci_init(unsigned int capability_base);
unsigned int xhci_port_count(void);
unsigned int xhci_port_status(unsigned int port);
unsigned int xhci_port_speed(unsigned int port);
int xhci_reset_port(unsigned int port);
int xhci_prepare_port(unsigned int port);
int xhci_release_device(unsigned char address);
void xhci_bind_address(unsigned char address);
unsigned int xhci_slot_for_address(unsigned char address);
int xhci_update_ep0_packet(unsigned char address, unsigned short max_packet);
int xhci_control_transfer(unsigned char address, const unsigned char setup[8],
                         void* buffer, unsigned short length, unsigned short* actual);
int xhci_configure_endpoint(unsigned char address, unsigned char endpoint_address,
                            unsigned char transfer_type, unsigned short max_packet,
                            unsigned char interval, unsigned char max_burst);
int xhci_transfer(unsigned char address, unsigned char endpoint_address, int input,
                  void* buffer, unsigned short length, unsigned short* actual);

#endif
