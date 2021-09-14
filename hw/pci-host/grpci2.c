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

static void grpci2_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    grpci2State *s = GRPCI2(dev);

    memory_region_init_io(&s->mmio, OBJECT(s), &grpci2_ops, s,
                          "grlib-grpci2", 0x100);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
}

static Property grpci2_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void grpci2_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = grpci2_realize;
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
