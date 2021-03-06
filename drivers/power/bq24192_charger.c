/*
 * bq24192_charger.c - Charger driver for TI BQ24192,BQ24191 and BQ24190
 *
 * Copyright (C) 2011 Intel Corporation
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
 * Author: Ramakrishna Pallala <ramakrishna.pallala@intel.com>
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/power_supply.h>
#include <linux/power/bq24192_charger.h>
#include <linux/sfi.h>
#include <linux/pm_runtime.h>
#include <linux/io.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <asm/intel_mid_gpadc.h>

#define DRV_NAME "bq24192_charger"
#define DEV_NAME "bq24192"

/*
 * D0, D1, D2 can be used to set current limits
 * and D3, D4, D5, D6 can be used to voltage limits
 */
#define BQ24192_INPUT_SRC_CNTL_REG		0x0
#define INPUT_SRC_CNTL_EN_HIZ			(1 << 7)
/* set input voltage lim to 5V */
#define INPUT_SRC_VOLT_LMT			(6 << 3)
/* D0, D1, D2 represent the input current limit */
#define INPUT_SRC_CUR_LMT0		0x0	/* 100mA */
#define INPUT_SRC_CUR_LMT1		0x1	/* 150mA */
#define INPUT_SRC_CUR_LMT2		0x2	/* 500mA */
#define INPUT_SRC_CUR_LMT3		0x3	/* 900mA */
#define INPUT_SRC_CUR_LMT4		0x4	/* 1200mA */
#define INPUT_SRC_CUR_LMT5		0x5	/* 1500mA */
#define INPUT_SRC_CUR_LMT6		0x6	/* 2000mA */
#define INPUT_SRC_CUR_LMT7		0x7	/* 3000mA */

/*
 * D1, D2, D3 can be used to set min sys voltage limit
 * and D4, D5 can be used to control the charger
 */
#define BQ24192_POWER_ON_CFG_REG		0x1
#define POWER_ON_CFG_RESET			(1 << 7)
#define POWER_ON_CFG_I2C_WDTTMR_RESET		(1 << 6)
#define CHR_CFG_BIT_POS				4
#define CHR_CFG_BIT_LEN				2
#define POWER_ON_CFG_CHRG_CFG_DIS		(0 << 4)
#define POWER_ON_CFG_CHRG_CFG_EN		(1 << 4)
#define POWER_ON_CFG_CHRG_CFG_OTG		(3 << 4)
#define POWER_ON_CFG_BOOST_LIM			(1 << 0)

/*
 * Charge Current control register
 * with range from 500 - 4532mA
 */
#define BQ24192_CHRG_CUR_CNTL_REG		0x2
#define BQ24192_CHRG_CUR_OFFSET		500	/* 500 mA */
#define BQ24192_CHRG_CUR_LSB_TO_CUR	64	/* 64 mA */
#define BQ24192_GET_CHRG_CUR(reg) ((reg>>2)*BQ24192_CHRG_CUR_LSB_TO_CUR\
			+ BQ24192_CHRG_CUR_OFFSET) /* in mA */

/* Pre charge and termination current limit reg */
#define BQ24192_PRECHRG_TERM_CUR_CNTL_REG	0x3

/* Charge voltage control reg */
#define BQ24192_CHRG_VOLT_CNTL_REG	0x4
#define BQ24192_CHRG_VOLT_OFFSET	3504	/* 3504 mV */
#define BQ24192_CHRG_VOLT_LSB_TO_VOLT	16	/* 16 mV */
/* Low voltage setting 0 - 2.8V and 1 - 3.0V */
#define CHRG_VOLT_CNTL_BATTLOWV		(1 << 1)
/* Battery Recharge threshold 0 - 100mV and 1 - 300mV */
#define CHRG_VOLT_CNTL_VRECHRG		(1 << 0)
#define BQ24192_GET_CHRG_VOLT(reg) ((reg>>2)*BQ24192_CHRG_VOLT_LSB_TO_VOLT\
			+ BQ24192_CHRG_VOLT_OFFSET) /* in mV */

/* Charge termination and Timer control reg */
#define BQ24192_CHRG_TIMER_EXP_CNTL_REG		0x5
#define CHRG_TIMER_EXP_CNTL_EN_TERM		(1 << 7)
#define CHRG_TIMER_EXP_CNTL_TERM_STAT		(1 << 6)
/* WDT Timer uses 2 bits */
#define WDT_TIMER_BIT_POS			4
#define WDT_TIMER_BIT_LEN			2
#define CHRG_TIMER_EXP_CNTL_WDTDISABLE		(0 << 4)
#define CHRG_TIMER_EXP_CNTL_WDT40SEC		(1 << 4)
#define CHRG_TIMER_EXP_CNTL_WDT80SEC		(2 << 4)
#define CHRG_TIMER_EXP_CNTL_WDT160SEC		(3 << 4)
/* Safety Timer Enable bit */
#define CHRG_TIMER_EXP_CNTL_EN_TIMER		(1 << 3)
/* Charge Timer uses 2bits(20 hrs) */
#define SFT_TIMER_BIT_POS			1
#define SFT_TIMER_BIT_LEN			2
#define CHRG_TIMER_EXP_CNTL_SFT_TIMER		(3 << 1)

#define BQ24192_CHRG_THRM_REGL_REG		0x6

#define BQ24192_MISC_OP_CNTL_REG		0x7
#define MISC_OP_CNTL_DPDM_EN			(1 << 7)
#define MISC_OP_CNTL_TMR2X_EN			(1 << 6)
#define MISC_OP_CNTL_BATFET_DIS			(1 << 5)
#define MISC_OP_CNTL_BATGOOD_EN			(1 << 4)
/* To mask INT's write 0 to the bit */
#define MISC_OP_CNTL_MINT_CHRG			(1 << 1)
#define MISC_OP_CNTL_MINT_BATT			(1 << 0)

#define BQ24192_SYSTEM_STAT_REG			0x8
/* D6, D7 show VBUS status */
#define SYSTEM_STAT_VBUS_UNKNOWN		(0 << 6)
#define SYSTEM_STAT_VBUS_HOST			(1 << 6)
#define SYSTEM_STAT_VBUS_ADP			(2 << 6)
#define SYSTEM_STAT_VBUS_OTG			(3 << 6)
/* D4, D5 show charger status */
#define SYSTEM_STAT_NOT_CHRG			(0 << 4)
#define SYSTEM_STAT_PRE_CHRG			(1 << 4)
#define SYSTEM_STAT_FAST_CHRG			(2 << 4)
#define SYSTEM_STAT_CHRG_DONE			(3 << 4)
#define SYSTEM_STAT_DPM				(1 << 3)
#define SYSTEM_STAT_PWR_GOOD			(1 << 2)
#define SYSTEM_STAT_THERM_REG			(1 << 1)
#define SYSTEM_STAT_VSYS_LOW			(1 << 0)
#define SYSTEM_STAT_CHRG_MASK			(3 << 4)

#define BQ24192_FAULT_STAT_REG			0x9
#define FAULT_STAT_WDT_TMR_EXP			(1 << 7)
#define FAULT_STAT_OTG_FLT			(1 << 6)
/* D4, D5 show charger fault status */
#define FAULT_STAT_CHRG_NORMAL			(0 << 4)
#define FAULT_STAT_CHRG_IN_FLT			(1 << 4)
#define FAULT_STAT_CHRG_THRM_FLT		(2 << 4)
#define FAULT_STAT_CHRG_TMR_FLT			(3 << 4)
#define FAULT_STAT_BATT_FLT			(1 << 3)

#define BQ24192_VENDER_REV_REG			0xA
/* D3, D4, D5 indicates the chip model number */
#define BQ24190_IC_VERSION			0x0
#define BQ24191_IC_VERSION			0x1
#define BQ24192_IC_VERSION			0x2
#define BQ24192I_IC_VERSION			0x3

#define BQ24192_MAX_MEM		12
#define NR_RETRY_CNT		3

#define CHARGER_PS_NAME				"bq24192_charger"

#define BQ24192_DEF_VBATT_MAX		4192	/* 4192mV */
#define BQ24192_DEF_SDP_ILIM_CUR	500	/* 500mA */
#define BQ24192_DEF_DCP_ILIM_CUR	1500	/* 1500mA */
#define BQ24192_DEF_CHRG_CUR		1500	/* 1500mA */

#define BQ24192_CHRG_CUR_LOW		100	/* 100mA */
#define BQ24192_CHRG_CUR_MEDIUM		500	/* 500mA */
#define BQ24192_CHRG_CUR_HIGH		900	/* 900mA */
#define BQ24192_CHRG_CUR_NOLIMIT	1500	/* 1500mA */

#define STATUS_UPDATE_INTERVAL		(HZ * 60) /* 60sec */

#define BQ24192_CHRG_OTG_GPIO		36
#define MAINTENANCE_CHRG_JIFFIES	(HZ * 30) /* 30sec */

#define CLT_BPTHERM_CURVE_MAX_SAMPLES	23
#define CLT_BPTHERM_CURVE_MAX_VALUES	4
/* default Charger parameters */
#define CLT_BATT_CHRVOLTAGE_SET_DEF	4200 /*in mV */
#define CLT_BATT_DEFAULT_MAX_CAPACITY	1500 /*in mAH */

/* ADC Channel Numbers */
#define CLT_BATT_NUM_GPADC_SENSORS	1
#define CLT_GPADC_BPTHERM_CHNUM	0x9
#define CLT_GPADC_BPTHERM_SAMPLE_COUNT	1

/*CLT battery temperature  attributes*/
#define CLT_BTP_ADC_MIN	107
#define CLT_BTP_ADC_MAX	977

#define SFI_BATTPROP_TBL_ID	"OEM0"
#define CLT_ADC_TIME_TO_LIVE	(HZ/8)	/* 125 ms */

#define CLT_VBATT_FULL_DET_MARGIN	50	/* 50mV */
#define CLT_FULL_CURRENT_AVG_LOW	0
#define CLT_FULL_CURRENT_AVG_HIGH	50

#define CLT_BATT_VMIN_THRESHOLD_DEF	3600	/* 3600mV */
#define CLT_BATT_TEMP_MAX_DEF	60	/* 60 degrees */
#define CLT_BATT_TEMP_MIN_DEF	0
#define CLT_BATT_CRIT_CUTOFF_VOLT_DEF	3700	/* 3700 mV */

