/**********************************************************************
 *
 * Copyright (C) Imagination Technologies Ltd. All rights reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope it will be useful but, except 
 * as otherwise stated in writing, without any warranty; without even the 
 * implied warranty of merchantability or fitness for a particular purpose. 
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * 
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * Imagination Technologies Ltd. <gpl-support@imgtec.com>
 * Home Park Estate, Kings Langley, Herts, WD4 8LZ, UK 
 *
 ******************************************************************************/

#ifndef __SGXCONFIG_H__
#define __SGXCONFIG_H__

#include "sgxdefs.h"

#define DEV_DEVICE_TYPE			PVRSRV_DEVICE_TYPE_SGX
#define DEV_DEVICE_CLASS		PVRSRV_DEVICE_CLASS_3D

#define DEV_MAJOR_VERSION		1
#define DEV_MINOR_VERSION		0

#if defined(SUPPORT_EXTERNAL_SYSTEM_CACHE)
#define SGX_KERNEL_DATA_HEAP_OFFSET		0x00001000
#else
#define SGX_KERNEL_DATA_HEAP_OFFSET		0x00000000
#endif

#if SGX_FEATURE_ADDRESS_SPACE_SIZE == 32
#if defined(FIX_HW_BRN_31620)
	#if defined(SGX_FEATURE_2D_HARDWARE)
	#define SGX_2D_HEAP_BASE					 0x04000000
	#define SGX_2D_HEAP_SIZE					(0x08000000-0x04000000-0x00001000)
	#endif

	#define SGX_GENERAL_HEAP_BASE				 0x08000000
	#define SGX_GENERAL_HEAP_SIZE				(0xB8000000-0x00001000)

	
	#define SGX_3DPARAMETERS_HEAP_SIZE			0x10000000

	
#if !defined(HYBRID_SHARED_PB_SIZE)
	#define HYBRID_SHARED_PB_SIZE				(SGX_3DPARAMETERS_HEAP_SIZE >> 1)
#endif
#if defined(SUPPORT_HYBRID_PB)
	#define SGX_SHARED_3DPARAMETERS_SIZE			(HYBRID_SHARED_PB_SIZE)
	#define SGX_SHARED_3DPARAMETERS_HEAP_SIZE		(HYBRID_SHARED_PB_SIZE-0x00001000)
	#define SGX_PERCONTEXT_3DPARAMETERS_HEAP_SIZE		(SGX_3DPARAMETERS_HEAP_SIZE - SGX_SHARED_3DPARAMETERS_SIZE - 0x00001000)
#else
#if defined(SUPPORT_PERCONTEXT_PB)
	#define SGX_SHARED_3DPARAMETERS_SIZE			0
	#define SGX_SHARED_3DPARAMETERS_HEAP_SIZE		0
	#define SGX_PERCONTEXT_3DPARAMETERS_HEAP_SIZE		(SGX_3DPARAMETERS_HEAP_SIZE - 0x00001000)
#endif
#if defined(SUPPORT_SHARED_PB)
	#define SGX_SHARED_3DPARAMETERS_SIZE			SGX_3DPARAMETERS_HEAP_SIZE
	#define SGX_SHARED_3DPARAMETERS_HEAP_SIZE		(SGX_3DPARAMETERS_HEAP_SIZE - 0x00001000)
	#define SGX_PERCONTEXT_3DPARAMETERS_HEAP_SIZE		0
