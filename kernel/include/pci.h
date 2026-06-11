#ifndef PCI_H
#define PCI_H

#include <stdint.h>
#include <stdbool.h>

typedef struct
{
	uint8_t bus, slot, func;
	uint16_t vendor_id, device_id;
} PciDevice;

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset,
				 uint32_t value);

// Find the first device matching vendor/device id. Returns false if absent.
bool pci_find(uint16_t vendor, uint16_t device, PciDevice *out);

// BAR0 memory base (masked) and bus-mastering enable.
uint64_t pci_bar0(const PciDevice *dev);
void pci_enable_bus_master(const PciDevice *dev);

#endif