#define BQ24192_INVALID_CURR -1
#define BQ24192_INVALID_VOLT -1

static struct power_supply *fg_psy;
static struct ctp_batt_sfi_prop *ctp_sfi_table;

struct bq24192_chrg_regs {
	u8 in_src;
	u8 pwr_cfg;
	u8 chr_cur;
	u8 chr_volt;
};

struct bq24192_chip {
	struct i2c_client *client;
	struct bq24192_platform_data *pdata;
	struct power_supply usb;
	struct power_supply_charger_cap cap;
	struct delayed_work chrg_evt_wrkr;
	struct delayed_work stat_mon_wrkr;
	struct delayed_work maint_chrg_wrkr;
	struct mutex event_lock;

	int present;
	int online;
	enum power_supply_type chrg_type;
	int chrg_cur_cntl; /* contains the current limit index */

	/* battery info */
	int batt_status;
	bool votg;
	enum bq24192_bat_chrg_mode batt_mode;

	/* Handle for gpadc requests */
	void *gpadc_handle;
	struct ctp_batt_safety_thresholds batt_thrshlds;
	/* cached parameters for event worker handler needed
	 * to support extreme charging*/
	int curr_volt;
	int curr_chrg;
	int cached_chrg_cur_cntl;
	struct power_supply_charger_cap cached_cap;
};

#ifdef CONFIG_DEBUG_FS
static struct dentry *bq24192_dbgfs_root;
static char bq24192_dbg_regs[BQ24192_MAX_MEM][4];
#endif

static struct i2c_client *bq24192_client;

static char *bq24192_power_supplied_to[] = {
			"max170xx_battery",
			"max17042_battery",
};

/*
 * temperature v/s ADC value table to interpolate and calculate temp
 */
static int const ctp_bptherm_curve_data[CLT_BPTHERM_CURVE_MAX_SAMPLES]
	[CLT_BPTHERM_CURVE_MAX_VALUES] = {
	/* {temp_max, temp_min, adc_max, adc_min} */
	{-15, -20, 977, 961},
	{-10, -15, 961, 941},
	{-5, -10, 941, 917},
	{0, -5, 917, 887},
	{5, 0, 887, 853},
	{10, 5, 853, 813},
	{15, 10, 813, 769},
	{20, 15, 769, 720},
	{25, 20, 720, 669},
	{30, 25, 669, 615},
	{35, 30, 615, 561},
	{40, 35, 561, 508},
	{45, 40, 508, 456},
	{50, 45, 456, 407},
	{55, 50, 407, 357},
	{60, 55, 357, 315},
	{65, 60, 315, 277},
	{70, 65, 277, 243},
	{75, 70, 243, 212},
	{80, 75, 212, 186},
	{85, 80, 186, 162},
	{90, 85, 162, 140},
	{100, 90, 140, 107},
};


static enum power_supply_property bq24192_usb_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_TYPE
};

/************************************************************************
 * End of structure definition section
 ***********************************************************************/
/*
 * sfi table parsing specific interfaces
 */

/**
 * ctp_sfi_table_invalid_batt - default battery SFI table values  to be
 * used in case of invalid battery
 *
 * @sfi_table : sfi table pointer
 * Context: can sleep
 * Note: These interfaces will move to a common file and will be
 * independent of the platform
 */
static void ctp_sfi_table_invalid_batt(struct ctp_batt_sfi_prop *sfi_table)
{

	/*
	 * In case of invalid battery we manually set
	 * the SFI parameters and limit the battery from
	 * charging, so platform will be in discharging mode
	 */
	memcpy(ctp_sfi_table->batt_id, "UNKNOWN", sizeof("UNKNOWN"));
	ctp_sfi_table->voltage_max = CLT_BATT_CHRVOLTAGE_SET_DEF;
	ctp_sfi_table->capacity = CLT_BATT_DEFAULT_MAX_CAPACITY;
	ctp_sfi_table->battery_type = POWER_SUPPLY_TECHNOLOGY_LION;
	ctp_sfi_table->temp_mon_ranges = 0;

}

/**
 * ctp_sfi_table_populate - Simple Firmware Interface table Populate
 * @sfi_table: Simple Firmware Interface table structure
 *
 * SFI table has entries for the temperature limits
 * which is populated in a local structure
 */
static int __init ctp_sfi_table_populate(struct sfi_table_header *table)
{
	struct sfi_table_simple *sb;
	struct ctp_batt_sfi_prop *pentry;
	struct bq24192_chip *chip = i2c_get_clientdata(bq24192_client);
	int totentrs = 0, totlen = 0;

	sb = (struct sfi_table_simple *)table;
	if (!sb) {
		dev_warn(&chip->client->dev, "SFI: Unable to map BATT signature\n");
		return -ENODEV;
	}

	totentrs = SFI_GET_NUM_ENTRIES(sb, struct ctp_batt_sfi_prop);
	if (totentrs) {
		pentry = (struct ctp_batt_sfi_prop *)sb->pentry;
		totlen = totentrs * sizeof(*pentry);
		memcpy(ctp_sfi_table, pentry, totlen);
		if (ctp_sfi_table->temp_mon_ranges != CLT_SFI_TEMP_NR_RNG)
			dev_warn(&chip->client->dev, "SFI: temperature monitoring range"
				"doesn't match with its Array elements size\n");
	} else {
		dev_warn(&chip->client->dev, "Invalid battery detected\n");
		ctp_sfi_table_invalid_batt(ctp_sfi_table);
	}
	return 0;
}

/* Check for valid Temp ADC range */
static bool ctp_is_valid_temp_adc(int adc_val)
{
	bool ret = false;

	if (adc_val >= CLT_BTP_ADC_MIN && adc_val <= CLT_BTP_ADC_MAX)
		ret = true;

	return ret;
}

/* Temperature conversion Macros */
static int ctp_conv_adc_temp(int adc_val,
	int adc_max, int adc_diff, int temp_diff)
{
	int ret;

	ret = (adc_max - adc_val) * temp_diff;
	return ret / adc_diff;
}

/* Check if the adc value is in the curve sample range */
static bool ctp_is_valid_temp_adc_range(int val, int min, int max)
{
	bool ret = false;
	if (val > min && val <= max)
		ret = true;
	return ret;
}

/**
 * ctp_adc_to_temp - convert ADC code to temperature
 * @adc_val : ADC sensor reading
 * @tmp : finally read temperature
 *
 * Returns 0 on success or -ERANGE in error case
 */
static int ctp_adc_to_temp(uint16_t adc_val, int *tmp)
{
	int temp = 0;
	int i;

	if (!ctp_is_valid_temp_adc(adc_val)) {
		dev_warn(&bq24192_client->dev,
			"Temperature out of Range: %u\n", adc_val);
		return -ERANGE;
	}

	for (i = 0; i < CLT_BPTHERM_CURVE_MAX_SAMPLES; i++) {
		/* linear approximation for battery pack temperature */
		if (ctp_is_valid_temp_adc_range(
			adc_val, ctp_bptherm_curve_data[i][3],
			ctp_bptherm_curve_data[i][2])) {

			temp = ctp_conv_adc_temp(adc_val,
				  ctp_bptherm_curve_data[i][2],
				  ctp_bptherm_curve_data[i][2] -
				  ctp_bptherm_curve_data[i][3],
				  ctp_bptherm_curve_data[i][0] -
				  ctp_bptherm_curve_data[i][1]);

			temp += ctp_bptherm_curve_data[i][1];
			break;
		}
	}

	if (i >= CLT_BPTHERM_CURVE_MAX_SAMPLES) {
		dev_warn(&bq24192_client->dev, "Invalid temp adc range\n");
		return -EINVAL;
	}
	*tmp = temp;

	return 0;
}

/**
 * ctp_read_adc_temp - read ADC sensor to get the temperature
 * @tmp: op parameter where temperature get's read
 *
 * Returns 0 if success else -1 or -ERANGE
 */
static int ctp_read_adc_temp(int *tmp)
{
	int gpadc_sensor_val = 0;
	int ret;
	struct bq24192_chip *chip = i2c_get_clientdata(bq24192_client);

	if (!chip->gpadc_handle) {
		ret = -ENODEV;
		goto read_adc_exit;
	}

	ret = intel_mid_gpadc_sample(chip->gpadc_handle,
				CLT_GPADC_BPTHERM_SAMPLE_COUNT,
				&gpadc_sensor_val);
	if (ret) {
		dev_err(&bq24192_client->dev,
			"adc driver api returned error(%d)\n", ret);
		goto read_adc_exit;
	}

	ret = ctp_adc_to_temp(gpadc_sensor_val, tmp);
read_adc_exit:
	return ret;
}

/**
 * ctp_sfi_temp_range_lookup - lookup SFI table to find the temperature range index
 * @adc_temp : temperature in Degree Celcius
 *
 * Returns temperature range index
 */
