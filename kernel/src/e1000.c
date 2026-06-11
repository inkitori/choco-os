// e1000 (Intel 82540EM, PCI 8086:100E) driver in polled mode.
// Rings and packet buffers are physically contiguous PMM pages addressed
// through the HHDM; the NIC sees physical addresses.
#include "e1000.h"
#include "pci.h"
#include "pmm.h"
#include "vmm.h"
#include "kprintf.h"
#include "string.h"
#include "limine.h"

extern volatile struct limine_hhdm_request hhdm_request;

#define E1000_VENDOR 0x8086
#define E1000_DEVICE 0x100E

// registers
#define REG_CTRL 0x0000
#define REG_STATUS 0x0008
#define REG_EERD 0x0014
#define REG_IMC 0x00D8
#define REG_RCTL 0x0100
#define REG_TCTL 0x0400
#define REG_TIPG 0x0410
#define REG_RDBAL 0x2800
#define REG_RDBAH 0x2804
#define REG_RDLEN 0x2808
#define REG_RDH 0x2810
#define REG_RDT 0x2818
#define REG_TDBAL 0x3800
#define REG_TDBAH 0x3804
#define REG_TDLEN 0x3808
#define REG_TDH 0x3810
#define REG_TDT 0x3818
#define REG_RAL 0x5400
#define REG_RAH 0x5404
#define REG_MTA 0x5200

#define CTRL_RST (1u << 26)
#define CTRL_SLU (1u << 6)

#define RCTL_EN (1u << 1)
#define RCTL_BAM (1u << 15)
#define RCTL_SECRC (1u << 26)

#define TCTL_EN (1u << 1)
#define TCTL_PSP (1u << 3)

#define RX_DESC_COUNT 32
#define TX_DESC_COUNT 16
#define RX_BUF_SIZE 2048

typedef struct
{
	uint64_t addr;
	uint16_t length;
	uint16_t checksum;
	uint8_t status;
	uint8_t errors;
	uint16_t special;
} __attribute__((packed)) RxDesc;

typedef struct
{
	uint64_t addr;
	uint16_t length;
	uint8_t cso;
	uint8_t cmd;
	uint8_t status;
	uint8_t css;
	uint16_t special;
} __attribute__((packed)) TxDesc;

static volatile uint32_t *mmio = NULL;
static uint64_t hhdm;

static RxDesc *rx_ring; // virtual (HHDM) pointers
static TxDesc *tx_ring;
static uint64_t rx_ring_phys, tx_ring_phys;
static uint8_t *rx_bufs[RX_DESC_COUNT];
static uint64_t rx_bufs_phys[RX_DESC_COUNT];
static uint8_t *tx_buf;
static uint64_t tx_buf_phys;
static uint32_t rx_tail; // next descriptor we will look at
static uint32_t tx_tail;
static uint8_t mac[6];
static bool up = false;

static inline uint32_t rd(uint32_t reg)
{
	return mmio[reg / 4];
}

static inline void wr(uint32_t reg, uint32_t val)
{
	mmio[reg / 4] = val;
}

static bool eeprom_read(uint8_t addr, uint16_t *out)
{
	wr(REG_EERD, 1 | ((uint32_t)addr << 8));
	for (int i = 0; i < 100000; i++)
	{
		uint32_t v = rd(REG_EERD);
		if (v & (1 << 4))
		{
			*out = v >> 16;
			return true;
		}
	}
	return false;
}

