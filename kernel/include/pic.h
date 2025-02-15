#ifndef PIC_H
#define PIC_H

#define PIC1 0x20 /* IO base address for master PIC */
#define PIC2 0xA0 /* IO base address for slave PIC */
#define PIC1_COMMAND PIC1
#define PIC1_DATA (PIC1 + 1)
#define PIC2_COMMAND PIC2
#define PIC2_DATA (PIC2 + 1)
/* reinitialize the PIC controllers, giving them specified vector offsets
   rather than 8h and 70h, as configured by default */

#define ICW1_ICW4 0x01      /* Indicates that ICW4 will be present */
#define ICW1_SINGLE 0x02    /* Single (cascade) mode */
#define ICW1_INTERVAL4 0x04 /* Call address interval 4 (8) */
#define ICW1_LEVEL 0x08     /* Level triggered (edge) mode */
#define ICW1_INIT 0x10      /* Initialization - required! */

#define ICW4_8086 0x01       /* 8086/88 (MCS-80/85) mode */
#define ICW4_AUTO 0x02       /* Auto (normal) EOI */
#define ICW4_BUF_SLAVE 0x08  /* Buffered mode/slave */
#define ICW4_BUF_MASTER 0x0C /* Buffered mode/master */
#define ICW4_SFNM 0x10       /* Special fully nested (not) */

#define PIC_EOI 0x20 /* End-of-interrupt command code */

#define PIC_MASTER_OFFSET 0x20 // this represents a shift by 32 (because we have 32 exceptions)
#define PIC_SLAVE_OFFSET 0x28

#define PIC_SLAVE_IRQ_START 8

#define PIC_TIMER_IRQ_LINE 0
#define PIC_KEYBOARD_IRQ_LINE 1

#include "stdint.h"

void pic_init();
void pic_remap(int offset1, int offset2);
void pic_mask_irq(uint8_t irq);
void pic_unmask_irq(uint8_t irq); // seems unintuitive but unmasking is the same as enabling
void pic_send_eoi(uint8_t irq);

#endif