static int ctp_sfi_temp_range_lookup(int adc_temp)
{
	int i, idx = -1;
	int max_range;
	struct bq24192_chip *chip = i2c_get_clientdata(bq24192_client);
	struct ctp_temp_mon_table *temp_mon_tabl = NULL;
	short int temp_low_lim;

	if (chip->pdata->sfi_tabl_present) {
		dev_info(&chip->client->dev,
			 "Read the temperature range from sfi table\n");
		if (ctp_sfi_table->temp_mon_ranges < CLT_SFI_TEMP_NR_RNG)
			max_range = ctp_sfi_table->temp_mon_ranges;
		else
			max_range = CLT_SFI_TEMP_NR_RNG;
		temp_mon_tabl = &ctp_sfi_table->temp_mon_range[0];
		temp_low_lim = ctp_sfi_table->temp_low_lim;
	} else {
		dev_info(&chip->client->dev,
			 "Read the temperature range from platform data\n");
		temp_mon_tabl = &chip->pdata->temp_mon_range[0];
		temp_low_lim = chip->pdata->temp_low_lim;
		max_range = chip->pdata->temp_mon_ranges;
	}

	for (i = max_range-1; i >= 0; i--) {
		if (adc_temp <= temp_mon_tabl[i].temp_up_lim &&
			adc_temp > temp_low_lim) {
			idx = i;
			break;
		}
	}

	dev_info(&chip->client->dev, "%s:temp idx = %d\n", __func__, idx);
	return idx;
}
/* returns the max and min temp in which battery is suppose to operate */
static void ctp_get_batt_temp_thresholds(short int *temp_high,
		short int *temp_low)
{
	int i, max_range;
	struct bq24192_chip *chip = i2c_get_clientdata(bq24192_client);
	struct ctp_temp_mon_table *temp_mon_tabl = NULL;
	short int temp_low_lim;

	*temp_high = *temp_low = 0;
	if (!chip->pdata->sfi_tabl_present) {
		if (ctp_sfi_table->temp_mon_ranges < CLT_SFI_TEMP_NR_RNG)
			max_range = ctp_sfi_table->temp_mon_ranges;
		else
			max_range = CLT_SFI_TEMP_NR_RNG;
		temp_mon_tabl = &ctp_sfi_table->temp_mon_range[0];
		temp_mon_tabl = &ctp_sfi_table->temp_mon_range[0];
	} else {
		temp_mon_tabl = &chip->pdata->temp_mon_range[0];
		max_range = chip->pdata->temp_mon_ranges;
		temp_low_lim = chip->pdata->temp_low_lim;
	}

	for (i = 0; i < max_range; i++) {
		if (*temp_high < temp_mon_tabl[i].temp_up_lim)
			*temp_high = temp_mon_tabl[i].temp_up_lim;
	}

	*temp_low = temp_low_lim;
}

/*-------------------------------------------------------------------------*/


/*
 * Genenric register read/write interfaces to access registers in charger ic
 */

static int bq24192_write_reg(struct i2c_client *client, u8 reg, u8 value)
{
	int ret, i;

	for (i = 0; i < NR_RETRY_CNT; i++) {
		ret = i2c_smbus_write_byte_data(client, reg, value);
		if (ret == -EAGAIN || ret == -ETIMEDOUT)
			continue;
		else
			break;
	}

	if (ret < 0)
		dev_err(&client->dev, "I2C SMbus Write error:%d\n", ret);

	return ret;
}

static int bq24192_read_reg(struct i2c_client *client, u8 reg)
{
	int ret, i;

	for (i = 0; i < NR_RETRY_CNT; i++) {
		ret = i2c_smbus_read_byte_data(client, reg);
		if (ret == -EAGAIN || ret == -ETIMEDOUT)
			continue;
		else
			break;
	}

	if (ret < 0)
		dev_err(&client->dev, "I2C SMbus Read error:%d\n", ret);

	return ret;
}

int bq24192_query_battery_status(void)
{
	struct bq24192_chip *chip = i2c_get_clientdata(bq24192_client);

	return chip->batt_status;
}
EXPORT_SYMBOL(bq24192_query_battery_status);

/*
 * If the bit_set is TRUE then val 1s will be SET in the reg else val 1s will
 * be CLEARED
 */
static int bq24192_reg_read_modify(struct i2c_client *client, u8 reg,
							u8 val, bool bit_set)
{
	int ret;

	ret = bq24192_read_reg(client, reg);

	if (bit_set)
		ret |= val;
	else
		ret &= (~val);

	ret = bq24192_write_reg(client, reg, ret);

	return ret;
}

static int bq24192_reg_multi_bitset(struct i2c_client *client, u8 reg,
						u8 val, u8 pos, u8 len)
{
	int ret;
	u8 data;

	ret = bq24192_read_reg(client, reg);
	if (ret < 0) {
		dev_warn(&client->dev, "I2C SMbus Read error:%d\n", ret);
		return ret;
	}

	data = (1 << len) - 1;
	ret = (ret & ~(data << pos)) | val;
	ret = bq24192_write_reg(client, reg, ret);

	return ret;
}

/*****************************************************************************/
/*
 * Extreme Charging Section: This section defines sysfs interfaces used
 * for setting up the required thermal zone.
 */

/* Sysfs Entry for enable or disable Charging from user space */
static ssize_t set_charge_current_limit(struct device *device,
			struct device_attribute *attr, const char *buf,
			size_t count);
static ssize_t get_charge_current_limit(struct device *device,
			struct device_attribute *attr, char *buf);
static DEVICE_ATTR(charge_current_limit, S_IRUGO | S_IWUSR,
					get_charge_current_limit,
					set_charge_current_limit);


/* map charge current control setting
 * to input current limit value in mA.
 */
static int chrg_lim_idx_to_chrg_cur(int lim)
{
	int cur_lim;

	switch (lim) {
	case USER_SET_CHRG_LMT1:
		cur_lim = BQ24192_CHRG_CUR_LOW;
		break;
	case USER_SET_CHRG_LMT2:
		cur_lim = BQ24192_CHRG_CUR_MEDIUM;
		break;
	case USER_SET_CHRG_LMT3:
		cur_lim = BQ24192_CHRG_CUR_HIGH;
		break;
	default:
		cur_lim = -EINVAL;
	}
	return cur_lim;
}

/**
 * set_charge_current_limit - sysfs set api for charge_enable attribute
 * Parameter as define by sysfs interface
 * Context: can sleep
 *
 */
static ssize_t set_charge_current_limit(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t count)
{
	struct bq24192_chip *chip = i2c_get_clientdata(bq24192_client);
	unsigned long value;
	int chr_mode;
	dev_info(&chip->client->dev, "+%s\n", __func__);

	if (kstrtoul(buf, 10, &value))
		return -EINVAL;

	/* Allow only 0 to 4 for writing */
	if (value < USER_SET_CHRG_DISABLE || value > USER_SET_CHRG_NOLMT) {
		dev_info(&chip->client->dev,
			"%s: Thermal index %lu out of range\n", __func__,
			value);
		return -EINVAL;
	}
	chr_mode = chip->batt_mode;

	switch (value) {
	case USER_SET_CHRG_DISABLE:
		dev_dbg(&chip->client->dev,
			"%s: User App Charge Disable\n", __func__);
		mutex_lock(&chip->event_lock);
		chip->chrg_cur_cntl = value;
		mutex_unlock(&chip->event_lock);

		/* check if battery is in charging mode */
		if (chr_mode != BATT_CHRG_NONE) {
			/* Disable Charger before setting up usr_chrg_enable */
			dev_dbg(&chip->client->dev,
				"%s: Send POWER_SUPPLY_CHARGER_EVENT_SUSPEND\n"\
				, __func__);
			mutex_lock(&chip->event_lock);
			chip->cap.chrg_evt = POWER_SUPPLY_CHARGER_EVENT_SUSPEND;
			mutex_unlock(&chip->event_lock);
			schedule_delayed_work(&chip->chrg_evt_wrkr, 0);
		}
		break;
	case USER_SET_CHRG_LMT1:
	case USER_SET_CHRG_LMT2:
	case USER_SET_CHRG_LMT3:
	case USER_SET_CHRG_NOLMT:
		dev_dbg(&chip->client->dev, \
			"%s: User App Charge Enable\n", __func__);
		mutex_lock(&chip->event_lock);
		chip->chrg_cur_cntl = value;
		chip->cap.chrg_evt = POWER_SUPPLY_CHARGER_EVENT_RESUME;
		mutex_unlock(&chip->event_lock);
		schedule_delayed_work(&chip->chrg_evt_wrkr, 0);
		break;
	default:
		dev_err(&chip->client->dev, "Invalid request\n");
	}

	dev_info(&chip->client->dev,
		"%s:chr_mode : %d, chip->chrg_cur_cntl: %d\n", \
		__func__, chip->batt_mode, chip->chrg_cur_cntl);
	return count;
}

/**
 * get_chrg_enable - sysfs get api for charge_enable attribute
 * Parameter as define by sysfs interface
 * Context: can sleep
 *
 */
static ssize_t get_charge_current_limit(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct bq24192_chip *chip = i2c_get_clientdata(bq24192_client);
	unsigned long value;

	dev_info(&chip->client->dev, "+%s\n", __func__);

	/* Update the variable for the user app */
	mutex_lock(&chip->event_lock);
	value = chip->chrg_cur_cntl;
	mutex_unlock(&chip->event_lock);

	return sprintf(buf, "%lu\n", value);
}
/****************************************************************************/
/*
 * charger and battery specific interfaces exposed to external modules
 */

/* returns the battery pack temperature read from adc */
int ctp_get_battery_pack_temp(int *temp)
{
	struct bq24192_chip *chip = NULL;

	if (!bq24192_client)
		return -ENODEV;

	chip = i2c_get_clientdata(bq24192_client);

	/* check if charger is ready */
	if (!power_supply_get_by_name(CHARGER_PS_NAME))
		return -EAGAIN;

	return ctp_read_adc_temp(temp);
}
EXPORT_SYMBOL(ctp_get_battery_pack_temp);

/* returns battery status */
int ctp_query_battery_status(void)
{
	struct bq24192_chip *chip = NULL;

	if (!bq24192_client)
		return -ENODEV;

	chip = i2c_get_clientdata(bq24192_client);

	return chip->batt_status;
}
EXPORT_SYMBOL(ctp_query_battery_status);

/***********************************************************************/

/* convert the input current limit value
 * into equivalent register setting.
 * Note: ilim must be in mA.
 */
static u8 chrg_ilim_to_reg(int ilim)
{
	u8 reg;

	/* set voltage to 5V */
	reg = INPUT_SRC_VOLT_LMT;

	/* Set the input source current limit
	 * between 100 to 1500mA */
	if (ilim <= 100)
		reg |= INPUT_SRC_CUR_LMT0;
	else if (ilim <= 150)
		reg |= INPUT_SRC_CUR_LMT1;
	else if (ilim <= 500)
		reg |= INPUT_SRC_CUR_LMT2;
	else if (ilim <= 900)
		reg |= INPUT_SRC_CUR_LMT3;
	else if (ilim <= 1200)
		reg |= INPUT_SRC_CUR_LMT4;
	else
		reg |= INPUT_SRC_CUR_LMT5;

	return reg;
}

/* convert the charge current value
 * into equivalent register setting
 */