#endif
#endif

	#define SGX_SHARED_3DPARAMETERS_HEAP_BASE		 0xC0000000
	

	#define SGX_PERCONTEXT_3DPARAMETERS_HEAP_BASE		 (SGX_SHARED_3DPARAMETERS_HEAP_BASE + SGX_SHARED_3DPARAMETERS_SIZE)
	

	#define SGX_TADATA_HEAP_BASE				 0xD0000000
	#define SGX_TADATA_HEAP_SIZE				(0x0D000000-0x00001000)

	#define SGX_SYNCINFO_HEAP_BASE				 0xE0000000
	#define SGX_SYNCINFO_HEAP_SIZE				(0x01000000-0x00001000)

	#define SGX_PDSPIXEL_CODEDATA_HEAP_BASE		 0xE4000000
	#define SGX_PDSPIXEL_CODEDATA_HEAP_SIZE		(0x02000000-0x00001000)

	#define SGX_KERNEL_CODE_HEAP_BASE			 0xE8000000
	#define SGX_KERNEL_CODE_HEAP_SIZE			(0x00080000-0x00001000)

	#define SGX_PDSVERTEX_CODEDATA_HEAP_BASE	 0xEC000000
	#define SGX_PDSVERTEX_CODEDATA_HEAP_SIZE	(0x01C00000-0x00001000)

	#define SGX_KERNEL_DATA_HEAP_BASE		 	0xF0000000
	#define SGX_KERNEL_DATA_HEAP_SIZE			(0x03000000-(0x00001000+SGX_KERNEL_DATA_HEAP_OFFSET))

	
	#define SGX_PIXELSHADER_HEAP_BASE			 0xF4000000
	#define SGX_PIXELSHADER_HEAP_SIZE			(0x05000000-0x00001000)
	
	#define SGX_VERTEXSHADER_HEAP_BASE			 0xFC000000
	#define SGX_VERTEXSHADER_HEAP_SIZE			(0x02000000-0x00001000)
#else 
	#if defined(SGX_FEATURE_2D_HARDWARE)
	#define SGX_2D_HEAP_BASE					 0x00100000
	#define SGX_2D_HEAP_SIZE					(0x08000000-0x00100000-0x00001000)
	#else
		#if defined(FIX_HW_BRN_26915)
		#define SGX_CGBUFFER_HEAP_BASE					 0x00100000
		#define SGX_CGBUFFER_HEAP_SIZE					(0x08000000-0x00100000-0x00001000)
		#endif
	#endif

	#if defined(SUPPORT_SGX_GENERAL_MAPPING_HEAP)
	#define SGX_GENERAL_MAPPING_HEAP_BASE		 0x08000000
	#define SGX_GENERAL_MAPPING_HEAP_SIZE		(0x08000000-0x00001000)
	#endif

	#if !defined(SUPPORT_MEMORY_TILING)
	#define SGX_GENERAL_HEAP_BASE				 0x10000000
	#define SGX_GENERAL_HEAP_SIZE				(0xC2000000-0x00001000)
	#else
		#include <sgx_msvdx_defs.h>
		
	 	
	 	#define SGX_GENERAL_HEAP_BASE				 0x10000000
		#define SGX_GENERAL_HEAP_SIZE				(0xB5000000-0x00001000)

		#define SGX_VPB_TILED_HEAP_STRIDE			TILING_TILE_STRIDE_2K
		#define SGX_VPB_TILED_HEAP_BASE		 0xC5000000
		#define SGX_VPB_TILED_HEAP_SIZE	(0x0D000000-0x00001000)

		
		#if((SGX_VPB_TILED_HEAP_BASE & SGX_BIF_TILING_ADDR_INV_MASK) != 0)
		#error "sgxconfig.h: SGX_VPB_TILED_HEAP has insufficient alignment"
		#endif

	#endif 

	
	#define SGX_3DPARAMETERS_HEAP_SIZE			0x10000000

	
#if !defined(HYBRID_SHARED_PB_SIZE)
	#define HYBRID_SHARED_PB_SIZE				(SGX_3DPARAMETERS_HEAP_SIZE >> 1)
#endif
#if defined(SUPPORT_HYBRID_PB)
	#define SGX_SHARED_3DPARAMETERS_SIZE			(HYBRID_SHARED_PB_SIZE)
	#define SGX_SHARED_3DPARAMETERS_HEAP_SIZE		(HYBRID_SHARED_PB_SIZE-0x00001000)
	#define SGX_PERCONTEXT_3DPARAMETERS_HEAP_SIZE		(SGX_3DPARAMETERS_HEAP_SIZE - SGX_SHARED_3DPARAMETERS_SIZE - 0x00001000)
#else
#if defined(SUPPORT_PERCONTEXT_PB)
	#define SGX_SHARED_3DPARAMETERS_SIZE			0
	#define SGX_SHARED_3DPARAMETERS_HEAP_SIZE		0
	#define SGX_PERCONTEXT_3DPARAMETERS_HEAP_SIZE		(SGX_3DPARAMETERS_HEAP_SIZE - 0x00001000)
