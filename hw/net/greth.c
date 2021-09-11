/*
 * This code is licensed under the GPL.
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "net/net.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include <zlib.h>
#include "qom/object.h"

#define TYPE_GRETH "grlib-greth"
OBJECT_DECLARE_SIMPLE_TYPE(grethState, GRETH)

struct grethState {
    SysBusDevice parent_obj;

    NICState *nic;
    NICConf conf;
    qemu_irq irq;
    MemoryRegion mmio;
};

static ssize_t greth_receive(NetClientState *nc, const uint8_t *buf,
                            size_t size)
{
    grethState *s = qemu_get_nic_opaque(nc);

    (void)s;

    return 0;
}

static uint64_t greth_read(void *opaque, hwaddr offset, unsigned size)
{
    grethState *s = (grethState *)opaque;
    (void)s;

    switch (offset) {
    case 0x00:
        printf("Reading reg 0\n");
        return 0;
    default:
        printf("GRETH: Unhandled read access to offset 0x%lx\n", offset);
        return 0;
    }
}

static void greth_write(void *opaque, hwaddr offset, uint64_t value,
                        unsigned size)
{
    grethState *s = (grethState *)opaque;
    (void)s;

    switch (offset) {
    case 0x00:
        printf("Writing reg 0\n");
        break;
    default:
        printf("GRETH: Unhandled write access to offset 0x%lx\n", offset);
    }
}

static const MemoryRegionOps greth_ops = {
    .read = greth_read,
    .write = greth_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void greth_reset(DeviceState *dev)
{
    grethState *s =  GRETH(dev);
    (void)s;
}

static NetClientInfo net_greth_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .receive = greth_receive,
};

static void greth_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    grethState *s = GRETH(dev);

    memory_region_init_io(&s->mmio, OBJECT(s), &greth_ops, s,
                          "grlib-greth", 0x100);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
    qemu_macaddr_default_if_unset(&s->conf.macaddr);

    s->nic = qemu_new_nic(&net_greth_info, &s->conf,
                          object_get_typename(OBJECT(dev)), dev->id, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);
}

static Property greth_properties[] = {
    DEFINE_NIC_PROPERTIES(grethState, conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void greth_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = greth_realize;
    dc->reset = greth_reset;
    device_class_set_props(dc, greth_properties);
}

static const TypeInfo greth_info = {
    .name          = TYPE_GRETH,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(grethState),
    .class_init    = greth_class_init,
};

static void greth_register_types(void)
{
    type_register_static(&greth_info);
}

type_init(greth_register_types)
