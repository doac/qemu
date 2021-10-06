/*
 * QEMU RISC-V Spike Board
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017-2018 SiFive, Inc.
 *
 * This provides a RISC-V Board with the following devices:
 *
 * 0) HTIF Console and Poweroff
 * 1) CLINT (Timer and IPI)
 * 2) PLIC (Platform Level Interrupt Controller)
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

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "target/riscv/cpu.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/riscv/noel.h"
#include "hw/riscv/boot.h"
#include "hw/riscv/numa.h"
#include "hw/sparc/grlib.h"
#include "hw/intc/sifive_clint.h"
#include "hw/intc/sifive_plic.h"
#include "hw/qdev-properties.h"
#include "qemu/datadir.h"
#include "chardev/char.h"
#include "sysemu/arch_init.h"
#include "sysemu/device_tree.h"
#include "sysemu/sysemu.h"
#include "hw/misc/grlib_ahb_apb_pnp.h"

#if defined(TARGET_RISCV32)
#define NOEL_CPU TYPE_RISCV_CPU_BASE32
#elif defined(TARGET_RISCV64)
#define NOEL_CPU TYPE_RISCV_CPU_BASE64
#endif

static const MemMapEntry noel_memmap[] = {
    [NOEL_DRAM]  =    {        0x0,        0x0 },
    [NOEL_MROM]  =    { 0xc0000000,      0x100 },
    [NOEL_CLINT] =    { 0xe0000000,   0x100000 },
    [NOEL_PLIC]  =    { 0xf8000000,  0x4000000 },
    [NOEL_UART0] =    { 0xfc001000,      0x100 },
    [NOEL_GRETH] =    { 0xfc084000,      0x100 },    
};

static void create_fdt(NoelState *s, const MemMapEntry *memmap,
                       uint64_t mem_size, const char *cmdline)
{
    void *fdt;
    int cpu;
    uint32_t *cells;
    char *nodename;
    uint32_t plic_phandle, phandle = 1;
    uint32_t clock_phandle;
    const char *dtb_filename;
    int size;
    char macaddr[6];
    MachineState *machine = MACHINE(s);

    dtb_filename = machine->dtb;
    if (dtb_filename) {
        char *filename;
        filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, dtb_filename);
        if (!filename) {
            fprintf(stderr, "Couldn't open dtb file %s\n", dtb_filename);
            return;
        }

        fdt = s->fdt = load_device_tree(filename, &size);
        if (!fdt) {
            fprintf(stderr, "Couldn't open dtb file %s\n", filename);
            g_free(filename);
            return;
        }
        g_free(filename);
        return;
    }

    fdt = s->fdt = create_device_tree(&s->fdt_size);
    if (!fdt) {
        error_report("create_device_tree() failed");
        exit(1);
    }

    qemu_fdt_setprop_string(fdt, "/", "model", "qemu-noelv");
    qemu_fdt_setprop_string(fdt, "/", "compatible", "gaisler,noelv");
    qemu_fdt_setprop_cell(fdt, "/", "#size-cells", 0x1);
    qemu_fdt_setprop_cell(fdt, "/", "#address-cells", 0x1);

    qemu_fdt_add_subnode(fdt, "/soc");
    qemu_fdt_setprop(fdt, "/soc", "ranges", NULL, 0);
    qemu_fdt_setprop_string(fdt, "/soc", "compatible", "simple-bus");
    qemu_fdt_setprop_cell(fdt, "/soc", "#size-cells", 0x1);
    qemu_fdt_setprop_cell(fdt, "/soc", "#address-cells", 0x1);

    nodename = g_strdup_printf("/memory@%lx",
        (long)memmap[NOEL_DRAM].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
        memmap[NOEL_DRAM].base,
        mem_size);
    qemu_fdt_setprop_string(fdt, nodename, "device_type", "memory");
    g_free(nodename);

    qemu_fdt_add_subnode(fdt, "/cpus");
    qemu_fdt_setprop_cell(fdt, "/cpus", "timebase-frequency",
                          SIFIVE_CLINT_TIMEBASE_FREQ);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#size-cells", 0x0);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#address-cells", 0x1);

    for (cpu = s->soc.num_harts - 1; cpu >= 0; cpu--) {
        int intc_phandle;
        nodename = g_strdup_printf("/cpus/cpu@%d", cpu);
        char *intc = g_strdup_printf("/cpus/cpu@%d/interrupt-controller", cpu);
        char *isa = riscv_isa_string(&s->soc.harts[cpu]);
        qemu_fdt_add_subnode(fdt, nodename);
#if defined(TARGET_RISCV32)
        qemu_fdt_setprop_string(fdt, nodename, "mmu-type", "riscv,sv32");
#else
        /* TODO sv39 does not seem to be supported */
        qemu_fdt_setprop_string(fdt, nodename, "mmu-type", "riscv,sv48");