#endif
#if defined(SUPPORT_SHARED_PB)
	#define SGX_SHARED_3DPARAMETERS_SIZE			SGX_3DPARAMETERS_HEAP_SIZE
	#define SGX_SHARED_3DPARAMETERS_HEAP_SIZE		(SGX_3DPARAMETERS_HEAP_SIZE - 0x00001000)
	#define SGX_PERCONTEXT_3DPARAMETERS_HEAP_SIZE		0
#endif
#endif

	#define SGX_SHARED_3DPARAMETERS_HEAP_BASE		 0xD2000000
	

	#define SGX_PERCONTEXT_3DPARAMETERS_HEAP_BASE		 (SGX_SHARED_3DPARAMETERS_HEAP_BASE + SGX_SHARED_3DPARAMETERS_SIZE)
	

	#define SGX_TADATA_HEAP_BASE				 0xE2000000
	#define SGX_TADATA_HEAP_SIZE				(0x0D000000-0x00001000)

	#define SGX_SYNCINFO_HEAP_BASE				 0xEF000000
	#define SGX_SYNCINFO_HEAP_SIZE				(0x01000000-0x00001000)

	#define SGX_PDSPIXEL_CODEDATA_HEAP_BASE		 0xF0000000
	#define SGX_PDSPIXEL_CODEDATA_HEAP_SIZE		(0x02000000-0x00001000)

	#define SGX_KERNEL_CODE_HEAP_BASE			 0xF2000000
	#define SGX_KERNEL_CODE_HEAP_SIZE			(0x00080000-0x00001000)

	#define SGX_PDSVERTEX_CODEDATA_HEAP_BASE	 0xF2400000
	#define SGX_PDSVERTEX_CODEDATA_HEAP_SIZE	(0x01C00000-0x00001000)

	#define SGX_KERNEL_DATA_HEAP_BASE		 	0xF4000000
	#define SGX_KERNEL_DATA_HEAP_SIZE			(0x05000000-(0x00001000+SGX_KERNEL_DATA_HEAP_OFFSET))

	
	#define SGX_PIXELSHADER_HEAP_BASE			 0xF9000000
	#define SGX_PIXELSHADER_HEAP_SIZE			(0x05000000-0x00001000)
	
	#define SGX_VERTEXSHADER_HEAP_BASE			 0xFE000000
	#define SGX_VERTEXSHADER_HEAP_SIZE			(0x02000000-0x00001000)
#endif 
	
	#define SGX_CORE_IDENTIFIED
#endif 

#if SGX_FEATURE_ADDRESS_SPACE_SIZE == 28

#if defined(SUPPORT_SGX_GENERAL_MAPPING_HEAP)
	#define SGX_GENERAL_MAPPING_HEAP_BASE		 0x00001000
	#define SGX_GENERAL_MAPPING_HEAP_SIZE		(0x01800000-0x00001000-0x00001000)

	#define SGX_GENERAL_HEAP_BASE				 0x01800000
	#define SGX_GENERAL_HEAP_SIZE				(0x07000000-0x00001000)

#else
	#define SGX_GENERAL_HEAP_BASE				 0x00001000
#if defined(SUPPORT_LARGE_GENERAL_HEAP)
	#define SGX_GENERAL_HEAP_SIZE				(0x0B800000-0x00001000-0x00001000)
#else
	#define SGX_GENERAL_HEAP_SIZE				(0x08800000-0x00001000-0x00001000)
#endif
#endif
	
#if defined(SUPPORT_LARGE_GENERAL_HEAP)
	#define SGX_3DPARAMETERS_HEAP_SIZE			0x01000000
#else
	#define SGX_3DPARAMETERS_HEAP_SIZE			0x04000000
#endif

	
#if !defined(HYBRID_SHARED_PB_SIZE)
	#define HYBRID_SHARED_PB_SIZE				(SGX_3DPARAMETERS_HEAP_SIZE >> 1)
