/*
 * pmic.c - Intel MSIC Driver
 *
 * Copyright (C) 2012 Intel Corporation
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Author: Bin Yang <bin.yang@intel.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/ipc_device.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <asm/intel_scu_ipc.h>

#define IPC_WWBUF_SIZE    20
#define IPC_RWBUF_SIZE    20

#define IPCMSG_PCNTRL           0xFF

#define IPC_CMD_PCNTRL_W      0
#define IPC_CMD_PCNTRL_R      1
#define IPC_CMD_PCNTRL_M      2

static int pwr_reg_rdwr(u16 *addr, u8 *data, u32 count, u32 cmd, u32 sub)
{
	int i, j, err, inlen = 0, outlen;

	u8 wbuf[IPC_WWBUF_SIZE] = {};
	u8 rbuf[IPC_RWBUF_SIZE] = {};

	memset(wbuf, 0, sizeof(wbuf));

	if (intel_mid_identify_cpu() == INTEL_MID_CPU_CHIP_LINCROFT) {
		for (i = 0, j = 0; i < count; i++) {
			wbuf[inlen++] = addr[i] & 0xff;
			wbuf[inlen++] = (addr[i] >> 8) & 0xff;

			if (sub != IPC_CMD_PCNTRL_R) {
				wbuf[inlen++] = data[j++];
				outlen = 0;
			}

			if (sub == IPC_CMD_PCNTRL_M)
				wbuf[inlen++] = data[j++];
		}
	} else {
		for (i = 0; i < count; i++) {
			wbuf[inlen++] = addr[i] & 0xff;
			wbuf[inlen++] = (addr[i] >> 8) & 0xff;
		}

		if (sub == IPC_CMD_PCNTRL_R) {
			outlen = count > 0 ? ((count - 1) / 4) + 1 : 0;
		} else if (sub == IPC_CMD_PCNTRL_W) {
			for (i = 0; i < count; i++)
				wbuf[inlen++] = data[i] & 0xff;
			outlen = 0;
		} else if (sub == IPC_CMD_PCNTRL_M) {
			wbuf[inlen++] = data[0] & 0xff;
			wbuf[inlen++] = data[1] & 0xff;
			outlen = 0;
		} else
			pr_err("IPC command not supported\n");
	}

	err = intel_scu_ipc_command(cmd, sub, (u32 *)wbuf, inlen,
			(u32 *)rbuf, outlen);

	if (sub == IPC_CMD_PCNTRL_R) {
		if (intel_mid_identify_cpu() == INTEL_MID_CPU_CHIP_LINCROFT) {
			for (i = 0, j = 2; i < count; i++, j += 3)
				data[i] = rbuf[j];
		} else {
			for (i = 0; i < count; i++)
				data[i] = rbuf[i];
		}
	}

	return err;
}

int intel_scu_ipc_ioread8(u16 addr, u8 *data)
{
	return pwr_reg_rdwr(&addr, data, 1, IPCMSG_PCNTRL, IPC_CMD_PCNTRL_R);
}
EXPORT_SYMBOL(intel_scu_ipc_ioread8);

int intel_scu_ipc_iowrite8(u16 addr, u8 data)
{
	return pwr_reg_rdwr(&addr, &data, 1, IPCMSG_PCNTRL, IPC_CMD_PCNTRL_W);
}
EXPORT_SYMBOL(intel_scu_ipc_iowrite8);

int intel_scu_ipc_iowrite32(u16 addr, u32 data)
{
	u16 x[4] = {addr, addr + 1, addr + 2, addr + 3};
	return pwr_reg_rdwr(x, (u8 *)&data, 4, IPCMSG_PCNTRL, IPC_CMD_PCNTRL_W);
}
EXPORT_SYMBOL(intel_scu_ipc_iowrite32);

int intel_scu_ipc_readv(u16 *addr, u8 *data, int len)
{
	return pwr_reg_rdwr(addr, data, len, IPCMSG_PCNTRL, IPC_CMD_PCNTRL_R);
}
EXPORT_SYMBOL(intel_scu_ipc_readv);

int intel_scu_ipc_writev(u16 *addr, u8 *data, int len)
{
	return pwr_reg_rdwr(addr, data, len, IPCMSG_PCNTRL, IPC_CMD_PCNTRL_W);
}
EXPORT_SYMBOL(intel_scu_ipc_writev);

int intel_scu_ipc_update_register(u16 addr, u8 bits, u8 mask)
{
	u8 data[2] = { bits, mask };
	return pwr_reg_rdwr(&addr, data, 1, IPCMSG_PCNTRL, IPC_CMD_PCNTRL_M);
}
EXPORT_SYMBOL(intel_scu_ipc_update_register);

/* pmic sysfs for debug */