static u8 chrg_cur_to_reg(int cur)
{
	u8 reg;

	if (cur <= BQ24192_CHRG_CUR_OFFSET)
		reg = 0x0;
	else
		reg = ((cur - BQ24192_CHRG_CUR_OFFSET) /
				BQ24192_CHRG_CUR_LSB_TO_CUR);

	/* D0, D1 bits of Charge Current
	 * register are not used */
	reg = reg << 2;
	return reg;
}

/* convert the charge voltage value
 * into equivalent register setting
 */
static u8 chrg_volt_to_reg(int volt)
{
	u8 reg;

	if (volt <= BQ24192_CHRG_VOLT_OFFSET)
		reg = 0x0;
	else
		reg = (volt - BQ24192_CHRG_VOLT_OFFSET) /
				BQ24192_CHRG_VOLT_LSB_TO_VOLT;

	reg = (reg << 2) | CHRG_VOLT_CNTL_BATTLOWV;
	return reg;
}

static int program_wdt_timer(struct bq24192_chip *chip, u8 val)
{
	int ret;

	/* program WDT timer value */
	ret = bq24192_reg_multi_bitset(chip->client,
					BQ24192_CHRG_TIMER_EXP_CNTL_REG,
					val,
					WDT_TIMER_BIT_POS, WDT_TIMER_BIT_LEN);
	if (ret < 0)
		dev_warn(&chip->client->dev, "I2C write failed:%s\n", __func__);

	return ret;
}

static int reset_wdt_timer(struct bq24192_chip *chip)
{
	int ret;

	/* reset WDT timer */
	ret = bq24192_reg_read_modify(chip->client, BQ24192_POWER_ON_CFG_REG,
						BQ24192_POWER_ON_CFG_REG, true);
	if (ret < 0)
		dev_warn(&chip->client->dev, "I2C write failed:%s\n", __func__);

	return ret;
}

static int enable_charging(struct bq24192_chip *chip,
				struct bq24192_chrg_regs *reg)
{
	int ret;

	/* set input voltage and current reg */
	ret = bq24192_write_reg(chip->client, BQ24192_INPUT_SRC_CNTL_REG,
								reg->in_src);
	if (ret < 0) {
		dev_warn(&chip->client->dev, "I2C write failed:%s\n", __func__);
		goto i2c_write_failed;
	}

	/* set charge current reg */
	ret = bq24192_write_reg(chip->client, BQ24192_CHRG_CUR_CNTL_REG,
								reg->chr_cur);
	if (ret < 0) {
		dev_warn(&chip->client->dev, "I2C write failed:%s\n", __func__);
		goto i2c_write_failed;
	}

	/* set charge voltage reg */
	ret = bq24192_write_reg(chip->client, BQ24192_CHRG_VOLT_CNTL_REG,
								reg->chr_volt);
	if (ret < 0) {
		dev_warn(&chip->client->dev, "I2C write failed:%s\n", __func__);
		goto i2c_write_failed;
	}

	/* disable WDT timer */
	ret = program_wdt_timer(chip, CHRG_TIMER_EXP_CNTL_WDTDISABLE);
	if (ret < 0) {
		dev_warn(&chip->client->dev, "I2C write failed:%s\n", __func__);
		goto i2c_write_failed;
	}

	/* enable charger */
	ret = bq24192_reg_multi_bitset(chip->client, BQ24192_POWER_ON_CFG_REG,
						POWER_ON_CFG_CHRG_CFG_EN,
					CHR_CFG_BIT_POS, CHR_CFG_BIT_LEN);
	if (ret < 0)
		dev_warn(&chip->client->dev, "I2C write failed:%s\n", __func__);

i2c_write_failed:
	return ret;

}

static int stop_charging(struct bq24192_chip *chip)
{
	int ret;

	/* Disable the charger */
	ret = bq24192_reg_multi_bitset(chip->client, BQ24192_POWER_ON_CFG_REG,
						POWER_ON_CFG_CHRG_CFG_DIS,
					CHR_CFG_BIT_POS, CHR_CFG_BIT_LEN);
	if (ret < 0)
		dev_warn(&chip->client->dev, "I2C write failed:%s\n", __func__);

	return ret;
}

static int update_chrcurr_settings(struct bq24192_chip *chip, int chrg_lim)
{
	int ret;
	u8 in_src;

	if (chrg_lim == POWER_SUPPLY_CHARGE_CURRENT_LIMIT_ZERO) {
		ret = stop_charging(chip);
		if (ret < 0) {
			dev_err(&chip->client->dev,
				"charge disabling failed\n");
			goto uptd_chrg_set_exit;
		}
	}

	ret = chrg_lim_idx_to_chrg_cur(chrg_lim);
	if (ret < 0) {
		dev_err(&chip->client->dev,
			"invalid chrg limit index %d\n", chrg_lim);
		goto uptd_chrg_set_exit;
	}

	in_src = chrg_cur_to_reg(ret);
	/* set charge current reg with the limited index*/
	ret = bq24192_write_reg(chip->client,
			BQ24192_CHRG_CUR_CNTL_REG, in_src);
	if (ret < 0)
		dev_warn(&chip->client->dev,
				"I2C write failed:%s\n", __func__);

uptd_chrg_set_exit:
	return ret;
}

static void set_up_charging(struct bq24192_chip *chip,
		struct bq24192_chrg_regs *reg, int chr_curr, int chr_volt)
{
	int ret;
	reg->in_src = chrg_ilim_to_reg(chip->cap.mA);
	reg->chr_cur = chrg_cur_to_reg(chr_curr);
	reg->chr_volt = chrg_volt_to_reg(chr_volt);

	/* Disable the Charge termination */
	ret = bq24192_reg_read_modify(chip->client,
		BQ24192_CHRG_TIMER_EXP_CNTL_REG,
			CHRG_TIMER_EXP_CNTL_EN_TERM, false);
	if (ret < 0)
		dev_warn(&chip->client->dev, "I2C write failed:%s\n", __func__);
}

/* check_batt_psy -check for whether power supply type is battery
 * @dev : Power Supply dev structure
 * @data : Power Supply Driver Data
 * Context: can sleep
 *
 * Return true if power supply type is battery
 *
 */
static int check_batt_psy(struct device *dev, void *data)
{
	struct power_supply *psy = dev_get_drvdata(dev);

	/* check for whether power supply type is battery */
	if (psy->type == POWER_SUPPLY_TYPE_BATTERY) {
		fg_psy = psy;
		return 1;
	}
	return 0;
}
/**
 * get_fg_chip_psy - identify the Fuel Gauge Power Supply device
 * Context: can sleep
 *
 * Return Fuel Gauge power supply structure
 */
static struct power_supply *get_fg_chip_psy(void)
{
	if (fg_psy)
		return fg_psy;

	/* loop through power supply class */
	class_for_each_device(power_supply_class, NULL, NULL,
			check_batt_psy);
	return fg_psy;
}
/**
 * fg_chip_get_property - read a power supply property from Fuel Gauge driver
 * @psp : Power Supply property
 *
 * Return power supply property value
 *
 */
static int fg_chip_get_property(enum power_supply_property psp)
{
	union power_supply_propval val;
	int ret = -ENODEV;

	if (!fg_psy)
		fg_psy = get_fg_chip_psy();
	if (fg_psy) {
		ret = fg_psy->get_property(fg_psy, psp, &val);
		if (!ret)
			return val.intval;
	}
	return ret;
}

/* check if charger automatically terminated charging
 * even when charging is enabled */
static bool bq24192_is_chrg_terminated(struct bq24192_chip *chip)
{
	bool is_chrg_term = false;
	int ret;

	dev_info(&chip->client->dev, "+%s\n", __func__);
	ret = bq24192_read_reg(chip->client, BQ24192_SYSTEM_STAT_REG);
	if (ret < 0) {
		dev_err(&chip->client->dev, "i2c read err:%d\n", ret);
		goto is_chrg_term_exit;
	}

	if (((ret&SYSTEM_STAT_CHRG_MASK) == SYSTEM_STAT_CHRG_DONE) ||
	    ((ret&SYSTEM_STAT_CHRG_MASK) == SYSTEM_STAT_NOT_CHRG))
		is_chrg_term = true;
is_chrg_term_exit:
	return is_chrg_term;
}

static void bq24192_monitor_worker(struct work_struct *work)
{
	struct bq24192_chip *chip = container_of(work,
				struct bq24192_chip, stat_mon_wrkr.work);
	power_supply_changed(&chip->usb);
	schedule_delayed_work(&chip->stat_mon_wrkr, STATUS_UPDATE_INTERVAL);
}

/*
 * bq24192_do_charging - Programs the charger as per the charge current passed
 * curr -charging current value passed as per the platform current state
 */
static int bq24192_do_charging(int curr, int volt)
{
	struct bq24192_chip *chip = i2c_get_clientdata(bq24192_client);
	struct bq24192_chrg_regs reg;
	int ret = 0, chr_curr;

	dev_info(&chip->client->dev, "+ %s\n", __func__);
	/*
	 * Check if user has enabled charging through sysfs
	 * If yes then program the charge current  as per the user
	 * configuration
	 */
	mutex_lock(&chip->event_lock);
	if (chip->chrg_cur_cntl == USER_SET_CHRG_LMT1)
		chr_curr = INPUT_CHRG_CURR_100;
	else if (chip->chrg_cur_cntl == USER_SET_CHRG_LMT2)
		chr_curr = INPUT_CHRG_CURR_500;
	else if (chip->chrg_cur_cntl == USER_SET_CHRG_LMT3)
		chr_curr = INPUT_CHRG_CURR_950;
	else if (chip->chrg_cur_cntl == USER_SET_CHRG_DISABLE) {
		dev_info(&chip->client->dev,
			"Charging is disabled via sysfs interface %s\n",
			 __func__);
		goto bq24192_do_charging_exit;
	} else /* USER_SET_CHRG_NOLMT */
		chr_curr = curr;

	/*
	 * Make sure we program the lesser of the current values
	 * to satisfy the thermal requirement for the platform
	 */
	if (chr_curr > curr)
		chr_curr = curr;
	dev_info(&chip->client->dev,
		"voltage = %d, current = %d, usr_chrg_enable = %d\n",
		volt, curr, chip->chrg_cur_cntl);

	if (chip->batt_mode != BATT_CHRG_FULL) {
		set_up_charging(chip, &reg, chr_curr, volt);
		ret = enable_charging(chip, &reg);
		if (ret < 0) {
			dev_err(&chip->client->dev, "enable charging failed\n");
		} else {
			dev_info(&chip->client->dev, "Charging enabled\n");
			/* cache the current charge voltage and current*/
			chip->curr_volt = volt;
			chip->curr_chrg = chr_curr;
		}
	} else {
		dev_info(&chip->client->dev, "Battery is full. Don't charge\n");
	}
bq24192_do_charging_exit:
	mutex_unlock(&chip->event_lock);
	return ret;
}

