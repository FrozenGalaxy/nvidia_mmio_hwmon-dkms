// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/err.h>

#define DRV_NAME "nvidia_mmio_hwmon"

/* ------------------------------------------------------------ */
/* NVIDIA MMIO thermal profile table                            */
/* ------------------------------------------------------------ */

struct nvidia_mmio_profile {
    u16 device_id;
    const char *name;
    u32 hotspot_offset;
    u32 vram_offset;
};

static const struct nvidia_mmio_profile nvidia_profiles[] = {
    {
        .device_id      = 0x2684,
        .name           = "AD102 (GeForce RTX 4090)",
        .hotspot_offset = 0x0002046C,
        .vram_offset    = 0x0000E2A8,
    },
};

/* ------------------------------------------------------------ */
/* NVML snapshot injected from userspace                         */
/* ------------------------------------------------------------ */

struct nvml_snapshot {
    long temp_core_mC; /* millidegC */
    long power_mW;     /* milliwatt */
    long fan_input;    /* percent or RPM */
};

static struct nvml_snapshot nvml = {
    .temp_core_mC = -1,
    .power_mW     = -1,
    .fan_input    = -1,
};

static DEFINE_MUTEX(nvml_lock);

struct nmmio {
    struct pci_dev *pdev;
    const struct nvidia_mmio_profile *profile;
    void __iomem *bar0;
    struct device *hwmon_dev;
};

static struct nmmio *g;

/* ------------------------------------------------------------ */
/* nvml sysfs attribute (atomic update)                          */
/*
 * Userspace writes:
 *   <temp_mC> <power_mW> <fan>
 *
 * Example:
 *   42000 312000 47
 * ------------------------------------------------------------ */

static ssize_t nvml_store(struct device *dev,
                          struct device_attribute *attr,
                          const char *buf, size_t count)
{
    struct nvml_snapshot tmp;

    if (sscanf(buf, "%ld %ld %ld",
        &tmp.temp_core_mC,
        &tmp.power_mW,
        &tmp.fan_input) != 3)
        return -EINVAL;

    mutex_lock(&nvml_lock);
    nvml = tmp;
    mutex_unlock(&nvml_lock);

    return count;
}

static DEVICE_ATTR_WO(nvml);

/* ------------------------------------------------------------ */
/* Helpers                                                       */
/* ------------------------------------------------------------ */

static inline u32 mmio_read32(void __iomem *base, u32 off)
{
    return readl(base + off);
}

static const struct nvidia_mmio_profile *find_profile(u16 device_id)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(nvidia_profiles); i++) {
        if (nvidia_profiles[i].device_id == device_id)
            return &nvidia_profiles[i];
    }
    return NULL;
}

/* ------------------------------------------------------------ */
/* hwmon ops                                                     */
/* ------------------------------------------------------------ */

static umode_t nmmio_is_visible(const void *data,
                                enum hwmon_sensor_types type,
                                u32 attr, int channel)
{
    switch (type) {
        case hwmon_temp:
            if ((attr == hwmon_temp_input || attr == hwmon_temp_label) &&
                channel >= 0 && channel <= 2)
                return 0444;
            return 0;

        case hwmon_power:
            if (channel == 0 &&
                (attr == hwmon_power_input || attr == hwmon_power_label))
                return 0444;
            return 0;

        case hwmon_fan:
            if (channel == 0 &&
                (attr == hwmon_fan_input || attr == hwmon_fan_label))
                return 0444;
            return 0;

        default:
            return 0;
    }
}

static int nmmio_read(struct device *dev,
                      enum hwmon_sensor_types type,
                      u32 attr, int channel, long *val)
{
    struct nmmio *d = dev_get_drvdata(dev);
    u32 reg;

    if (!d || !d->bar0)
        return -ENODEV;

    switch (type) {
        case hwmon_temp:
            if (attr != hwmon_temp_input)
                return -EOPNOTSUPP;

        switch (channel) {
            case 0: /* GPU core (NVML) */
                mutex_lock(&nvml_lock);
                *val = nvml.temp_core_mC;
                mutex_unlock(&nvml_lock);
                return (*val < 0) ? -EOPNOTSUPP : 0;

            case 1: /* Junction / hotspot (MMIO) */
                reg = mmio_read32(d->bar0, d->profile->hotspot_offset);
                *val = ((reg >> 8) & 0xff) * 1000;
                return 0;

            case 2: /* VRAM (MMIO) */
                reg = mmio_read32(d->bar0, d->profile->vram_offset);
                *val = ((reg & 0x00000fff) / 0x20) * 1000;
                return 0;

            default:
                return -EINVAL;
        }

            case hwmon_power:
                if (attr != hwmon_power_input || channel != 0)
                    return -EOPNOTSUPP;

        mutex_lock(&nvml_lock);
        *val = nvml.power_mW;
        mutex_unlock(&nvml_lock);
        return (*val < 0) ? -EOPNOTSUPP : 0;

            case hwmon_fan:
                if (attr != hwmon_fan_input || channel != 0)
                    return -EOPNOTSUPP;

        mutex_lock(&nvml_lock);
        *val = nvml.fan_input;
        mutex_unlock(&nvml_lock);
        return (*val < 0) ? -EOPNOTSUPP : 0;

            default:
                return -EOPNOTSUPP;
    }
}

