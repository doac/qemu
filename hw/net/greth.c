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

    uint32_t ctrl;
    uint32_t mdio;
    uint32_t txdesc;
    uint32_t rxdesc;

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

static void greth_mdio(grethState *s, uint64_t value)
{
    uint32_t phy_address;
    uint32_t reg_address;
    uint32_t read_op;

    const uint32_t mdio_regs[] = {0x1140, 0x796d, 0x0022, 0x1612,
                                  0x01e1, 0xcde1, 0x000d, 0x2001,
                                  0x4006, 0x0300, 0x3c00, 0x0000,
                                  0x0000, 0xaaaa, 0x0000, 0x3000,
                                  0x0000, 0x00f0, 0x0000, 0x0006,
                                  0x44fe, 0x0000, 0x0000, 0x0200,
                                  0x0000, 0x0000, 0x0000, 0x0000,
                                  0x0000, 0x0000, 0x0000, 0x0348};

    phy_address = (value >> 11) & 0x1f;
    reg_address = (value >> 6) & 0x1f;

    read_op = value & 2;

    value &= ~0x3f;

    if (phy_address != 1) {
        /* Link fail */
        s->mdio = value | 0xffff0004;
        return;
    }

    if (!read_op) {
        /* Ignore writes */
        s->mdio = value;
        return;
    }

    s->mdio = (value & ~(0xffff0000)) | (mdio_regs[reg_address] << 16);
}

static uint64_t greth_read(void *opaque, hwaddr offset, unsigned size)
{
    grethState *s = (grethState *)opaque;

    switch (offset) {
    case 0x00:
        return s->ctrl;
    case 0x08: /* MAC MSB */
        return s->conf.macaddr.a[4] | (s->conf.macaddr.a[5] << 8);
    case 0x0c: /* MAC LSB */
        return s->conf.macaddr.a[0] | (s->conf.macaddr.a[1] << 8)
            | (s->conf.macaddr.a[2] << 16)
            | ((uint32_t)s->conf.macaddr.a[3] << 24);
    case 0x10: /* MDIO */
        return s->mdio;
    case 0x14: /* Transmitter descriptor table */
        return s->txdesc;
    case 0x18: /* Receiver descriptor table */
        return s->rxdesc;
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
    case 0x08: /* MAC MSB */
        s->conf.macaddr.a[4] = value;
        s->conf.macaddr.a[5] = value >> 8;
        break;
    case 0x0c: /* MAC LSB */
        s->conf.macaddr.a[0] = value;
        s->conf.macaddr.a[1] = value >> 8;
        s->conf.macaddr.a[2] = value >> 16;
        s->conf.macaddr.a[3] = value >> 24;
        break;
    case 0x10: /* MDIO */
        greth_mdio(s, value);
        break;
    case 0x14: /* Transmitter descriptor table */
        s->txdesc = value & ~3;
        break;
    case 0x18: /* Receiver descriptor table */
        s->rxdesc = value & ~3;
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
    /* Simulate gigabit capable device */
    s->ctrl = (1 << 27);
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