/**
 * bq24192_check_charge_full -  check battery is full or not
 * @vref: battery voltage
 *
 * Return true if full
 *
 */
static  bool bq24192_check_charge_full(struct bq24192_chip *chip, int vref)
{
	static int volt_prev;
	bool is_full = false;
	int volt_now;
	int cur_avg;

	/* Read voltage and current from FG driver */
	volt_now = fg_chip_get_property(POWER_SUPPLY_PROP_VOLTAGE_NOW);
	if (volt_now == -ENODEV || volt_now == -EINVAL) {
		dev_warn(&chip->client->dev, "Can't read voltage from FG\n");
		return false;
	}
	/* convert to milli volts */
	volt_now /= 1000;

	/* Using Current-avg instead of Current-now to take care of
	 * instantaneous spike or dip */
	cur_avg = fg_chip_get_property(POWER_SUPPLY_PROP_CURRENT_AVG);
	if (cur_avg == -ENODEV || cur_avg == -EINVAL) {
		dev_warn(&chip->client->dev, "Can't read current-avg from FG\n");
		return false;
	}
	/* convert to milli amps */
	cur_avg /= 1000;
	/* voltage must be consistently above the vref threshold
	 * and current flow should be below a limit to confirm that
	 * battery is fully charged
	 */
	if ((volt_now >= (vref - CLT_VBATT_FULL_DET_MARGIN)) &&
	    (volt_prev >= (vref - CLT_VBATT_FULL_DET_MARGIN))) {
		if (cur_avg >= CLT_FULL_CURRENT_AVG_LOW  &&
				cur_avg <= CLT_FULL_CURRENT_AVG_HIGH)
			is_full = true;
		else
			is_full = false;
	} else {
		is_full = false;
	}

	volt_prev = volt_now;

	return is_full;
}

/*
 * bq24192_maintenance_worker
 * Maintenace worker thread monitors current voltage w.r.t temperature
 * and makes sure that we are within the current range. It also monitors user
 * based overriding control and gives higher priority to the same
 */
static void bq24192_maintenance_worker(struct work_struct *work)
{
	int ret, batt_temp, battery_status, idx, vbatt = 0;
	struct bq24192_chip *chip = container_of(work,
				struct bq24192_chip, maint_chrg_wrkr.work);
	short int cv = 0, usr_cc = -1;
	struct ctp_temp_mon_table *temp_mon = NULL;
	bool is_chrg_term = false, is_chrg_full = false;
	static int prev_temp_idx = -1;
	static int chrg_cur_cntl = USER_SET_CHRG_NOLMT;
	bool sysfs_stat = false;

	dev_dbg(&chip->client->dev, "+ %s\n", __func__);
	/* Check if we have the charger present */
	if (chip->present && chip->online) {
		dev_info(&chip->client->dev,
			"Charger is present\n");
	} else {
		dev_info(&chip->client->dev,
				"Charger is not present. Schedule worker\n");
		goto sched_maint_work;
	}

	/* read the temperature via adc */
	ret = ctp_read_adc_temp(&batt_temp);
	if (ret < 0) {
		dev_err(&chip->client->dev, "failed to acquire batt temp\n");
		goto sched_maint_work;
	}
	/* find the temperature range */
	idx = ctp_sfi_temp_range_lookup(batt_temp);
	if (idx == -1) {
		dev_warn(&chip->client->dev,
			"battery temperature is outside the designated zones\n");

		if (batt_temp < chip->batt_thrshlds.temp_low) {
			dev_info(&chip->client->dev,
				"batt temp:POWER_SUPPLY_HEALTH_COLD\n");
		} else {
			dev_info(&chip->client->dev,
				"batt temp:POWER_SUPPLY_HEALTH_OVERHEAT\n");
		}
		/* PMIC disables charging as it's hit the
		 * critical temperature range */
		goto sched_maint_work;
	}

	dev_info(&chip->client->dev, "temperature zone idx = %d\n", idx);
	/* read the battery voltage */
	vbatt = fg_chip_get_property(POWER_SUPPLY_PROP_VOLTAGE_NOW);
	if (vbatt == -ENODEV || vbatt == -EINVAL) {
		dev_err(&chip->client->dev, "Can't read voltage from FG\n");
		goto sched_maint_work;
	}

	/* convert voltage into millivolts */
	vbatt /= 1000;
	dev_info(&chip->client->dev, "vbatt = %d\n", vbatt);
	/* read the charge current based upon user setting */
	if (chip->chrg_cur_cntl != chrg_cur_cntl) {
		usr_cc = chrg_lim_idx_to_chrg_cur(chip->chrg_cur_cntl);
		chrg_cur_cntl = chip->chrg_cur_cntl;
		sysfs_stat = true;
		dev_info(&chip->client->dev,
			"change in user setting %d usr_cc = %d\n",
			chip->chrg_cur_cntl, usr_cc);
	}
	/*
	 * A temporary work around to do maintenance charging until we
	 * we get the entries in SFI table
	 */
	if (!chip->pdata->sfi_tabl_present) {
		dev_info(&chip->client->dev, "Using Platform data table\n");
		temp_mon = &chip->pdata->temp_mon_range[idx];
	} else {
		dev_info(&chip->client->dev, "Using SFI table data\n");
		temp_mon = &ctp_sfi_table->temp_mon_range[idx];
	}

	/* Read the charger status bit for charge complete */
	is_chrg_term = bq24192_is_chrg_terminated(chip);

	if (chip->batt_mode == BATT_CHRG_MAINT)
		cv = temp_mon->maint_chrg_vol_ul;
	else
		cv = temp_mon->full_chrg_vol;


	if (chip->batt_mode == BATT_CHRG_FULL)
		is_chrg_full = true;
	else
		/* check if the charge is full */
		is_chrg_full = bq24192_check_charge_full(chip, cv);

	dev_info(&chip->client->dev,
		"charge_full=%d charging mode = %d is_chrg_term = %d\n",
		is_chrg_full, chip->batt_mode, is_chrg_term);

	switch (chip->batt_mode) {
	case BATT_CHRG_NONE:
		goto sched_maint_work;
	case BATT_CHRG_NORMAL:
		if ((is_chrg_full == true) || (is_chrg_term == true)) {
			dev_info(&chip->client->dev, "Charge is Full or terminated\n");
			ret = stop_charging(chip);
			if (ret < 0) {
				dev_info(&chip->client->dev,
					"Stop charging failed:%s\n", __func__);
				goto sched_maint_work;
			}
			mutex_lock(&chip->event_lock);
			chip->batt_mode = BATT_CHRG_FULL;
			mutex_unlock(&chip->event_lock);
		} else if ((prev_temp_idx != idx) || (sysfs_stat == true)) {
			/* If there is change in temperature zone
			 * or user mode charge current settings */
			ret = bq24192_do_charging(
				temp_mon->full_chrg_cur,
				temp_mon->full_chrg_vol);
			if (ret < 0) {
				dev_warn(&chip->client->dev,
					"do_charing failed:\n");
				goto sched_maint_work;
			}
		}
		break;
	case BATT_CHRG_FULL:
		if (vbatt <= temp_mon->maint_chrg_vol_ll) {
			dev_info(&chip->client->dev,
				"vbatt is lower than maint_chrg_vol_ll\n");
			mutex_lock(&chip->event_lock);
			chip->batt_mode = BATT_CHRG_MAINT;
			mutex_unlock(&chip->event_lock);
			ret = bq24192_do_charging(
				temp_mon->maint_chrg_cur,
				temp_mon->maint_chrg_vol_ul);
			if (ret < 0) {
				dev_warn(&chip->client->dev, "do_charing failed\n");
				goto sched_maint_work;
			}
		}
		break;
	case BATT_CHRG_MAINT:
		dev_info(&chip->client->dev,
			"Current batt_mode : BATT_CHRG_MAINT\n");
		if ((is_chrg_full == true) || (is_chrg_term == true)) {
			/* Need to stop charging */
			ret = stop_charging(chip);
			if (ret < 0) {
				dev_warn(&chip->client->dev, "do_charing failed\n");
				goto sched_maint_work;
			}
			mutex_lock(&chip->event_lock);
			chip->batt_mode = BATT_CHRG_FULL;
			mutex_unlock(&chip->event_lock);
		} else if ((vbatt <= temp_mon->maint_chrg_vol_ll) &&
			(vbatt > (temp_mon->maint_chrg_vol_ll - RANGE))) {
			dev_info(&chip->client->dev,
				"Discharging and withing maintenance mode range\n");
			/* if within the range */
			if ((prev_temp_idx != idx) || (sysfs_stat == true)) {
				dev_info(&chip->client->dev,
					"Change in Temp Zone or User Setting:\n");
				ret = bq24192_do_charging(
					temp_mon->maint_chrg_cur,
					temp_mon->maint_chrg_vol_ul);
				if (ret < 0) {
					dev_warn(&chip->client->dev, "do_charing failed\n");
					goto sched_maint_work;
				}
			}
		} else if (vbatt <= temp_mon->maint_chrg_vol_ll - RANGE) {
			dev_info(&chip->client->dev,
				"vbatt less then low voltage threshold\n");
			/* This can happen because of more current being
			 * drawn then  maintenance mode charging charges at
			 */
			ret = bq24192_do_charging(
					temp_mon->full_chrg_cur,
					temp_mon->full_chrg_vol);
			if (ret < 0) {
				dev_warn(&chip->client->dev, "do_charing failed\n");
				goto sched_maint_work;
			}
			mutex_lock(&chip->event_lock);
			chip->batt_mode = BATT_CHRG_NORMAL;
			mutex_unlock(&chip->event_lock);
		} else if (sysfs_stat == true) {
			/* override if non of the condition succeeds
			 * This can happen if none of the cases match but
			 * there is a in that case we must run the
			 * maintenance mode settings again.
			 */
			dev_info(&chip->client->dev,
				"Override chrg params with User conifig\n");
			/* fetch the current voltage being driven */
			ret = bq24192_read_reg(chip->client,
				BQ24192_CHRG_VOLT_CNTL_REG);
			if (ret < 0) {
				dev_warn(&chip->client->dev,
					"Charger Voltage register read failed\n");
				goto sched_maint_work;
			}
			cv = BQ24192_GET_CHRG_VOLT(ret);
			usr_cc = (usr_cc > 0) ? usr_cc :
				temp_mon->full_chrg_cur;
			ret = bq24192_do_charging(usr_cc, cv);
			if (ret < 0) {
				dev_warn(&chip->client->dev, "do_charing failed\n");
				goto sched_maint_work;
			}
		}
		break;
	default:
		dev_warn(&chip->client->dev, "Invalid charing mode\n");
		goto sched_maint_work;
	}
	/* store the current temp index */
	prev_temp_idx = idx;
	power_supply_changed(&chip->usb);
sched_maint_work:
	if ((chip->batt_mode == BATT_CHRG_MAINT) ||
	    (chip->batt_mode == BATT_CHRG_FULL))
		battery_status = POWER_SUPPLY_STATUS_FULL;
	else
		battery_status = POWER_SUPPLY_STATUS_CHARGING;

	if ((!chip->present || !chip->online) ||
	    (chip->chrg_type == POWER_SUPPLY_TYPE_USB_HOST))
		battery_status = POWER_SUPPLY_STATUS_DISCHARGING;

	mutex_lock(&chip->event_lock);
	chip->batt_status = battery_status;
	mutex_unlock(&chip->event_lock);
	schedule_delayed_work(&chip->maint_chrg_wrkr, MAINTENANCE_CHRG_JIFFIES);
	dev_info(&chip->client->dev, "battery mode is  %d\n", chip->batt_mode);
	dev_dbg(&chip->client->dev, "- %s\n", __func__);
}