static int nmmio_read_string(struct device *dev,
                             enum hwmon_sensor_types type,
                             u32 attr, int channel,
                             const char **str)
{
    switch (type) {
        case hwmon_temp:
            if (attr != hwmon_temp_label)
                return -EOPNOTSUPP;
        switch (channel) {
            case 0: *str = "GPU Core";     return 0;
            case 1: *str = "GPU Junction"; return 0;
            case 2: *str = "GPU VRAM";     return 0;
            default: return -EINVAL;
        }

            case hwmon_power:
                if (attr != hwmon_power_label || channel != 0)
                    return -EOPNOTSUPP;
        *str = "Total Board Power";
        return 0;

            case hwmon_fan:
                if (attr != hwmon_fan_label || channel != 0)
                    return -EOPNOTSUPP;
        *str = "GPU Fan";
        return 0;

            default:
                return -EOPNOTSUPP;
    }
}

static const struct hwmon_ops nmmio_hwmon_ops = {
    .is_visible  = nmmio_is_visible,
    .read        = nmmio_read,
    .read_string = nmmio_read_string,
};

static const struct hwmon_channel_info *nmmio_info[] = {
    HWMON_CHANNEL_INFO(temp,
                       HWMON_T_INPUT | HWMON_T_LABEL,
                       HWMON_T_INPUT | HWMON_T_LABEL,
                       HWMON_T_INPUT | HWMON_T_LABEL),
                       HWMON_CHANNEL_INFO(power,
                                          HWMON_P_INPUT | HWMON_P_LABEL),
                                          HWMON_CHANNEL_INFO(fan,
                                                             HWMON_F_INPUT | HWMON_F_LABEL),
                                                             NULL
};

static const struct hwmon_chip_info nmmio_chip_info = {
    .ops  = &nmmio_hwmon_ops,
    .info = nmmio_info,
};

/* ------------------------------------------------------------ */
/* Module init / exit                                            */
/* ------------------------------------------------------------ */

static int __init nmmio_init(void)
{
    struct pci_dev *pdev = NULL;
    const struct nvidia_mmio_profile *profile = NULL;
    resource_size_t len;

    pr_info(DRV_NAME ": initializing\n");

    for_each_pci_dev(pdev) {
        if (pdev->vendor != 0x10de)
            continue;

        profile = find_profile(pdev->device);
        if (!profile)
            continue;

        g = kzalloc(sizeof(*g), GFP_KERNEL);
        if (!g)
            return -ENOMEM;

        pci_dev_get(pdev);
        g->pdev = pdev;
        g->profile = profile;
        break;
    }

    if (!g)
        return -ENODEV;

    len = pci_resource_len(g->pdev, 0);
    if (!len)
        goto err;

    g->bar0 = pci_iomap(g->pdev, 0, 0);
    if (!g->bar0)
        goto err;

    g->hwmon_dev = hwmon_device_register_with_info(
        &g->pdev->dev, "nvidia_mmio", g,
        &nmmio_chip_info, NULL);

    if (IS_ERR(g->hwmon_dev))
        goto err;

    device_create_file(g->hwmon_dev, &dev_attr_nvml);

    pr_info(DRV_NAME ": loaded successfully\n");
    return 0;

    err:
    if (g->bar0)
        pci_iounmap(g->pdev, g->bar0);
    if (g->pdev)
        pci_dev_put(g->pdev);
    kfree(g);
    g = NULL;
    return -EIO;
}

static void __exit nmmio_exit(void)
{
    if (!g)
        return;

    device_remove_file(g->hwmon_dev, &dev_attr_nvml);
    hwmon_device_unregister(g->hwmon_dev);

    pci_iounmap(g->pdev, g->bar0);
    pci_dev_put(g->pdev);
    kfree(g);

    pr_info(DRV_NAME ": unloaded\n");
}

module_init(nmmio_init);
module_exit(nmmio_exit);

MODULE_AUTHOR("you");
MODULE_DESCRIPTION("NVIDIA MMIO hwmon (junction/VRAM) + NVML bridge (temp/power/fan)");
MODULE_LICENSE("GPL");
