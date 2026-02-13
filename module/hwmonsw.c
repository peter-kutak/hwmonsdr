#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/device.h>
#include <linux/hwmon.h>
#include <linux/sysfs.h>

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("PeterK");
MODULE_DESCRIPTION("hwmon dynamic sensors using hwmon_device_register_with_groups");

struct sensor_entry {
    struct list_head list;
    char name[32]; /* "fan1" */
    char label[64]; /* human readable label (e.g., "FAN1 SYS" ) */
    long value;
    struct device_attribute *dev_attr_input;
    struct device_attribute *dev_attr_label;
};

struct hwmon_ctx {
    struct device *hwmon_dev;
    struct mutex lock;
    struct list_head sensors;
};

static struct hwmon_ctx *gctx;

/* helper: find by name (expects lock) */
static struct sensor_entry *find_sensor(struct hwmon_ctx *ctx, const char *name)
{
    struct sensor_entry *s;
    list_for_each_entry(s, &ctx->sensors, list) {
        if (strcmp(s->name, name) == 0)
            return s;
    }
    return NULL;
}

/* show for fanN_input */
static ssize_t fan_input_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct hwmon_ctx *ctx = dev_get_drvdata(dev);
    struct sensor_entry *s;
    char base[32];
    ssize_t ret = 0;

    /* attr->attr.name is like "fan1_input" */
    //FIXME strlcpy
    strncpy(base, attr->attr.name, sizeof(base));
    if (strlen(base) > 6) /* remove "_input" */
        base[strlen(base)-6] = '\0';

    mutex_lock(&ctx->lock);
    s = find_sensor(ctx, base);
    if (s)
        ret = scnprintf(buf, PAGE_SIZE, "%ld\n", s->value);
    else
        ret = scnprintf(buf, PAGE_SIZE, "0\n");
    mutex_unlock(&ctx->lock);

    return ret;
}

static ssize_t fan_label_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct hwmon_ctx *ctx = dev_get_drvdata(dev);
    struct sensor_entry *s;
    char base[32];
    ssize_t ret = 0;

    strncpy(base, attr->attr.name, sizeof(base)); /* e.g., "fan1\_label" */
    if (strlen(base) > 6)
        base[strlen(base)-6] = '\0'; /* remove "\_label" */

    mutex_lock(&ctx->lock);
    s = find_sensor(ctx, base);
    if (s)
        ret = scnprintf(buf, PAGE_SIZE, "%s\n", s->label);
    else
        ret = scnprintf(buf, PAGE_SIZE, "\n");
    mutex_unlock(&ctx->lock);
    return ret;
}

/* create sensor (expects lock) */
static int create_sensor_locked(struct hwmon_ctx *ctx, const char *name, const char *label, long val)
{
    struct sensor_entry *s;
    struct device_attribute *dinput, *dlabel;
    char *attr_input_name, *attr_label_name;

    s = kzalloc(sizeof(*s), GFP_KERNEL);
    if (!s)
        return -ENOMEM;
    strncpy(s->name, name, sizeof(s->name));
    strncpy(s->label, label, sizeof(s->label));
    s->value = val;

    dinput = kzalloc(sizeof(*dinput), GFP_KERNEL);
    if (!dinput) {
        kfree(s);
        return -ENOMEM;
    }
    dlabel = kzalloc(sizeof(*dlabel), GFP_KERNEL);
    if (!dlabel) {
        kfree(dinput);
        kfree(s);
        return -ENOMEM;
    }

    attr_input_name = kasprintf(GFP_KERNEL, "%s_input", name);
    if (!attr_input_name) {
        kfree(dlabel);
        kfree(dinput);
        kfree(s);
        return -ENOMEM;
    }
    attr_label_name = kasprintf(GFP_KERNEL, "%s_label", name);
    if (!attr_label_name) {
        kfree(dlabel);
        kfree(dinput);
        kfree(s);
        return -ENOMEM;
    }
 
    sysfs_attr_init(&dinput->attr);
    dinput->attr.name = attr_input_name;
    dinput->attr.mode = 0444;
    dinput->show = fan_input_show;
    dinput->store = NULL;

    sysfs_attr_init(&dlabel->attr);
    dlabel->attr.name = attr_label_name;
    dlabel->attr.mode = 0444;
    dlabel->show = fan_label_show;

    if (device_create_file(ctx->hwmon_dev, dinput) < 0) {
        kfree(attr_label_name);
        kfree(attr_input_name);
        kfree(dlabel);
        kfree(dinput);
        kfree(s);
        return -EIO;
    }
    if (device_create_file(ctx->hwmon_dev, dlabel) < 0) {
        kfree(attr_label_name);
        kfree(attr_input_name);
        kfree(dlabel);
        kfree(dinput);
        kfree(s);
        return -EIO;
    }


    s->dev_attr_input = dinput;
    s->dev_attr_label = dlabel;
    list_add_tail(&s->list, &ctx->sensors);
    return 0;
}