static int turn_otg_vbus(struct bq24192_chip *chip, bool votg_on)
{
	int ret = 0;

	if (votg_on) {
			/*
			 * Disable WD timer to make sure the WD timer doesn't
			 * expire and put the charger chip into default state
			 * which will bring down the VBUS. The issue will arise
			 * only when the host mode cable is plugged in before
			 * USB charging cable (SDP/DCP/CDP/ACA).
			 */
			ret = program_wdt_timer(chip,
					CHRG_TIMER_EXP_CNTL_WDTDISABLE);
			if (ret < 0) {
				dev_warn(&chip->client->dev,
					"I2C write failed:%s\n", __func__);
				goto i2c_write_fail;
			}

			/* Configure the charger in OTG mode */
			ret = bq24192_reg_read_modify(chip->client,
					BQ24192_POWER_ON_CFG_REG,
					POWER_ON_CFG_CHRG_CFG_OTG, true);
			if (ret < 0) {
				dev_warn(&chip->client->dev,
						"read reg modify failed\n");
				goto i2c_write_fail;
			}
			/* Put the charger IC in reverse boost mode. Since
			 * SDP charger can supply max 500mA charging current
			 * Setting the boost current to 500mA
			 */
			ret = bq24192_reg_read_modify(chip->client,
					BQ24192_POWER_ON_CFG_REG,
					POWER_ON_CFG_BOOST_LIM, false);
			if (ret < 0) {
				dev_warn(&chip->client->dev,
						"read reg modify failed\n");
				goto i2c_write_fail;
			}
			/* assert the chrg_otg gpio now */
			gpio_direction_output(BQ24192_CHRG_OTG_GPIO, 1);
	} else {
			/* Clear the charger from the OTG mode */
			ret = bq24192_reg_read_modify(chip->client,
					BQ24192_POWER_ON_CFG_REG,
					POWER_ON_CFG_CHRG_CFG_OTG, false);
			if (ret < 0) {
				dev_warn(&chip->client->dev,
						"read reg modify failed\n");
				goto i2c_write_fail;
			}
			/* Put the charger IC out of reverse boost mode 500mA */
			ret = bq24192_reg_read_modify(chip->client,
					BQ24192_POWER_ON_CFG_REG,
					POWER_ON_CFG_BOOST_LIM, false);
			if (ret < 0) {
				dev_warn(&chip->client->dev,
						"read reg modify failed\n");
				goto i2c_write_fail;
			}
			/* de-assert the chrg_otg gpio now */
			gpio_direction_output(BQ24192_CHRG_OTG_GPIO, 0);
			gpio_direction_input(BQ24192_CHRG_OTG_GPIO);
	}
i2c_write_fail:
	return ret;
}

static void bq24192_event_worker(struct work_struct *work)
{
	struct bq24192_chip *chip = container_of(work,
				struct bq24192_chip, chrg_evt_wrkr.work);
	int ret;
	int disconnected = 0;

	dev_info(&chip->client->dev, "%s\n", __func__);

	switch (chip->cap.chrg_evt) {
	case POWER_SUPPLY_CHARGER_EVENT_CONNECT:
		pm_runtime_get_sync(&chip->client->dev);
	case POWER_SUPPLY_CHARGER_EVENT_UPDATE:
	case POWER_SUPPLY_CHARGER_EVENT_RESUME:
		if ((chip->chrg_cur_cntl == USER_SET_CHRG_DISABLE) &&
			(chip->chrg_cur_cntl == chip->cached_chrg_cur_cntl)) {
			/* cache the charging parameters as this has come from
			 * USB OTG driver. Typically ends up here when we have
			 * disabled charging through sysfs and connect charger
			 */
			dev_info(&chip->client->dev, "cache the charging parameters");
			dev_info(&chip->client->dev, "notification from USB driver\n");
			mutex_lock(&chip->event_lock);
			chip->cached_cap = chip->cap;
			mutex_unlock(&chip->event_lock);
			break;
		} else if ((chip->cached_chrg_cur_cntl !=
				chip->chrg_cur_cntl) &&
			    (chip->chrg_cur_cntl !=
				USER_SET_CHRG_DISABLE)) {
			/* This is a event generated by exterme charging sysfs
			 * interface restore the cacehd parameter and exit
			 * the switch case */
			mutex_lock(&chip->event_lock);
			chip->cap = chip->cached_cap;
			mutex_unlock(&chip->event_lock);
			dev_info(&chip->client->dev,
				"event generated by sysfs interface\n");
			/* 1. Check the previous power state of USB hardware */
			if ((chip->cached_cap.chrg_evt ==
				POWER_SUPPLY_CHARGER_EVENT_SUSPEND) ||
			    (chip->cached_cap.chrg_evt ==
				POWER_SUPPLY_CHARGER_EVENT_DISCONNECT)) {
				/* In this case the charger is not
				 * attached or is suspended and hence we
				 * will not resume charging
				 */
				dev_dbg(&chip->client->dev,
				"Charger not attached, dnt resume charging\n");
				break;
			}
		}
		/* updating this because we have resumed charging */
		mutex_lock(&chip->event_lock);
		chip->cached_cap = chip->cap;
		mutex_unlock(&chip->event_lock);

		if (chip->cap.chrg_type != POWER_SUPPLY_TYPE_USB_HOST) {
			dev_info(&chip->client->dev, "Enable charging\n");
			/* This is the condition where event has occured
			 * because of SYSFS change or USB driver */
			if ((chip->curr_volt == BQ24192_INVALID_VOLT) ||
				(chip->curr_chrg == BQ24192_INVALID_CURR))
				ret = bq24192_do_charging(BQ24192_DEF_CHRG_CUR,
					BQ24192_DEF_VBATT_MAX);
			else
				ret = bq24192_do_charging(chip->curr_chrg,
					chip->curr_volt);

			if (ret < 0) {
				dev_err(&chip->client->dev,
					"charge enabling failed\n");
				goto i2c_write_fail;
			}

			mutex_lock(&chip->event_lock);
			chip->present = 1;
			chip->online = 1;
			mutex_unlock(&chip->event_lock);
		}

		mutex_lock(&chip->event_lock);
		chip->chrg_type = chip->cap.chrg_type;
		if (chip->chrg_type == POWER_SUPPLY_TYPE_USB_DCP) {
			chip->usb.type = POWER_SUPPLY_TYPE_USB_DCP;
			dev_info(&chip->client->dev,
				 "Charger type DCP\n");
		} else if (chip->chrg_type == POWER_SUPPLY_TYPE_USB_CDP) {
			chip->usb.type = POWER_SUPPLY_TYPE_USB_CDP;
			dev_info(&chip->client->dev,
				"Charger type CDP\n");
		} else if (chip->chrg_type == POWER_SUPPLY_TYPE_USB_ACA) {
			chip->usb.type = POWER_SUPPLY_TYPE_USB_ACA;
			dev_info(&chip->client->dev,
				"Charger type ACA\n");
		} else if (chip->chrg_type == POWER_SUPPLY_TYPE_USB) {
			chip->usb.type = POWER_SUPPLY_TYPE_USB;
			dev_info(&chip->client->dev,
				 "Charger type SDP\n");
		} else if (chip->chrg_type == POWER_SUPPLY_TYPE_USB_HOST) {
			dev_info(&chip->client->dev,
				 "Charger type USB HOST\n");
			ret = turn_otg_vbus(chip, true);
			if (ret < 0) {
				dev_err(&chip->client->dev,
				"turning OTG vbus ON failed\n");
				mutex_unlock(&chip->event_lock);
				goto i2c_write_fail;
			}
			/* otg vbus is turned ON */
			chip->votg = true;
		} else {
			dev_info(&chip->client->dev,
				 "Unknown Charger type\n");
		}
		chip->batt_status = POWER_SUPPLY_STATUS_CHARGING;
		chip->batt_mode = BATT_CHRG_NORMAL;
		mutex_unlock(&chip->event_lock);
		break;
	case POWER_SUPPLY_CHARGER_EVENT_DISCONNECT:
		disconnected = 1;
		pm_runtime_put_sync(&chip->client->dev);
	case POWER_SUPPLY_CHARGER_EVENT_SUSPEND:
		dev_info(&chip->client->dev, "Disable charging\n");
		ret = stop_charging(chip);
		if (ret < 0) {
			dev_err(&chip->client->dev,
				"charge disabling failed\n");
			goto i2c_write_fail;
		}
		mutex_lock(&chip->event_lock);
		if (chip->cap.chrg_evt ==
			POWER_SUPPLY_CHARGER_EVENT_SUSPEND) {
			chip->present = 1;
		} else {
			chip->present = 0;
			chip->chrg_type = chip->cap.chrg_type;
			chip->usb.type = POWER_SUPPLY_TYPE_USB;
		}
		chip->online = 0;
		chip->batt_status = POWER_SUPPLY_STATUS_DISCHARGING;
		if (chip->votg) {
				ret = turn_otg_vbus(chip, false);
				if (ret < 0) {
					dev_err(&chip->client->dev,
						"turning OTG vbus OFF failed\n");

					mutex_unlock(&chip->event_lock);
					goto i2c_write_fail;
				}
				/* otg vbus is turned OFF */
				chip->votg = false;
		}
		chip->batt_mode = BATT_CHRG_NORMAL;
		/* Cache all the parameters */
		chip->curr_volt = BQ24192_INVALID_VOLT;
		chip->curr_chrg = BQ24192_INVALID_CURR;
		/* update the caps if it's a notification coming from USB
		 * driver, since in that case exterme charging parameter
		 * will remain the same and caps must change.
		 */
		if (disconnected) {
			dev_info(&chip->client->dev, "Cached chip->cap\n");
			chip->cached_cap = chip->cap;
		} else
			dev_info(&chip->client->dev, "dnt Cache chip->cap\n");

		chip->cached_chrg_cur_cntl = chip->chrg_cur_cntl;

		mutex_unlock(&chip->event_lock);
		break;
	default:
		dev_err(&chip->client->dev,
			"invalid charger event:%d\n", chip->cap.chrg_evt);
		goto i2c_write_fail;
	}

	power_supply_changed(&chip->usb);
i2c_write_fail:
	return ;
}

