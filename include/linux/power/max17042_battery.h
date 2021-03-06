/*
 * max17042_battery.h - Fuel gauge driver for Maxim 17042 / 8966 / 8997
 *  Note that Maxim 8966 and 8997 are mfd and this is its subdevice.
 *
 * Copyright (C) 2011 Samsung Electronics
 * MyungJoo Ham <myungjoo.ham@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __MAX17042_BATTERY_H_
#define __MAX17042_BATTERY_H_

/* No of cell characterization words to be written to max17042 */
#define CELL_CHAR_TBL_SAMPLES	48

/* fuel gauge table type for DV10 platfrom */
#define MAX17042_TBL_TYPE_DV10	0xff

struct max17042_config_data {
	/*
	 * if config_init is 0, which means new
	 * configuration has been loaded in that case
	 * we need to perform complete init of chip
	 */
	u16	size;
	u16	checksum;
	u8	table_type;
	u8	config_init;

	u16	rcomp0;
	u16	tempCo;
	u16	kempty0;
	u16	full_cap;
	u16	cycles;
	u16	full_capnom;

	u16	qrtbl00;
	u16	qrtbl10;
	u16	qrtbl20;
	u16	qrtbl30;
	u16	full_soc_thr;
	u16	vempty;

	u16	soc_empty;
	u16	ichgt_term;
	u16	design_cap;
	u16	etc;
	u16	rsense;
	u16	cfg;
	u16	learn_cfg;
	u16	filter_cfg;
	u16	relax_cfg;


	u16	cell_char_tbl[CELL_CHAR_TBL_SAMPLES];
} __packed;

struct max17042_platform_data {
	bool enable_current_sense;
	bool is_init_done;
	bool is_volt_shutdown;
	bool is_capacity_shutdown;
	bool is_lowbatt_shutdown;
	int technology;

	/* battery safety thresholds */
	int temp_min_lim;	/* in degrees centigrade */
	int temp_max_lim;	/* in degrees centigrade */
	int volt_min_lim;	/* milli volts */
	int volt_max_lim;	/* milli volts */

	int (*current_sense_enabled)(void);
	int (*battery_present)(void);
	int (*battery_health)(void);
	int (*battery_status)(void);
	int (*battery_pack_temp)(int *);
	int (*save_config_data)(const char *name, void *data, int len);
	int (*restore_config_data)(const char *name, void *data, int len);
	void (*reset_i2c_lines)(void);

	bool (*is_cap_shutdown_enabled)(void);
	bool (*is_volt_shutdown_enabled)(void);
	bool (*is_lowbatt_shutdown_enabled)(void);
	int (*get_vmin_threshold)(void);
};

#endif /* __MAX17042_BATTERY_H_ */
