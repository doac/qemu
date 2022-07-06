/*
 * QEMU RISC-V NoelIO machine interface
 *
 * Copyright (c) 2017 SiFive, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_RISCV_NOEL_H
#define HW_RISCV_NOEL_H

#include "hw/riscv/riscv_hart.h"
#include "hw/sysbus.h"

#define NOEL_CPUS_MAX 4

#define TYPE_NOEL_MACHINE MACHINE_TYPE_NAME("noel")
#define NOEL_MACHINE(obj) \
    OBJECT_CHECK(NoelState, (obj), TYPE_NOEL_MACHINE)

typedef struct {
    /*< private >*/
    MachineState parent;

    /*< public >*/
    RISCVHartArrayState soc;
    DeviceState *plic;

    void *fdt;
    int fdt_size;
} NoelState;

enum {
    NOEL_MROM,
    NOEL_CLINT,
    NOEL_PLIC,
    NOEL_UART0,
    NOEL_DRAM,
    NOEL_GRETH,
};

enum {
    UART0_IRQ = 1,
    GRETH_IRQ = 5,
    NOEL_NDEV = 31
};

#define NOEL_PLIC_HART_CONFIG "MSH"
#define NOEL_PLIC_NUM_SOURCES 127
#define NOEL_PLIC_NUM_PRIORITIES 7
#define NOEL_PLIC_PRIORITY_BASE 0x04
#define NOEL_PLIC_PENDING_BASE 0x1000
#define NOEL_PLIC_ENABLE_BASE 0x2000
#define NOEL_PLIC_ENABLE_STRIDE 0x80
#define NOEL_PLIC_CONTEXT_BASE 0x200000
#define NOEL_PLIC_CONTEXT_STRIDE 0x1000

#define FDT_PCI_ADDR_CELLS    3
#define FDT_PCI_INT_CELLS     1
#define FDT_PLIC_ADDR_CELLS   0
#define FDT_PLIC_INT_CELLS    1
#define FDT_INT_MAP_WIDTH     (FDT_PCI_ADDR_CELLS + FDT_PCI_INT_CELLS + 1 + \
                               FDT_PLIC_ADDR_CELLS + FDT_PLIC_INT_CELLS)

#if defined(TARGET_RISCV32)
#define NOEL_CPU TYPE_RISCV_CPU_BASE32
#elif defined(TARGET_RISCV64)
#define NOEL_CPU TYPE_RISCV_CPU_BASE64
#endif

#endif