int bq24192_slave_mode_enable_charging(int volt, int cur, int ilim)
{
	struct bq24192_chip *chip = i2c_get_clientdata(bq24192_client);
	struct bq24192_chrg_regs reg;
	int ret;

	reg.in_src = chrg_ilim_to_reg(ilim);
	reg.chr_cur = chrg_cur_to_reg(cur);
	reg.chr_volt = chrg_volt_to_reg(volt);

	ret = enable_charging(chip, &reg);
	if (ret < 0)
		dev_err(&chip->client->dev, "charge enable failed\n");

	return ret;
}
EXPORT_SYMBOL(bq24192_slave_mode_enable_charging);

int bq24192_slave_mode_disable_charging(void)
{
	struct bq24192_chip *chip = i2c_get_clientdata(bq24192_client);
	int ret;

	ret = stop_charging(chip);
	if (ret < 0)
		dev_err(&chip->client->dev, "charge disable failed\n");
	return ret;
}
EXPORT_SYMBOL(bq24192_slave_mode_disable_charging);

static void bq24192_charging_port_changed(struct power_supply *psy,
				struct power_supply_charger_cap *cap)
{
	struct bq24192_chip *chip = container_of(psy,
				struct bq24192_chip, usb);

	mutex_lock(&chip->event_lock);
	chip->cap.chrg_evt = cap->chrg_evt;
	chip->cap.chrg_type = cap->chrg_type;
	chip->cap.mA = cap->mA;
	mutex_unlock(&chip->event_lock);

	dev_info(&chip->client->dev, "[chrg] evt:%d type:%d cur:%d\n",
				cap->chrg_evt, cap->chrg_type, cap->mA);
	schedule_delayed_work(&chip->chrg_evt_wrkr, 0);
}

#ifdef CONFIG_DEBUG_FS
#define DBGFS_REG_BUF_LEN	4

static int bq24192_show(struct seq_file *seq, void *unused)
{
	u16 val;
	long addr;

	if (strict_strtol((char *)seq->private, 16, &addr))
		return -EINVAL;

	val = bq24192_read_reg(bq24192_client, addr);
	seq_printf(seq, "%x\n", val);

	return 0;
}

static int bq24192_dbgfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, bq24192_show, inode->i_private);
}

static ssize_t bq24192_dbgfs_reg_write(struct file *file,
		const char __user *user_buf, size_t count, loff_t *ppos)
{
	char buf[DBGFS_REG_BUF_LEN] = {'\0', };
	long addr, value;
	int ret;
	struct seq_file *seq = file->private_data;

	if (!seq || strict_strtol((char *)seq->private, 16, &addr))
		return -EINVAL;

	if (copy_from_user(buf, user_buf, DBGFS_REG_BUF_LEN))
		return -EFAULT;

	if (strict_strtoul(buf, 16, &value))
		return -EINVAL;

	dev_info(&bq24192_client->dev,
			"[dbgfs write] Addr:0x%x Val:0x%x\n",
			(u32)addr, (u32)value);


	ret = bq24192_write_reg(bq24192_client, addr, value);
	if (ret < 0)
		dev_warn(&bq24192_client->dev, "I2C write failed\n");

	return count;
}

static const struct file_operations bq24192_dbgfs_fops = {
	.owner		= THIS_MODULE,
	.open		= bq24192_dbgfs_open,
	.read		= seq_read,
	.write		= bq24192_dbgfs_reg_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int bq24192_create_debugfs(struct bq24192_chip *chip)
{
	int i;
	struct dentry *entry;

	bq24192_dbgfs_root = debugfs_create_dir(DEV_NAME, NULL);
	if (IS_ERR(bq24192_dbgfs_root)) {
		dev_warn(&chip->client->dev, "DEBUGFS DIR create failed\n");
		return -ENOMEM;
	}

	for (i = 0; i < BQ24192_MAX_MEM; i++) {
		sprintf((char *)&bq24192_dbg_regs[i], "%x", i);
		entry = debugfs_create_file(
					(const char *)&bq24192_dbg_regs[i],
					S_IRUGO,
					bq24192_dbgfs_root,
					&bq24192_dbg_regs[i],
					&bq24192_dbgfs_fops);
		if (IS_ERR(entry)) {
			debugfs_remove_recursive(bq24192_dbgfs_root);
			bq24192_dbgfs_root = NULL;
			dev_warn(&chip->client->dev,
					"DEBUGFS entry Create failed\n");
			return -ENOMEM;
		}
	}

	return 0;
}
static inline void bq24192_remove_debugfs(struct bq24192_chip *chip)
{
	if (bq24192_dbgfs_root)
		debugfs_remove_recursive(bq24192_dbgfs_root);
}
#else
static inline int bq24192_create_debugfs(struct bq24192_chip *chip)
{
	return 0;
}
static inline void bq24192_remove_debugfs(struct bq24192_chip *chip)
{
}
#endif

static int bq24192_usb_get_property(struct power_supply *psy,
			    enum power_supply_property psp,
			    union power_supply_propval *val)
{
	struct bq24192_chip *chip = container_of(psy,
				struct bq24192_chip, usb);

	mutex_lock(&chip->event_lock);
	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = chip->present;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = chip->online;
		break;
	case POWER_SUPPLY_PROP_TYPE:
		val->intval = chip->chrg_type;
		break;
	case POWER_SUPPLY_CHARGE_CURRENT_LIMIT:
		val->intval = chip->chrg_cur_cntl;
		break;
	default:
		mutex_unlock(&chip->event_lock);
		return -EINVAL;
	}
	mutex_unlock(&chip->event_lock);

	return 0;
}

static int bq24192_usb_set_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       const union power_supply_propval *val)
{
	struct bq24192_chip *chip = container_of(psy,
				struct bq24192_chip, usb);
	int ret;

	mutex_lock(&chip->event_lock);
	switch (psp) {
	case POWER_SUPPLY_CHARGE_CURRENT_LIMIT:
		ret = update_chrcurr_settings(chip, val->intval);
		if (ret < 0)
			goto usb_set_prop_exit;

		chip->chrg_cur_cntl = val->intval;
		break;
	default:
		ret = -EPERM;
	}
usb_set_prop_exit:
	mutex_unlock(&chip->event_lock);
	return ret;
}

static int bq24192_usb_property_is_writeable(struct power_supply *psy,
						enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_CHARGE_CURRENT_LIMIT:
		return 1;
	default:
		break;
	}

	return 0;
}

/**
 * init_batt_thresholds - initialize battery thresholds
 * @chp: charger driver device context
 * Context: can sleep
 */
static void init_batt_thresholds(struct bq24192_chip *chip)
{

	chip->batt_thrshlds.vbatt_sh_min = CLT_BATT_VMIN_THRESHOLD_DEF;
	chip->batt_thrshlds.vbatt_crit = CLT_BATT_CRIT_CUTOFF_VOLT_DEF;
	chip->batt_thrshlds.temp_high = CLT_BATT_TEMP_MAX_DEF;
	chip->batt_thrshlds.temp_low = CLT_BATT_TEMP_MIN_DEF;
	/* Need to add SMIP related support to fetch this information.
	 * This is currently not supported by FW and hence using hard
	 * coded values
	 */
	ctp_get_batt_temp_thresholds(&chip->batt_thrshlds.temp_high,
		&chip->batt_thrshlds.temp_low);
}

