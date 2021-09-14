/*
 * This code is licensed under the GPL.
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_host.h"
#include "qemu/bswap.h"
#include "qemu/module.h"
#include "qom/object.h"

#define TYPE_GRPCI2 "grlib-grpci2"
OBJECT_DECLARE_SIMPLE_TYPE(grpci2State, GRPCI2)

struct grpci2State {
    PCIHostState parent_obj;

    uint32_t ctrl;
    uint32_t status;

    qemu_irq irq;
    MemoryRegion mmio;
    MemoryRegion pci_mem;
    MemoryRegion conf_mem;
    MemoryRegion data_mem;
};

static uint64_t grpci2_read(void *opaque, hwaddr offset, unsigned size)
{
    grpci2State *s = (grpci2State *)opaque;

    switch (offset) {
    case 0x00: /* control */
        return s->ctrl;
    case 0x04: /* status and capability */
        return s->status;
    case 0x08: /* PCI master prefetch burst limit */
        return 0xff;
    default:
        printf("GRPCI2: Unhandled read access to offset 0x%lx\n", offset);
        return 0;
    }

    return 0;
}

static void grpci2_write(void *opaque, hwaddr offset, uint64_t value,
                        unsigned size)
{
    grpci2State *s = (grpci2State *)opaque;

    switch (offset) {
    case 0x00: /* control */
        s->ctrl = value;
        return;
    case 0x04: /* status and capability */
        return;
    default:
        printf("GRPCI2: Unhandled write of value 0x%lx to offset 0x%lx\n",
               value, offset);
    }
}

static const MemoryRegionOps grpci2_ops = {
    .read = grpci2_read,
    .write = grpci2_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};


static uint64_t grpci2_conf_read(void *opaque, hwaddr offset, unsigned size)
{
    grpci2State *s = (grpci2State *)opaque;
    PCIHostState *phb = PCI_HOST_BRIDGE(s);

    return pci_data_read(phb->bus, offset, size);
}

static void grpci2_conf_write(void *opaque, hwaddr offset, uint64_t value,
                        unsigned size)
{
    grpci2State *s = (grpci2State *)opaque;
    PCIHostState *phb = PCI_HOST_BRIDGE(s);

    return pci_data_write(phb->bus, offset, value, size);
}

static const MemoryRegionOps grpci2_conf_ops = {
    .read = grpci2_conf_read,
    .write = grpci2_conf_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static int grpci2_map_irq(PCIDevice *pci_dev, int irq_num)
{
    return (irq_num + (pci_dev->devfn >> 3)) & 3;
}

static void grpci2_set_irq(void *opaque, int irq_num, int level)
{
    grpci2State *s = opaque;

    if (!level)
        s->status |= (irq_num << 8);
    else
        s->status &= ~(irq_num << 8);

    qemu_set_irq(s->irq, level);
}

static void grpci2_reset(DeviceState *dev)
{
    grpci2State *s =  GRPCI2(dev);

    s->ctrl = 0;
    s->status = (1 << 30) | (1 << 29) | (1 << 26) | (1 << 20);
}

static void grpci2_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    PCIHostState *phb = PCI_HOST_BRIDGE(dev);
    grpci2State *s = GRPCI2(dev);

    memory_region_init_io(&s->mmio, OBJECT(s), &grpci2_ops, s,
                          "grlib-grpci2", 0x100);

    memory_region_init(&s->pci_mem, OBJECT(s), "grlib-grpci2-mem", 0x40000000);

    memory_region_init_io(&s->data_mem, OBJECT(s), &grpci2_conf_ops, s,
                          "grlib-grpci2-data", 0x10000);

    memory_region_init_io(&s->conf_mem, OBJECT(s), &grpci2_conf_ops, s,
                          "grlib-grpci2-conf", 0x10000);

    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_mmio(sbd, &s->data_mem);
    sysbus_init_mmio(sbd, &s->conf_mem);
    sysbus_init_irq(sbd, &s->irq);

    phb->bus = pci_register_root_bus(dev, NULL,
                                     grpci2_set_irq,
                                     grpci2_map_irq,
                                     s,
                                     get_system_memory(),
                                     &s->data_mem,
                                     PCI_DEVFN(6, 0), 4, TYPE_PCI_BUS);
}

static Property grpci2_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void grpci2_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = grpci2_realize;
    dc->reset = grpci2_reset;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    device_class_set_props(dc, grpci2_properties);
}

static const TypeInfo grpci2host_info = {
    .name          = TYPE_GRPCI2,
    .parent        = TYPE_PCI_HOST_BRIDGE,
    .instance_size = sizeof(grpci2State),
    .class_init    = grpci2_class_init,
};

static void grpci2_register_types(void)
{
    type_register_static(&grpci2host_info);
}

type_init(grpci2_register_types)