#endif
        qemu_fdt_setprop_string(fdt, nodename, "riscv,isa", isa);
        qemu_fdt_setprop_string(fdt, nodename, "compatible", "riscv");
        qemu_fdt_setprop_string(fdt, nodename, "status", "okay");
        qemu_fdt_setprop_cell(fdt, nodename, "reg", cpu);
        qemu_fdt_setprop_string(fdt, nodename, "device_type", "cpu");
        intc_phandle = phandle++;
        qemu_fdt_add_subnode(fdt, intc);
        qemu_fdt_setprop_cell(fdt, intc, "phandle", intc_phandle);
        qemu_fdt_setprop_string(fdt, intc, "compatible", "riscv,cpu-intc");
        qemu_fdt_setprop(fdt, intc, "interrupt-controller", NULL, 0);
        qemu_fdt_setprop_cell(fdt, intc, "#interrupt-cells", 1);
        g_free(isa);
        g_free(intc);
        g_free(nodename);
    }

    cells =  g_new0(uint32_t, s->soc.num_harts * 4);
    for (cpu = 0; cpu < s->soc.num_harts; cpu++) {
        nodename =
            g_strdup_printf("/cpus/cpu@%d/interrupt-controller", cpu);
        uint32_t intc_phandle = qemu_fdt_get_phandle(fdt, nodename);
        cells[cpu * 4 + 0] = cpu_to_be32(intc_phandle);
        cells[cpu * 4 + 1] = cpu_to_be32(IRQ_M_SOFT);
        cells[cpu * 4 + 2] = cpu_to_be32(intc_phandle);
        cells[cpu * 4 + 3] = cpu_to_be32(IRQ_M_TIMER);
        g_free(nodename);
    }
    nodename = g_strdup_printf("/soc/clint@%lx",
        (long)memmap[NOEL_CLINT].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "riscv,clint0");
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
        memmap[NOEL_CLINT].base,
        memmap[NOEL_CLINT].size);
    qemu_fdt_setprop(fdt, nodename, "interrupts-extended",
        cells, s->soc.num_harts * sizeof(uint32_t) * 4);
    g_free(cells);
    g_free(nodename);

    clock_phandle = phandle++;
    qemu_fdt_add_subnode(fdt, "/sysclock");
    qemu_fdt_setprop_cell(fdt, "/sysclock", "phandle", clock_phandle);
    qemu_fdt_setprop_cell(fdt, "/sysclock", "#clock-cells", 0x0);
    qemu_fdt_setprop_string(fdt, "/sysclock", "compatible", "fixed-clock");
    qemu_fdt_setprop_cell(fdt, "/sysclock", "clock-frequency", 100000000);

    plic_phandle = phandle++;
    cells =  g_new0(uint32_t, s->soc.num_harts * 4);
    for (cpu = 0; cpu < s->soc.num_harts; cpu++) {
        nodename =
            g_strdup_printf("/cpus/cpu@%d/interrupt-controller", cpu);
        uint32_t intc_phandle = qemu_fdt_get_phandle(fdt, nodename);
        cells[cpu * 4 + 0] = cpu_to_be32(intc_phandle);
        cells[cpu * 4 + 1] = cpu_to_be32(IRQ_M_EXT);
        cells[cpu * 4 + 2] = cpu_to_be32(intc_phandle);
        cells[cpu * 4 + 3] = cpu_to_be32(IRQ_S_EXT);
        cells[cpu * 4 + 2] = cpu_to_be32(intc_phandle);
        cells[cpu * 4 + 3] = cpu_to_be32(IRQ_U_EXT);
        cells[cpu * 4 + 2] = cpu_to_be32(intc_phandle);
        cells[cpu * 4 + 3] = cpu_to_be32(IRQ_VS_EXT);
        g_free(nodename);
    }
    nodename = g_strdup_printf("/soc/interrupt-controller@%lx",
        (long)memmap[NOEL_PLIC].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_cell(fdt, nodename, "#address-cells",
                          FDT_PLIC_ADDR_CELLS);
    qemu_fdt_setprop_cell(fdt, nodename, "#interrupt-cells",
                          FDT_PLIC_INT_CELLS);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "riscv,plic0");
    qemu_fdt_setprop(fdt, nodename, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop(fdt, nodename, "interrupts-extended",
        cells, s->soc.num_harts * sizeof(uint32_t) * 4);
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
       memmap[NOEL_PLIC].base,
       memmap[NOEL_PLIC].size);
    qemu_fdt_setprop_cell(fdt, nodename, "phandle", plic_phandle);
    qemu_fdt_setprop_cell(fdt, nodename, "riscv,ndev", NOEL_NDEV);
    qemu_fdt_setprop_cell(fdt, nodename, "riscv,max-priority",
                          NOEL_PLIC_NUM_PRIORITIES);
    plic_phandle = qemu_fdt_get_phandle(fdt, nodename);
    g_free(cells);
    g_free(nodename);

    nodename = g_strdup_printf("/soc/greth@%lx",
        (long)memmap[NOEL_GRETH].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "gaisler,greth");
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
        0x0, memmap[NOEL_GRETH].base,
        0x0, memmap[NOEL_GRETH].size);
    qemu_fdt_setprop_cell(fdt, nodename, "interrupt-parent", plic_phandle);
    qemu_fdt_setprop_cell(fdt, nodename, "interrupts", 5);

    macaddr[0] = 0x00;
    macaddr[1] = 0x50;
    macaddr[2] = 0xc2;
    macaddr[3] = 0x75;
    macaddr[4] = 0xa3;
    macaddr[5] = 0x52;

    qemu_fdt_setprop(fdt, nodename, "local-mac-address", macaddr, 6);

    nodename = g_strdup_printf("/soc/uart@%lx",
        (long)memmap[NOEL_UART0].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "gaisler,apbuart");
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
        memmap[NOEL_UART0].base,
        memmap[NOEL_UART0].size);
    qemu_fdt_setprop_cell(fdt, nodename, "interrupt-parent", plic_phandle);
    qemu_fdt_setprop_cell(fdt, nodename, "current-speed", 115200);
    qemu_fdt_setprop_cell(fdt, nodename, "clocks", clock_phandle);
    qemu_fdt_setprop_cell(fdt, nodename, "interrupts", UART0_IRQ);

    qemu_fdt_add_subnode(fdt, "/aliases");
    qemu_fdt_setprop_string(fdt, "/aliases", "serial0", nodename);

    qemu_fdt_add_subnode(fdt, "/chosen");
    qemu_fdt_setprop_string(fdt, "/chosen", "stdout-path", nodename);
    if (cmdline) {
        qemu_fdt_setprop_string(fdt, "/chosen", "bootargs", cmdline);
    }
    g_free(nodename);
}