static void init_charger_regs(struct bq24192_chip *chip)
{
	int ret;

	/* disable WDT timer */
	ret = program_wdt_timer(chip, CHRG_TIMER_EXP_CNTL_WDTDISABLE);
	if (ret < 0)
		dev_warn(&chip->client->dev, "I2C write failed:%s\n", __func__);

	/* disable the charger */
	ret = bq24192_reg_multi_bitset(chip->client, BQ24192_POWER_ON_CFG_REG,
						POWER_ON_CFG_CHRG_CFG_DIS,
					CHR_CFG_BIT_POS, CHR_CFG_BIT_LEN);
	if (ret < 0)
		dev_warn(&chip->client->dev, "I2C write failed:%s\n", __func__);

	/* disable Charge Termination */
	ret = bq24192_reg_read_modify(chip->client,
			BQ24192_CHRG_TIMER_EXP_CNTL_REG,
				CHRG_TIMER_EXP_CNTL_EN_TERM, false);
	if (ret < 0)
		dev_warn(&chip->client->dev, "I2C write failed:%s\n", __func__);

	/* set safty charge time to maximum */
	ret = bq24192_reg_multi_bitset(chip->client,
					BQ24192_CHRG_TIMER_EXP_CNTL_REG,
					CHRG_TIMER_EXP_CNTL_SFT_TIMER,
					SFT_TIMER_BIT_POS, SFT_TIMER_BIT_LEN);
	if (ret < 0)
		dev_warn(&chip->client->dev, "I2C write failed:%s\n", __func__);

	/* disable charger interrupts */
	ret = bq24192_reg_read_modify(chip->client,
					BQ24192_MISC_OP_CNTL_REG,
					MISC_OP_CNTL_MINT_CHRG, false);
	if (ret < 0)
		dev_warn(&chip->client->dev, "I2C write failed:%s\n", __func__);

	/* disable battery interrupts */
	ret = bq24192_reg_read_modify(chip->client,
					BQ24192_MISC_OP_CNTL_REG,
					MISC_OP_CNTL_MINT_BATT, false);
	if (ret < 0)
		dev_warn(&chip->client->dev, "I2C write failed:%s\n", __func__);
}

static int __devinit bq24192_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct bq24192_chip *chip;
	int ret;

	if (!client->dev.platform_data) {
		dev_err(&client->dev, "platform Data is NULL");
		return -EFAULT;
	}

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&client->dev,
				"SMBus doesn't support BYTE transactions\n");
		return -EIO;
	}

	chip = kzalloc(sizeof(struct bq24192_chip), GFP_KERNEL);
	if (!chip) {
		dev_err(&client->dev, "mem alloc failed\n");
		return -ENOMEM;
	}

	chip->client = client;
	chip->pdata = client->dev.platform_data;
	i2c_set_clientdata(client, chip);
	bq24192_client = client;
	ret = bq24192_read_reg(client, BQ24192_VENDER_REV_REG);
	if (ret < 0) {
		dev_err(&client->dev, "i2c read err:%d\n", ret);
		i2c_set_clientdata(client, NULL);
		kfree(chip);
		return -EIO;
	}

	/* D3, D4, D5 indicates the chip model number */
	ret = (ret >> 3) & 0x07;
	if ((ret != BQ24192I_IC_VERSION) &&
		(ret != BQ24192_IC_VERSION) &&
		(ret != BQ24191_IC_VERSION) &&
		(ret != BQ24190_IC_VERSION)) {
		dev_err(&client->dev, "device version mismatch: %x\n", ret);
		i2c_set_clientdata(client, NULL);
		kfree(chip);
		return -EIO;
	}

	ret = device_create_file(&chip->client->dev,
		&dev_attr_charge_current_limit);
	if (ret) {
		dev_err(&chip->client->dev,
			"Failed to create sysfs:charge_current_limit\n");
	}

	ret = gpio_request(BQ24192_CHRG_OTG_GPIO, "CHRG_OTG");
	if (ret) {
		dev_err(&chip->client->dev,
			"Failed to request gpio %d with error %d\n",
			BQ24192_CHRG_OTG_GPIO, ret);
	}
	dev_info(&chip->client->dev, "request gpio %d for CHRG_OTG pin\n",
			BQ24192_CHRG_OTG_GPIO);

	INIT_DELAYED_WORK(&chip->chrg_evt_wrkr, bq24192_event_worker);
	INIT_DELAYED_WORK(&chip->stat_mon_wrkr, bq24192_monitor_worker);
	INIT_DELAYED_WORK(&chip->maint_chrg_wrkr, bq24192_maintenance_worker);
	mutex_init(&chip->event_lock);

	chip->chrg_cur_cntl = POWER_SUPPLY_CHARGE_CURRENT_LIMIT_NONE;
	chip->batt_status = POWER_SUPPLY_STATUS_DISCHARGING;
	chip->batt_mode = BATT_CHRG_NONE;
	chip->curr_volt = BQ24192_INVALID_VOLT;
	chip->curr_chrg = BQ24192_INVALID_CURR;
	chip->cached_chrg_cur_cntl = POWER_SUPPLY_CHARGE_CURRENT_LIMIT_NONE;

	/* register bq24192 usb with power supply subsystem */
	if (!chip->pdata->slave_mode) {
		chip->usb.name = CHARGER_PS_NAME;
		chip->usb.type = POWER_SUPPLY_TYPE_USB;
		chip->usb.supplied_to = bq24192_power_supplied_to;
		chip->usb.num_supplicants =
				ARRAY_SIZE(bq24192_power_supplied_to);
		chip->usb.properties = bq24192_usb_props;
		chip->usb.num_properties = ARRAY_SIZE(bq24192_usb_props);
		chip->usb.get_property = bq24192_usb_get_property;
		chip->usb.set_property = bq24192_usb_set_property;
		chip->usb.property_is_writeable =
					  bq24192_usb_property_is_writeable;
		chip->usb.charging_port_changed = bq24192_charging_port_changed;
		ret = power_supply_register(&client->dev, &chip->usb);
		if (ret) {
			dev_err(&client->dev, "failed:power supply register\n");
			i2c_set_clientdata(client, NULL);
			kfree(chip);
			return ret;
		}
	}


	ctp_sfi_table = kzalloc(sizeof(struct ctp_batt_sfi_prop), GFP_KERNEL);
	if (!ctp_sfi_table) {
		dev_err(&client->dev, "%s(): memory allocation failed\n",
			__func__);
		kfree(chip);
		return -ENOMEM;
	}

	/* check for valid SFI table entry for OEM0 table */
	if (sfi_table_parse(SFI_BATTPROP_TBL_ID, NULL, NULL,
		ctp_sfi_table_populate)) {
		chip->pdata->sfi_tabl_present = false;
		ctp_sfi_table_invalid_batt(ctp_sfi_table);
	}

	/* Allocate ADC Channels */
	chip->gpadc_handle =
		intel_mid_gpadc_alloc(CLT_BATT_NUM_GPADC_SENSORS,
				  CLT_GPADC_BPTHERM_CHNUM | CH_NEED_VCALIB |
				  CH_NEED_VREF);
	if (chip->gpadc_handle == NULL) {
		dev_err(&client->dev, "ADC allocation failed : Check if ADC driver came up\n");
		return -1;
	}

	init_batt_thresholds(chip);

	/* Init Runtime PM State */
	pm_runtime_put_noidle(&chip->client->dev);
	pm_schedule_suspend(&chip->client->dev, MSEC_PER_SEC);

	/* create debugfs for maxim registers */
	ret = bq24192_create_debugfs(chip);
	if (ret < 0) {
		dev_err(&client->dev, "debugfs create failed\n");
		power_supply_unregister(&chip->usb);
		i2c_set_clientdata(client, NULL);
		kfree(chip);
		intel_mid_gpadc_free(chip->gpadc_handle);
		return ret;
	}
	/* start the status monitor worker */
	schedule_delayed_work(&chip->stat_mon_wrkr, 0);
	/* start the maintenance charge worker */
	schedule_delayed_work(&chip->maint_chrg_wrkr, 0);
	return 0;
}

static int __devexit bq24192_remove(struct i2c_client *client)
{
	struct bq24192_chip *chip = i2c_get_clientdata(client);

	bq24192_remove_debugfs(chip);
	if (!chip->pdata->slave_mode)
		power_supply_unregister(&chip->usb);
	i2c_set_clientdata(client, NULL);
	intel_mid_gpadc_free(chip->gpadc_handle);
	kfree(ctp_sfi_table);
	kfree(chip);
	return 0;
}

#ifdef CONFIG_PM
static int bq24192_suspend(struct device *dev)
{
	struct bq24192_chip *chip = dev_get_drvdata(dev);

	cancel_delayed_work(&chip->stat_mon_wrkr);
	cancel_delayed_work(&chip->maint_chrg_wrkr);
	dev_dbg(&chip->client->dev, "bq24192 suspend\n");
	return 0;
}

static int bq24192_resume(struct device *dev)
{
	struct bq24192_chip *chip = dev_get_drvdata(dev);

	schedule_delayed_work(&chip->stat_mon_wrkr, 0);
	schedule_delayed_work(&chip->maint_chrg_wrkr, 0);
	dev_dbg(&chip->client->dev, "bq24192 resume\n");
	return 0;
}
#else
#define bq24192_suspend NULL
#define bq24192_resume NULL
#endif

#ifdef CONFIG_PM_RUNTIME
static int bq24192_runtime_suspend(struct device *dev)
{

	dev_dbg(dev, "%s called\n", __func__);
	return 0;
}

static int bq24192_runtime_resume(struct device *dev)
{
	dev_dbg(dev, "%s called\n", __func__);
	return 0;
}

static int bq24192_runtime_idle(struct device *dev)
{

	dev_dbg(dev, "%s called\n", __func__);
	return 0;
}
#else
#define bq24192_runtime_suspend	NULL
#define bq24192_runtime_resume		NULL
#define bq24192_runtime_idle		NULL
#endif

static const struct i2c_device_id bq24192_id[] = {
	{ DEV_NAME, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, bq24192_id);

static const struct dev_pm_ops bq24192_pm_ops = {
	.suspend		= bq24192_suspend,
	.resume			= bq24192_resume,
	.runtime_suspend	= bq24192_runtime_suspend,
	.runtime_resume		= bq24192_runtime_resume,
	.runtime_idle		= bq24192_runtime_idle,
};

static struct i2c_driver bq24192_i2c_driver = {
	.driver	= {
		.name	= DEV_NAME,
		.owner	= THIS_MODULE,
		.pm	= &bq24192_pm_ops,
	},
	.probe		= bq24192_probe,
	.remove		= __devexit_p(bq24192_remove),
	.id_table	= bq24192_id,
};

static int __init bq24192_init(void)
{
	return i2c_add_driver(&bq24192_i2c_driver);
}
module_init(bq24192_init);

static void __exit bq24192_exit(void)
{
	i2c_del_driver(&bq24192_i2c_driver);
}
module_exit(bq24192_exit);

MODULE_AUTHOR("Ramakrishna Pallala <ramakrishna.pallala@intel.com>");
MODULE_DESCRIPTION("BQ24192 Charger Driver");
MODULE_LICENSE("GPL");