#define MAX_PMIC_REG_NR 4
#define PMIC_OPS_LEN 10

enum {
	PMIC_DBG_ADDR,
	PMIC_DBG_BITS,
	PMIC_DBG_DATA,
	PMIC_DBG_MASK,
};

static char *pmic_msg_format[] = {
	"addr[%d]: %#x\n",
	"bits[%d]: %#x\n",
	"data[%d]: %#x\n",
	"mask[%d]: %#x\n",
};

static u16 pmic_reg_addr[MAX_PMIC_REG_NR];
static u8 pmic_reg_bits[MAX_PMIC_REG_NR];
static u8 pmic_reg_data[MAX_PMIC_REG_NR];
static u8 pmic_reg_mask[MAX_PMIC_REG_NR];
static int valid_addr_nr;
static int valid_bits_nr;
static int valid_data_nr;
static int valid_mask_nr;
static char pmic_ops[PMIC_OPS_LEN];

static int pmic_dbg_error;

static ssize_t pmic_generic_show(char *buf, int valid, u8 *array, int type)
{
	int i, buf_size;
	ssize_t ret = 0;

	switch (type) {
	case PMIC_DBG_ADDR:
		for (i = 0; i < valid; i++) {
			buf_size = PAGE_SIZE - ret;
			ret += snprintf(buf + ret, buf_size,
					pmic_msg_format[type],
					i, pmic_reg_addr[i]);
		}
		break;
	case PMIC_DBG_BITS:
	case PMIC_DBG_DATA:
	case PMIC_DBG_MASK:
		for (i = 0; i < valid; i++) {
			buf_size = PAGE_SIZE - ret;
			ret += snprintf(buf + ret, buf_size,
					pmic_msg_format[type],
					i, array[i]);
		}
		break;
	default:
		break;
	}

	return ret;
}

static void pmic_generic_store(const char *buf, int *valid, u8 *array, int type)
{
	u32 tmp[MAX_PMIC_REG_NR];
	int i, ret;

	ret = sscanf(buf, "%x %x %x %x", &tmp[0], &tmp[1], &tmp[2], &tmp[3]);
	if (ret == 0 || ret > MAX_PMIC_REG_NR) {
		*valid = 0;
		pmic_dbg_error = -EINVAL;
		return;
	}

	*valid = ret;

	switch (type) {
	case PMIC_DBG_ADDR:
		memset(pmic_reg_addr, 0, sizeof(pmic_reg_addr));
		for (i = 0; i < ret; i++)
			pmic_reg_addr[i] = (u16)tmp[i];
		break;
	case PMIC_DBG_BITS:
	case PMIC_DBG_DATA:
	case PMIC_DBG_MASK:
		memset(array, 0, sizeof(*array) * MAX_PMIC_REG_NR);
		for (i = 0; i < ret; i++)
			array[i] = (u8)tmp[i];
		break;
	default:
		break;
	}
}

static ssize_t pmic_addr_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return pmic_generic_show(buf, valid_addr_nr, NULL, PMIC_DBG_ADDR);
}

static ssize_t pmic_addr_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	pmic_generic_store(buf, &valid_addr_nr, NULL, PMIC_DBG_ADDR);
	return size;
}

static ssize_t pmic_bits_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return pmic_generic_show(buf, valid_bits_nr, pmic_reg_bits,
			PMIC_DBG_BITS);
}

static ssize_t pmic_bits_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	pmic_generic_store(buf, &valid_bits_nr, pmic_reg_bits, PMIC_DBG_BITS);
	return size;
}

static ssize_t pmic_data_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return pmic_generic_show(buf, valid_data_nr, pmic_reg_data,
			PMIC_DBG_DATA);
}

static ssize_t pmic_data_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	pmic_generic_store(buf, &valid_data_nr, pmic_reg_data, PMIC_DBG_DATA);
	return size;
}

static ssize_t pmic_mask_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return pmic_generic_show(buf, valid_mask_nr, pmic_reg_mask,
			PMIC_DBG_MASK);
}

static ssize_t pmic_mask_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	pmic_generic_store(buf, &valid_mask_nr, pmic_reg_mask, PMIC_DBG_MASK);
	return size;
}

