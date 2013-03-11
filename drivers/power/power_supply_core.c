/*
 *  Universal power supply monitor class
 *
 *  Copyright © 2007  Anton Vorontsov <cbou@mail.ru>
 *  Copyright © 2004  Szabolcs Gyurko
 *  Copyright © 2003  Ian Molton <spyro@f2s.com>
 *
 *  Modified: 2004, Oct     Szabolcs Gyurko
 *
 *  You may use this code as per GPL version 2
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/power_supply.h>
#include "power_supply.h"

/* exported for the APM Power driver, APM emulation */
struct class *power_supply_class;
EXPORT_SYMBOL_GPL(power_supply_class);

static struct device_type power_supply_dev_type;

static struct mutex ps_chrg_evt_lock;

static struct power_supply_charger_cap power_supply_chrg_cap = {
		.chrg_evt	= POWER_SUPPLY_CHARGER_EVENT_DISCONNECT,
		.chrg_type	= POWER_SUPPLY_TYPE_USB,
		.mA		= 0	/* 0 mA */
};

static int __power_supply_changed_work(struct device *dev, void *data)
{
	struct power_supply *psy = (struct power_supply *)data;
	struct power_supply *pst = dev_get_drvdata(dev);
	int i;

	for (i = 0; i < psy->num_supplicants; i++)
		if (!strcmp(psy->supplied_to[i], pst->name)) {
			if (pst->external_power_changed)
				pst->external_power_changed(pst);
		}
	return 0;
}

int charger_connect_mask = 0;
static void power_supply_changed_work(struct work_struct *work)
{
	unsigned long flags;
	char charger_flag[32] = "CHARGERFLAG=N";
	char *envp[] = {charger_flag, NULL};
	struct power_supply *psy = container_of(work, struct power_supply,
						changed_work);

	dev_dbg(psy->dev, "%s\n", __func__);

	spin_lock_irqsave(&psy->changed_lock, flags);
	if (psy->changed) {
		psy->changed = false;
		spin_unlock_irqrestore(&psy->changed_lock, flags);

		class_for_each_device(power_supply_class, NULL, psy,
				      __power_supply_changed_work);

		power_supply_update_leds(psy);

		spin_lock_irqsave(&psy->changed_lock, flags);
		if(charger_connect_mask & CHARGER_IN) {
			if(!(charger_connect_mask & CHARGER_STILL)) {
				strcpy(charger_flag, "CHARGERFLAG=Y");
				charger_connect_mask |= CHARGER_STILL;
			}
		} else {
			if(charger_connect_mask & CHARGER_STILL) {
				strcpy(charger_flag, "CHARGERFLAG=Y");
				charger_connect_mask = CHARGER_OUT;
			}
		}
		spin_unlock_irqrestore(&psy->changed_lock, flags);
		dev_dbg(psy->dev, "charger_connect_mask 0x%x; %s\n",
					charger_connect_mask, envp[0]);

		kobject_uevent_env(&psy->dev->kobj, KOBJ_CHANGE, envp);
		spin_lock_irqsave(&psy->changed_lock, flags);
	}
	if (!psy->changed)
		wake_unlock(&psy->work_wake_lock);
	spin_unlock_irqrestore(&psy->changed_lock, flags);
}

void power_supply_changed(struct power_supply *psy)
{
	unsigned long flags;

	dev_dbg(psy->dev, "%s\n", __func__);

	spin_lock_irqsave(&psy->changed_lock, flags);
	psy->changed = true;
	wake_lock(&psy->work_wake_lock);
	spin_unlock_irqrestore(&psy->changed_lock, flags);
	schedule_work(&psy->changed_work);
}
EXPORT_SYMBOL_GPL(power_supply_changed);

static int __power_supply_charger_event(struct device *dev, void *data)
{
	struct power_supply_charger_cap *cap =
				(struct power_supply_charger_cap *)data;
	struct power_supply *psy = dev_get_drvdata(dev);

	if (psy->charging_port_changed)
		psy->charging_port_changed(psy, cap);

	return 0;
}

void power_supply_charger_event(struct power_supply_charger_cap cap)
{
	class_for_each_device(power_supply_class, NULL, &cap,
				      __power_supply_charger_event);

	mutex_lock(&ps_chrg_evt_lock);
	memcpy(&power_supply_chrg_cap, &cap, sizeof(power_supply_chrg_cap));
	mutex_unlock(&ps_chrg_evt_lock);
}
EXPORT_SYMBOL_GPL(power_supply_charger_event);

void power_supply_query_charger_caps(struct power_supply_charger_cap *cap)
{
	mutex_lock(&ps_chrg_evt_lock);
	memcpy(cap, &power_supply_chrg_cap, sizeof(power_supply_chrg_cap));
	mutex_unlock(&ps_chrg_evt_lock);
}
EXPORT_SYMBOL_GPL(power_supply_query_charger_caps);