#endif
#if defined(SUPPORT_HYBRID_PB)
	#define SGX_SHARED_3DPARAMETERS_SIZE			(HYBRID_SHARED_PB_SIZE)
	#define SGX_SHARED_3DPARAMETERS_HEAP_SIZE		(HYBRID_SHARED_PB_SIZE-0x00001000)
	#define SGX_PERCONTEXT_3DPARAMETERS_HEAP_SIZE		(SGX_3DPARAMETERS_HEAP_SIZE - SGX_SHARED_3DPARAMETERS_SIZE - 0x00001000)
#else
#if defined(SUPPORT_PERCONTEXT_PB)
	#define SGX_SHARED_3DPARAMETERS_SIZE			0
	#define SGX_SHARED_3DPARAMETERS_HEAP_SIZE		0
	#define SGX_PERCONTEXT_3DPARAMETERS_HEAP_SIZE		(SGX_3DPARAMETERS_HEAP_SIZE - 0x00001000)
#endif
#if defined(SUPPORT_SHARED_PB)
	#define SGX_SHARED_3DPARAMETERS_SIZE			SGX_3DPARAMETERS_HEAP_SIZE
	#define SGX_SHARED_3DPARAMETERS_HEAP_SIZE		(SGX_3DPARAMETERS_HEAP_SIZE - 0x00001000)
	#define SGX_PERCONTEXT_3DPARAMETERS_HEAP_SIZE		0
#endif
#endif

#if defined(SUPPORT_LARGE_GENERAL_HEAP)
	#define SGX_SHARED_3DPARAMETERS_HEAP_BASE		 0x0B800000
#else
	#define SGX_SHARED_3DPARAMETERS_HEAP_BASE		 0x08800000
#endif

	

	#define SGX_PERCONTEXT_3DPARAMETERS_HEAP_BASE		 (SGX_SHARED_3DPARAMETERS_HEAP_BASE + SGX_SHARED_3DPARAMETERS_SIZE)
	

	#define SGX_TADATA_HEAP_BASE				 0x0C800000
	#define SGX_TADATA_HEAP_SIZE				(0x01000000-0x00001000)

	#define SGX_SYNCINFO_HEAP_BASE				 0x0D800000
	#define SGX_SYNCINFO_HEAP_SIZE				(0x00400000-0x00001000)

	#define SGX_PDSPIXEL_CODEDATA_HEAP_BASE		 0x0DC00000
	#define SGX_PDSPIXEL_CODEDATA_HEAP_SIZE		(0x00800000-0x00001000)

	#define SGX_KERNEL_CODE_HEAP_BASE			 0x0E400000
	#define SGX_KERNEL_CODE_HEAP_SIZE			(0x00080000-0x00001000)

	#define SGX_PDSVERTEX_CODEDATA_HEAP_BASE	 0x0E800000
	#define SGX_PDSVERTEX_CODEDATA_HEAP_SIZE	(0x00800000-0x00001000)

	#define SGX_KERNEL_DATA_HEAP_BASE			0x0F000000
	#define SGX_KERNEL_DATA_HEAP_SIZE			(0x00400000-(0x00001000+SGX_KERNEL_DATA_HEAP_OFFSET))

	#define SGX_PIXELSHADER_HEAP_BASE			 0x0F400000
	#define SGX_PIXELSHADER_HEAP_SIZE			(0x00500000-0x00001000)

	#define SGX_VERTEXSHADER_HEAP_BASE			 0x0FC00000
	#define SGX_VERTEXSHADER_HEAP_SIZE			(0x00200000-0x00001000)

	
	#define SGX_CORE_IDENTIFIED

#endif 

#if !defined(SGX_CORE_IDENTIFIED)
	#error "sgxconfig.h: ERROR: unspecified SGX Core version"
#endif

#if !defined (SGX_FEATURE_EDM_VERTEX_PDSADDR_FULL_RANGE)
	#if ((SGX_KERNEL_CODE_HEAP_BASE + SGX_KERNEL_CODE_HEAP_SIZE - SGX_PDSPIXEL_CODEDATA_HEAP_BASE) >  0x4000000)
	 	#error "sgxconfig.h: ERROR: SGX_KERNEL_CODE_HEAP_BASE out of range of SGX_PDSPIXEL_CODEDATA_HEAP_BASE"
	#endif
	
	#if ((SGX_PDSVERTEX_CODEDATA_HEAP_BASE + SGX_PDSVERTEX_CODEDATA_HEAP_SIZE - SGX_PDSPIXEL_CODEDATA_HEAP_BASE) >  0x4000000)
	 	#error "sgxconfig.h: ERROR: SGX_PDSVERTEX_CODEDATA_HEAP_BASE out of range of SGX_PDSPIXEL_CODEDATA_HEAP_BASE"
	#endif