static ssize_t pmic_ops_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	int i, ret;

	memset(pmic_ops, 0, sizeof(pmic_ops));

	ret = sscanf(buf, "%9s", pmic_ops);
	if (ret == 0) {
		pmic_dbg_error = -EINVAL;
		goto end;
	}

	if (valid_addr_nr <= 0) {
		pmic_dbg_error = -EINVAL;
		goto end;
	}

	if (!strncmp("read", pmic_ops, PMIC_OPS_LEN)) {
		valid_data_nr = valid_addr_nr;
		for (i = 0; i < valid_addr_nr; i++) {
			ret = intel_scu_ipc_ioread8(pmic_reg_addr[i],
					&pmic_reg_data[i]);
			if (ret) {
				pmic_dbg_error = ret;
				goto end;
			}
		}
	} else if (!strncmp("write", pmic_ops, PMIC_OPS_LEN)) {
		if (valid_addr_nr == valid_data_nr) {
			for (i = 0; i < valid_addr_nr; i++) {
				ret = intel_scu_ipc_iowrite8(pmic_reg_addr[i],
						pmic_reg_data[i]);
				if (ret) {
					pmic_dbg_error = ret;
					goto end;
				}
			}
		} else {
			pmic_dbg_error = -EINVAL;
			goto end;
		}
	} else if (!strncmp("update", pmic_ops, PMIC_OPS_LEN)) {
		if (valid_addr_nr == valid_mask_nr &&
				valid_mask_nr == valid_bits_nr) {
			for (i = 0; i < valid_addr_nr; i++) {
				ret = intel_scu_ipc_update_register(
						pmic_reg_addr[i],
						pmic_reg_bits[i],
						pmic_reg_mask[i]);
				if (ret) {
					pmic_dbg_error = ret;
					goto end;
				}
			}
		} else {
			pmic_dbg_error = -EINVAL;
			goto end;
		}
	} else {
		pmic_dbg_error = -EINVAL;
		goto end;
	}

		pmic_dbg_error = 0;

end:
	return size;
}

static ssize_t pmic_show_error(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", pmic_dbg_error);
}

static DEVICE_ATTR(addr, S_IRUGO|S_IWUSR, pmic_addr_show, pmic_addr_store);
static DEVICE_ATTR(bits, S_IRUGO|S_IWUSR, pmic_bits_show, pmic_bits_store);
static DEVICE_ATTR(data, S_IRUGO|S_IWUSR, pmic_data_show, pmic_data_store);
static DEVICE_ATTR(mask, S_IRUGO|S_IWUSR, pmic_mask_show, pmic_mask_store);
static DEVICE_ATTR(ops, S_IWUSR, NULL, pmic_ops_store);
static DEVICE_ATTR(error, S_IRUGO, pmic_show_error, NULL);

static struct attribute *pmic_attrs[] = {
	&dev_attr_addr.attr,
	&dev_attr_bits.attr,
	&dev_attr_data.attr,
	&dev_attr_mask.attr,
	&dev_attr_ops.attr,
	&dev_attr_error.attr,
	NULL,
};

static struct attribute_group pmic_attr_group = {
	.name = "pmic_debug",
	.attrs = pmic_attrs,
};

static int pmic_sysfs_create(struct ipc_device *ipcdev)
{
	return sysfs_create_group(&ipcdev->dev.kobj, &pmic_attr_group);
}

static void pmic_sysfs_remove(struct ipc_device *ipcdev)
{
	sysfs_remove_group(&ipcdev->dev.kobj, &pmic_attr_group);
}

static int __devinit pmic_probe(struct ipc_device *ipcdev)
{
	return pmic_sysfs_create(ipcdev);
}

static int __devexit pmic_remove(struct ipc_device *ipcdev)
{
	pmic_sysfs_remove(ipcdev);
	return 0;
}

static struct ipc_driver pmic_driver = {
	.driver = {
		.name = "intel_scu_pmic",
		.owner = THIS_MODULE,
	},
	.probe = pmic_probe,
	.remove = __devexit_p(pmic_remove),
};

static int __init pmic_module_init(void)
{
	return ipc_driver_register(&pmic_driver);
}

static void __exit pmic_module_exit(void)
{
	ipc_driver_unregister(&pmic_driver);
}

fs_initcall(pmic_module_init);
module_exit(pmic_module_exit);

MODULE_AUTHOR("Bin Yang<bin.yang@intel.com>");
MODULE_DESCRIPTION("Intel PMIC Driver");
MODULE_LICENSE("GPL v2");