static int __power_supply_am_i_supplied(struct device *dev, void *data)
{
	union power_supply_propval ret = {0,};
	struct power_supply *psy = (struct power_supply *)data;
	struct power_supply *epsy = dev_get_drvdata(dev);
	int i;

	for (i = 0; i < epsy->num_supplicants; i++) {
		if (!strcmp(epsy->supplied_to[i], psy->name)) {
			if (epsy->get_property(epsy,
				  POWER_SUPPLY_PROP_ONLINE, &ret))
				continue;
			if (ret.intval)
				return ret.intval;
		}
	}
	return 0;
}

int power_supply_am_i_supplied(struct power_supply *psy)
{
	int error;

	error = class_for_each_device(power_supply_class, NULL, psy,
				      __power_supply_am_i_supplied);

	dev_dbg(psy->dev, "%s %d\n", __func__, error);

	return error;
}
EXPORT_SYMBOL_GPL(power_supply_am_i_supplied);

static int __power_supply_is_system_supplied(struct device *dev, void *data)
{
	union power_supply_propval ret = {0,};
	struct power_supply *psy = dev_get_drvdata(dev);

	if (psy->type != POWER_SUPPLY_TYPE_BATTERY) {
		if (psy->get_property(psy, POWER_SUPPLY_PROP_ONLINE, &ret))
			return 0;
		if (ret.intval)
			return ret.intval;
	}
	return 0;
}

int power_supply_is_system_supplied(void)
{
	int error;

	error = class_for_each_device(power_supply_class, NULL, NULL,
				      __power_supply_is_system_supplied);

	return error;
}
EXPORT_SYMBOL_GPL(power_supply_is_system_supplied);

int power_supply_set_battery_charged(struct power_supply *psy)
{
	if (psy->type == POWER_SUPPLY_TYPE_BATTERY && psy->set_charged) {
		psy->set_charged(psy);
		return 0;
	}

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(power_supply_set_battery_charged);

static int power_supply_match_device_by_name(struct device *dev, void *data)
{
	const char *name = data;
	struct power_supply *psy = dev_get_drvdata(dev);

	return strcmp(psy->name, name) == 0;
}

struct power_supply *power_supply_get_by_name(char *name)
{
	struct device *dev = class_find_device(power_supply_class, NULL, name,
					power_supply_match_device_by_name);

	return dev ? dev_get_drvdata(dev) : NULL;
}
EXPORT_SYMBOL_GPL(power_supply_get_by_name);

static void power_supply_dev_release(struct device *dev)
{
	pr_debug("device: '%s': %s\n", dev_name(dev), __func__);
	kfree(dev);
}

int power_supply_register(struct device *parent, struct power_supply *psy)
{
	struct device *dev;
	int rc;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	device_initialize(dev);

	dev->class = power_supply_class;
	dev->type = &power_supply_dev_type;
	dev->parent = parent;
	dev->release = power_supply_dev_release;
	dev_set_drvdata(dev, psy);
	psy->dev = dev;

	INIT_WORK(&psy->changed_work, power_supply_changed_work);

	rc = kobject_set_name(&dev->kobj, "%s", psy->name);
	if (rc)
		goto kobject_set_name_failed;

	rc = device_add(dev);
	if (rc)
		goto device_add_failed;

	spin_lock_init(&psy->changed_lock);
	wake_lock_init(&psy->work_wake_lock, WAKE_LOCK_SUSPEND, "power-supply");

	rc = power_supply_create_triggers(psy);
	if (rc)
		goto create_triggers_failed;

	power_supply_changed(psy);

	goto success;

create_triggers_failed:
	wake_lock_destroy(&psy->work_wake_lock);
	device_del(dev);
kobject_set_name_failed:
device_add_failed:
	put_device(dev);
success:
	return rc;
}
EXPORT_SYMBOL_GPL(power_supply_register);

void power_supply_unregister(struct power_supply *psy)
{
	cancel_work_sync(&psy->changed_work);
	power_supply_remove_triggers(psy);
	wake_lock_destroy(&psy->work_wake_lock);
	device_unregister(psy->dev);
}
EXPORT_SYMBOL_GPL(power_supply_unregister);

static int __init power_supply_class_init(void)
{
	power_supply_class = class_create(THIS_MODULE, "power_supply");

	if (IS_ERR(power_supply_class))
		return PTR_ERR(power_supply_class);

	power_supply_class->dev_uevent = power_supply_uevent;
	power_supply_init_attrs(&power_supply_dev_type);
	mutex_init(&ps_chrg_evt_lock);

	return 0;
}

static void __exit power_supply_class_exit(void)
{
	class_destroy(power_supply_class);
}

subsys_initcall(power_supply_class_init);
module_exit(power_supply_class_exit);

MODULE_DESCRIPTION("Universal power supply monitor class");
MODULE_AUTHOR("Ian Molton <spyro@f2s.com>, "
	      "Szabolcs Gyurko, "
	      "Anton Vorontsov <cbou@mail.ru>");
MODULE_LICENSE("GPL");