#endif	

#if defined(SGX_FEATURE_2D_HARDWARE) && defined(SUPPORT_SGX_GENERAL_MAPPING_HEAP)
	#if ((SGX_GENERAL_MAPPING_HEAP_BASE + SGX_GENERAL_MAPPING_HEAP_SIZE - SGX_2D_HEAP_BASE) >= EUR_CR_BIF_TWOD_REQ_BASE_ADDR_MASK)
		#error "sgxconfig.h: ERROR: SGX_GENERAL_MAPPING_HEAP inaccessable by 2D requestor"
	#endif
#endif

#if defined (EURASIA_USE_CODE_PAGE_SIZE)
	#if ((SGX_KERNEL_CODE_HEAP_BASE & (EURASIA_USE_CODE_PAGE_SIZE - 1)) != 0)
		#error "sgxconfig.h: ERROR: Kernel code heap base misalignment"
	#endif
#endif

#if defined(SGX_FEATURE_2D_HARDWARE)
	#if defined(SUPPORT_SGX_GENERAL_MAPPING_HEAP)
		#if ((SGX_2D_HEAP_BASE + SGX_2D_HEAP_SIZE) >= SGX_GENERAL_MAPPING_HEAP_BASE)
			#error "sgxconfig.h: ERROR: SGX_2D_HEAP overlaps SGX_GENERAL_MAPPING_HEAP"
		#endif
	#else
		#if ((SGX_2D_HEAP_BASE + SGX_2D_HEAP_SIZE) >= SGX_GENERAL_HEAP_BASE)
			#error "sgxconfig.h: ERROR: SGX_2D_HEAP overlaps SGX_GENERAL_HEAP_BASE"
		#endif
	#endif
#else
    #if defined(FIX_HW_BRN_26915)
		#if defined(SUPPORT_SGX_GENERAL_MAPPING_HEAP)
			#if ((SGX_CGBUFFER_HEAP_BASE + SGX_CGBUFFER_HEAP_SIZE) >= SGX_GENERAL_MAPPING_HEAP_BASE)
				#error "sgxconfig.h: ERROR: SGX_CGBUFFER_HEAP overlaps SGX_GENERAL_MAPPING_HEAP"
			#endif
		#else
			#if ((SGX_CGBUFFER_HEAP_BASE + SGX_CGBUFFER_HEAP_SIZE) >= SGX_GENERAL_HEAP_BASE)
				#error "sgxconfig.h: ERROR: SGX_CGBUFFER_HEAP overlaps SGX_GENERAL_HEAP_BASE"
			#endif
		#endif
	#endif
#endif

#if defined(SUPPORT_SGX_GENERAL_MAPPING_HEAP)
	#if ((SGX_GENERAL_MAPPING_HEAP_BASE + SGX_GENERAL_MAPPING_HEAP_SIZE) >= SGX_GENERAL_HEAP_BASE)
		#error "sgxconfig.h: ERROR: SGX_GENERAL_MAPPING_HEAP overlaps SGX_GENERAL_HEAP"
	#endif
#endif

#if defined(SUPPORT_HYBRID_PB)
	#if ((HYBRID_SHARED_PB_SIZE + 0x000001000) > SGX_3DPARAMETERS_HEAP_SIZE)
		#error "sgxconfig.h: ERROR: HYBRID_SHARED_PB_SIZE too large"
	#endif
#endif