static void noel_board_init(MachineState *machine)
{
    const MemMapEntry *memmap = noel_memmap;
    NoelState *s = NOEL_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *main_mem = g_new(MemoryRegion, 1);
    MemoryRegion *mask_rom = g_new(MemoryRegion, 1);
    target_ulong firmware_end_addr, kernel_start_addr;
    char *plic_hart_config;
    size_t plic_hart_config_len;
    uint32_t fdt_load_addr;
    uint64_t kernel_entry;
    DeviceState *dev;
    int i;
    unsigned int smp_cpus = machine->smp.cpus;
    const char *bios_name = machine->firmware;
    AHBPnp *ahb_pnp;
    APBPnp *apb_pnp;

    /* Initialize SOC */
    object_initialize_child(OBJECT(machine), "soc", &s->soc,
                            TYPE_RISCV_HART_ARRAY);
    object_property_set_str(OBJECT(&s->soc), "cpu-type", machine->cpu_type,
                            &error_abort);
    object_property_set_int(OBJECT(&s->soc), "num-harts", smp_cpus,
                            &error_abort);
    object_property_set_int(OBJECT(&s->soc), "resetvec", 0xc0000000,
                            &error_abort);

    sysbus_realize(SYS_BUS_DEVICE(&s->soc), &error_abort);

    /* register system main memory (actual RAM) */
    memory_region_init_ram(main_mem, NULL, "riscv.noel.ram",
                           machine->ram_size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[NOEL_DRAM].base,
        main_mem);

    /* create device tree */
    create_fdt(s, memmap, machine->ram_size, machine->kernel_cmdline);

    /* boot rom */
    memory_region_init_rom(mask_rom, NULL, "riscv.noel.mrom",
                           memmap[NOEL_MROM].size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[NOEL_MROM].base,
                                mask_rom);

    bios_name = bios_name?: (riscv_is_32bit(&s->soc) ?
                             "opensbi-riscv32-generic-fw_dynamic.bin" :
                             "opensbi-riscv64-generic-fw_dynamic.bin");

    firmware_end_addr = riscv_find_and_load_firmware(machine,
                                bios_name,
                                memmap[NOEL_DRAM].base, NULL);

    if (machine->kernel_filename) {
        kernel_start_addr = riscv_calc_kernel_start_addr(&s->soc,
                                                         firmware_end_addr);

        kernel_entry = riscv_load_kernel(machine->kernel_filename,
                                         kernel_start_addr, NULL);

        if (machine->initrd_filename) {
            hwaddr start;
            hwaddr end = riscv_load_initrd(machine->initrd_filename,
                                           machine->ram_size, kernel_entry,
                                           &start);
            qemu_fdt_setprop_cell(s->fdt, "/chosen",
                                  "linux,initrd-start", start);
            qemu_fdt_setprop_cell(s->fdt, "/chosen", "linux,initrd-end",
                                  end);
        }
    } else {
       /*
        * If dynamic firmware is used, it doesn't know where is the next mode
        * if kernel argument is not set.
        */
        kernel_entry = 0;
    }

    /* Compute the fdt load address in dram */
    fdt_load_addr = riscv_load_fdt(memmap[NOEL_DRAM].base,
                                   machine->ram_size, s->fdt);

    /* TODO replace mrom with what we have */

    /* load the reset vector */
    riscv_setup_rom_reset_vec(machine, &s->soc, memmap[NOEL_DRAM].base,
                              memmap[NOEL_MROM].base,
                              memmap[NOEL_MROM].size, kernel_entry,
                              fdt_load_addr, s->fdt);

    /* create PLIC hart topology configuration string */
    plic_hart_config_len = (strlen(NOEL_PLIC_HART_CONFIG) + 1) * smp_cpus;
    plic_hart_config = g_malloc0(plic_hart_config_len);
    for (i = 0; i < smp_cpus; i++) {
        if (i != 0) {
            strncat(plic_hart_config, ",", plic_hart_config_len);
        }
        strncat(plic_hart_config, NOEL_PLIC_HART_CONFIG, plic_hart_config_len);
        plic_hart_config_len -= (strlen(NOEL_PLIC_HART_CONFIG) + 1);
    }
    s->plic = sifive_plic_create(memmap[NOEL_PLIC].base,
        plic_hart_config, 0,
        NOEL_PLIC_NUM_SOURCES,
        NOEL_PLIC_NUM_PRIORITIES,
        NOEL_PLIC_PRIORITY_BASE,
        NOEL_PLIC_PENDING_BASE,
        NOEL_PLIC_ENABLE_BASE,
        NOEL_PLIC_ENABLE_STRIDE,
        NOEL_PLIC_CONTEXT_BASE,
        NOEL_PLIC_CONTEXT_STRIDE,
        memmap[NOEL_PLIC].size);

    sifive_clint_create(memmap[NOEL_CLINT].base,
        memmap[NOEL_CLINT].size, 0, smp_cpus,
        SIFIVE_SIP_BASE, SIFIVE_TIMECMP_BASE, SIFIVE_TIME_BASE,
        SIFIVE_CLINT_TIMEBASE_FREQ, false);

    /* Allocate uart */
    dev = qdev_new(TYPE_GRLIB_APB_UART);
    qdev_prop_set_chr(dev, "chrdev", serial_hd(0));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, memmap[NOEL_UART0].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0,
                       qdev_get_gpio_in(DEVICE(s->plic), UART0_IRQ));

    ahb_pnp = GRLIB_AHB_PNP(qdev_new(TYPE_GRLIB_AHB_PNP));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(ahb_pnp), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(ahb_pnp), 0, 0xFFFFF000);

    apb_pnp = GRLIB_APB_PNP(qdev_new(TYPE_GRLIB_APB_PNP));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(apb_pnp), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(apb_pnp), 0, 0xfc0ff000);
    grlib_ahb_pnp_add_entry(ahb_pnp, 0xfc000000, 0xfffff,
                            GRLIB_VENDOR_GAISLER, GRLIB_APBMST_DEV,
                            GRLIB_AHB_SLAVE, GRLIB_AHBMEM_AREA);

    grlib_apb_pnp_add_entry(apb_pnp, 0xfc001000, 0xFFF,
                            GRLIB_VENDOR_GAISLER, GRLIB_APBUART_DEV, 1,
                            1, GRLIB_APBIO_AREA);

    grlib_ahb_pnp_add_entry(ahb_pnp, 0xf8000000, 0x3ffffff,
                            GRLIB_VENDOR_GAISLER, GRLIB_PLIC_DEV,
                            GRLIB_AHB_SLAVE, GRLIB_AHBMEM_AREA);

    grlib_ahb_pnp_add_entry(ahb_pnp, 0xe0000000, 0xFFFFF,
                            GRLIB_VENDOR_GAISLER, GRLIB_CLINT_DEV,
                            GRLIB_AHB_SLAVE, GRLIB_AHBMEM_AREA);
}

static void noel_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "RISC-V NOEL board";
    mc->init = noel_board_init;
    mc->max_cpus = NOEL_CPUS_MAX;
    mc->default_cpu_type = NOEL_CPU;
}

static const TypeInfo noel_machine_typeinfo = {
    .name       = MACHINE_TYPE_NAME("noel"),
    .parent     = TYPE_MACHINE,
    .class_init = noel_machine_class_init,
    .instance_size = sizeof(NoelState),
};

static void noel_machine_init_register_types(void)
{
    type_register_static(&noel_machine_typeinfo);
}

type_init(noel_machine_init_register_types)
