// Legacy PCI configuration space access via ports 0xCF8/0xCFC.
#include "pci.h"
#include "io.h"

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

static uint32_t config_address(uint8_t bus, uint8_t slot, uint8_t func,
							   uint8_t offset)
{
	return 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
		   ((uint32_t)func << 8) | (offset & 0xFC);
}

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
	outl(PCI_CONFIG_ADDR, config_address(bus, slot, func, offset));
	return inl(PCI_CONFIG_DATA);
}

void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset,
				 uint32_t value)
{
	outl(PCI_CONFIG_ADDR, config_address(bus, slot, func, offset));
	outl(PCI_CONFIG_DATA, value);
}

bool pci_find(uint16_t vendor, uint16_t device, PciDevice *out)
{
	for (int bus = 0; bus < 256; bus++)
	{
		for (int slot = 0; slot < 32; slot++)
		{
			for (int func = 0; func < 8; func++)
			{
				uint32_t id = pci_read32(bus, slot, func, 0);
				if ((id & 0xFFFF) == 0xFFFF)
				{
					if (func == 0)
						break; // no device in this slot
					continue;
				}
				if ((id & 0xFFFF) == vendor && (id >> 16) == device)
				{
					out->bus = bus;
					out->slot = slot;
					out->func = func;
					out->vendor_id = vendor;
					out->device_id = device;
					return true;
				}
				// only scan other functions on multi-function devices
				if (func == 0 &&
					!(pci_read32(bus, slot, 0, 0x0C) & 0x800000))
					break;
			}
		}
	}
	return false;
}

uint64_t pci_bar0(const PciDevice *dev)
{
	uint32_t bar = pci_read32(dev->bus, dev->slot, dev->func, 0x10);
	if (bar & 1)
		return 0; // I/O space BAR, not supported here

	uint64_t addr = bar & ~0xFull;
	if (((bar >> 1) & 3) == 2) // 64-bit BAR
	{
		uint32_t high = pci_read32(dev->bus, dev->slot, dev->func, 0x14);
		addr |= (uint64_t)high << 32;
	}
	return addr;
}

void pci_enable_bus_master(const PciDevice *dev)
{
	uint32_t cmd = pci_read32(dev->bus, dev->slot, dev->func, 0x04);
	cmd |= 0x7; // I/O space, memory space, bus master
	pci_write32(dev->bus, dev->slot, dev->func, 0x04, cmd);
}