bool e1000_init(void)
{
	PciDevice dev;
	if (!pci_find(E1000_VENDOR, E1000_DEVICE, &dev))
	{
		kprintf("e1000: not found on PCI bus\n");
		return false;
	}
	pci_enable_bus_master(&dev);

	uint64_t bar0 = pci_bar0(&dev);
	if (!bar0)
	{
		kprintf("e1000: unsupported BAR0\n");
		return false;
	}

	hhdm = hhdm_request.response->offset;

	// Map the 128 KiB MMIO window into the HHDM region (uncached).
	uint64_t *pml4 = vmm_get_kernel_pml4();
	for (uint64_t off = 0; off < 0x20000; off += PAGE_SIZE)
		vmm_map_page(pml4, hhdm + bar0 + off, bar0 + off,
					 PTE_PRESENT | PTE_WRITABLE | PTE_CACHE_DISABLE);
	mmio = (volatile uint32_t *)(hhdm + bar0);

	// Reset and bring the link up.
	wr(REG_IMC, 0xFFFFFFFF);
	wr(REG_CTRL, rd(REG_CTRL) | CTRL_RST);
	for (volatile int i = 0; i < 100000; i++)
		;
	wr(REG_IMC, 0xFFFFFFFF);
	wr(REG_CTRL, rd(REG_CTRL) | CTRL_SLU);

	// MAC address from EEPROM (QEMU supports EERD).
	uint16_t w0, w1, w2;
	if (eeprom_read(0, &w0) && eeprom_read(1, &w1) && eeprom_read(2, &w2))
	{
		mac[0] = w0 & 0xFF;
		mac[1] = w0 >> 8;
		mac[2] = w1 & 0xFF;
		mac[3] = w1 >> 8;
		mac[4] = w2 & 0xFF;
		mac[5] = w2 >> 8;
	}
	else
	{
		uint32_t ral = rd(REG_RAL);
		uint32_t rah = rd(REG_RAH);
		mac[0] = ral & 0xFF;
		mac[1] = (ral >> 8) & 0xFF;
		mac[2] = (ral >> 16) & 0xFF;
		mac[3] = (ral >> 24) & 0xFF;
		mac[4] = rah & 0xFF;
		mac[5] = (rah >> 8) & 0xFF;
	}

	// Program our MAC filter + clear the multicast table.
	wr(REG_RAL, (uint32_t)mac[0] | ((uint32_t)mac[1] << 8) |
					((uint32_t)mac[2] << 16) | ((uint32_t)mac[3] << 24));
	wr(REG_RAH, (uint32_t)mac[4] | ((uint32_t)mac[5] << 8) | (1u << 31));
	for (int i = 0; i < 128; i++)
		wr(REG_MTA + i * 4, 0);

	// RX ring: one page of descriptors + a 2 KiB buffer per descriptor.
	rx_ring_phys = (uint64_t)pmm_alloc_page();
	rx_ring = (RxDesc *)(rx_ring_phys + hhdm);
	for (int i = 0; i < RX_DESC_COUNT; i++)
	{
		rx_bufs_phys[i] = (uint64_t)pmm_alloc_page();
		rx_bufs[i] = (uint8_t *)(rx_bufs_phys[i] + hhdm);
		rx_ring[i].addr = rx_bufs_phys[i];
		rx_ring[i].status = 0;
	}
	wr(REG_RDBAL, (uint32_t)rx_ring_phys);
	wr(REG_RDBAH, (uint32_t)(rx_ring_phys >> 32));
	wr(REG_RDLEN, RX_DESC_COUNT * sizeof(RxDesc));
	wr(REG_RDH, 0);
	wr(REG_RDT, RX_DESC_COUNT - 1);
	rx_tail = 0;

	// TX ring + a bounce buffer for frames.
	tx_ring_phys = (uint64_t)pmm_alloc_page();
	tx_ring = (TxDesc *)(tx_ring_phys + hhdm);
	memset(tx_ring, 0, TX_DESC_COUNT * sizeof(TxDesc));
	for (int i = 0; i < TX_DESC_COUNT; i++)
		tx_ring[i].status = 1; // DD: descriptor done / free
	tx_buf_phys = (uint64_t)pmm_alloc_pages(TX_DESC_COUNT / 2); // 2KB each
	tx_buf = (uint8_t *)(tx_buf_phys + hhdm);
	wr(REG_TDBAL, (uint32_t)tx_ring_phys);
	wr(REG_TDBAH, (uint32_t)(tx_ring_phys >> 32));
	wr(REG_TDLEN, TX_DESC_COUNT * sizeof(TxDesc));
	wr(REG_TDH, 0);
	wr(REG_TDT, 0);
	tx_tail = 0;

	// RCTL: enable, broadcast accept, strip CRC, 2 KiB buffers.
	wr(REG_RCTL, RCTL_EN | RCTL_BAM | RCTL_SECRC);
	// TCTL: enable, pad short packets, standard collision params.
	wr(REG_TCTL, TCTL_EN | TCTL_PSP | (0x10 << 4) | (0x40 << 12));
	wr(REG_TIPG, 10 | (10 << 10) | (10 << 20));

	up = true;
	kprintf("e1000: up, MAC %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1],
			mac[2], mac[3], mac[4], mac[5]);
	return true;
}

void e1000_get_mac(uint8_t out[6])
{
	memcpy(out, mac, 6);
}

int e1000_send(const void *frame, size_t len)
{
	if (!up || len > 1518)
		return -1;

	TxDesc *d = &tx_ring[tx_tail];
	// wait for the descriptor to be free
	for (int spin = 0; !(d->status & 1) && spin < 1000000; spin++)
		;
	if (!(d->status & 1))
		return -1;

	uint8_t *slot = tx_buf + (uint64_t)tx_tail * 2048;
	memcpy(slot, frame, len);

	d->addr = tx_buf_phys + (uint64_t)tx_tail * 2048;
	d->length = len;
	d->cmd = (1 << 0) | (1 << 1) | (1 << 3); // EOP | IFCS | RS
	d->status = 0;

	tx_tail = (tx_tail + 1) % TX_DESC_COUNT;
	wr(REG_TDT, tx_tail);
	return (int)len;
}

int e1000_recv(void *buf, size_t cap)
{
	if (!up)
		return 0;

	RxDesc *d = &rx_ring[rx_tail];
	if (!(d->status & 1)) // DD
		return 0;

	int len = d->length;
	if ((size_t)len > cap)
		len = cap;
	memcpy(buf, rx_bufs[rx_tail], len);

	d->status = 0;
	wr(REG_RDT, rx_tail); // hand the descriptor back
	rx_tail = (rx_tail + 1) % RX_DESC_COUNT;
	return len;
}