#if defined(SUPPORT_MEMORY_TILING)
	#if ((SGX_GENERAL_HEAP_BASE + SGX_GENERAL_HEAP_SIZE) >= SGX_VPB_TILED_HEAP_BASE)
		#error "sgxconfig.h: ERROR: SGX_GENERAL_HEAP overlaps SGX_VPB_TILED_HEAP"
	#endif
	#if ((SGX_VPB_TILED_HEAP_BASE + SGX_VPB_TILED_HEAP_SIZE) >= SGX_SHARED_3DPARAMETERS_HEAP_BASE)
		#error "sgxconfig.h: ERROR: SGX_VPB_TILED_HEAP overlaps SGX_3DPARAMETERS_HEAP"
	#endif
#else
	#if ((SGX_GENERAL_HEAP_BASE + SGX_GENERAL_HEAP_SIZE) >= SGX_SHARED_3DPARAMETERS_HEAP_BASE)
		#error "sgxconfig.h: ERROR: SGX_GENERAL_HEAP overlaps SGX_3DPARAMETERS_HEAP"
	#endif
#endif

#if (((SGX_PERCONTEXT_3DPARAMETERS_HEAP_BASE + SGX_PERCONTEXT_3DPARAMETERS_HEAP_SIZE) >= SGX_TADATA_HEAP_BASE) && (SGX_PERCONTEXT_3DPARAMETERS_HEAP_SIZE > 0))
	#error "sgxconfig.h: ERROR: SGX_PERCONTEXT_3DPARAMETERS_HEAP_BASE overlaps SGX_TADATA_HEAP"
#endif

#if ((SGX_TADATA_HEAP_BASE + SGX_TADATA_HEAP_SIZE) >= SGX_SYNCINFO_HEAP_BASE)
	#error "sgxconfig.h: ERROR: SGX_TADATA_HEAP overlaps SGX_SYNCINFO_HEAP"
#endif

#if ((SGX_SYNCINFO_HEAP_BASE + SGX_SYNCINFO_HEAP_SIZE) >= SGX_PDSPIXEL_CODEDATA_HEAP_BASE)
	#error "sgxconfig.h: ERROR: SGX_SYNCINFO_HEAP overlaps SGX_PDSPIXEL_CODEDATA_HEAP"
#endif

#if ((SGX_PDSPIXEL_CODEDATA_HEAP_BASE + SGX_PDSPIXEL_CODEDATA_HEAP_SIZE) >= SGX_KERNEL_CODE_HEAP_BASE)
	#error "sgxconfig.h: ERROR: SGX_PDSPIXEL_CODEDATA_HEAP overlaps SGX_KERNEL_CODE_HEAP"
#endif

#if ((SGX_KERNEL_CODE_HEAP_BASE + SGX_KERNEL_CODE_HEAP_SIZE) >= SGX_PDSVERTEX_CODEDATA_HEAP_BASE)
	#error "sgxconfig.h: ERROR: SGX_KERNEL_CODE_HEAP overlaps SGX_PDSVERTEX_CODEDATA_HEAP"
#endif

#if ((SGX_PDSVERTEX_CODEDATA_HEAP_BASE + SGX_PDSVERTEX_CODEDATA_HEAP_SIZE) >= SGX_KERNEL_DATA_HEAP_BASE)
	#error "sgxconfig.h: ERROR: SGX_PDSVERTEX_CODEDATA_HEAP overlaps SGX_KERNEL_DATA_HEAP"
#endif

#if ((SGX_KERNEL_DATA_HEAP_BASE + SGX_KERNEL_DATA_HEAP_SIZE) >= SGX_PIXELSHADER_HEAP_BASE)
	#error "sgxconfig.h: ERROR: SGX_KERNEL_DATA_HEAP overlaps SGX_PIXELSHADER_HEAP"
#endif

#if ((SGX_PIXELSHADER_HEAP_BASE + SGX_PIXELSHADER_HEAP_SIZE) >= SGX_VERTEXSHADER_HEAP_BASE)
	#error "sgxconfig.h: ERROR: SGX_PIXELSHADER_HEAP overlaps SGX_VERTEXSHADER_HEAP"
#endif

#if ((SGX_VERTEXSHADER_HEAP_BASE + SGX_VERTEXSHADER_HEAP_SIZE) < SGX_VERTEXSHADER_HEAP_BASE)
	#error "sgxconfig.h: ERROR: SGX_VERTEXSHADER_HEAP_BASE size cause wraparound"
#endif

#endif 