/* parse "name=value" pairs separated by commas */
static void parse_and_update(struct hwmon_ctx *ctx, const char *buf, size_t count)
{
    char *dup, *p, *tok;
    char name[32];
    long val;

    dup = kstrndup(buf, count, GFP_KERNEL);
    if (!dup)
        return;

    mutex_lock(&ctx->lock);
    for (p = dup; (tok = strsep(&p, ",")) != NULL; ) {
        char *eq;
        if (*tok == '\0')
            continue;
        eq = strchr(tok, '=');
        if (!eq)
            continue;
        *eq = '\0';
        strncpy(name, tok, sizeof(name));
        if (kstrtol(eq+1, 10, &val) < 0)
            continue;

        {
            struct sensor_entry *s = find_sensor(ctx, name);
            if (s)
                s->value = val;
            else
                create_sensor_locked(ctx, name, name, val);
        }
    }
    mutex_unlock(&ctx->lock);
    kfree(dup);
}

/* update store */
static ssize_t update_store(struct device *dev, struct device_attribute *attr,
                            const char *buf, size_t count)
{
    parse_and_update(gctx, buf, count);
    return count;
}

static struct device_attribute update_attr = __ATTR_WO(update);

static int __init hwmon_multi_init(void)
{
    int err;

    gctx = kzalloc(sizeof(*gctx), GFP_KERNEL);
    if (!gctx)
        return -ENOMEM;
    mutex_init(&gctx->lock);
    INIT_LIST_HEAD(&gctx->sensors);

    gctx->hwmon_dev = hwmon_device_register_with_groups(NULL, "hwmonsw", gctx, NULL);
    if (IS_ERR(gctx->hwmon_dev)) {
        err = PTR_ERR(gctx->hwmon_dev);
        pr_err("hwmonsw: register failed %d\n", err);
        kfree(gctx);
        return err;
    }

    if (device_create_file(gctx->hwmon_dev, &update_attr) < 0) {
        pr_err("hwmonsw: create update failed\n");
        hwmon_device_unregister(gctx->hwmon_dev);
        kfree(gctx);
        return -EIO;
    }

    dev_set_drvdata(gctx->hwmon_dev, gctx);
    pr_info("hwmonsw loaded\n");
    return 0;
}

static void __exit hwmon_multi_exit(void)
{
    struct sensor_entry *s, *tmp;

    if (!gctx)
        return;

    mutex_lock(&gctx->lock);
    list_for_each_entry_safe(s, tmp, &gctx->sensors, list) {
        if (s->dev_attr_input) {
            device_remove_file(gctx->hwmon_dev, s->dev_attr_input);
            kfree(s->dev_attr_input->attr.name);
            kfree(s->dev_attr_input);
        }
        if (s->dev_attr_label) {
            device_remove_file(gctx->hwmon_dev, s->dev_attr_label);
            kfree(s->dev_attr_label->attr.name);
            kfree(s->dev_attr_label);
        }
        list_del(&s->list);
        kfree(s);
    }
    mutex_unlock(&gctx->lock);

    device_remove_file(gctx->hwmon_dev, &update_attr);
    hwmon_device_unregister(gctx->hwmon_dev);
    kfree(gctx);
    pr_info("hwmonsw unloaded\n");
}

module_init(hwmon_multi_init);
module_exit(hwmon_multi_exit);


