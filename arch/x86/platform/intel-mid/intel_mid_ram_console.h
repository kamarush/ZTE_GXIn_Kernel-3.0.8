/*
 * Intel mid ram console support
 *
 * Copyright (C) 2012 Motorola Mobility, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.

 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __INTEL_MID_RAM_CONSOLE_H
#define __INTEL_MID_RAM_CONSOLE_H

#define SZ_2M                           0x00200000
#define SZ_16M                          0x01000000

/* Board files use the following if they are ok with 16M size defaults */
#define INTEL_MID_RAM_CONSOLE_START_DEFAULT	SZ_16M
#define INTEL_MID_RAM_CONSOLE_SIZE_DEFAULT	SZ_2M

#endif
