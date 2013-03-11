/*
 * Support for Medfield PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2010 Intel Corporation. All Rights Reserved.
 *
 * Copyright (c) 2010 Silicon Hive www.siliconhive.com.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#ifndef _SH_CSS_METRICS_H_
#define _SH_CSS_METRICS_H_

#include "sh_css_types.h"

struct sh_css_pc_histogram {
	unsigned length;
	unsigned *run;
	unsigned *stall;
	unsigned *msink;
};

struct sh_css_binary_metrics {
	unsigned mode;
	struct sh_css_pc_histogram isp_histogram;
	struct sh_css_pc_histogram sp_histogram;
	struct sh_css_binary_metrics *next;
};

struct sh_css_frame_metrics {
	unsigned num_frames;
};

struct sh_css_metrics {
	struct sh_css_binary_metrics *binary_metrics;
	struct sh_css_frame_metrics   frame_metrics;
};

extern struct sh_css_metrics sh_css_metrics;

/* Sample ISP and SP pc and add to histogram */
void sh_css_metrics_enable_pc_histogram(bool enable);
void sh_css_metrics_start_frame(void);
void sh_css_metrics_start_binary(struct sh_css_binary_metrics *metrics);
void sh_css_metrics_sample_pcs(void);

#endif /* _SH_CSS_METRICS_H_ */