/*
 * platform_pmic_audio.h: PMIC AUDIO platform data header file
 *
 * (C) Copyright 2008 Intel Corporation
 * Author:
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */
#ifndef _PLATFORM_PMIC_AUDIO_H_
#define _PLATFORM_PMIC_AUDIO_H_

extern void __init *pmic_audio_platform_data(void *info) __attribute__((weak));
#endif
