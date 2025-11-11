/*
 * Exynos-SnapShot for Samsung's SoC's.
 *
 * Copyright (c) 2017 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef DEBUG_SNAPSHOT_TABLE_H
#define DEBUG_SNAPSHOT_TABLE_H

/************************************************************************
 * This definition is default settings.
 * We must use bootloader settings first.
*************************************************************************/

#define SZ_16				0x00000010
#define SZ_32				0x00000020
#define SZ_64				0x00000040
#define SZ_128				0x00000080
#define SZ_256				0x00000100
#define SZ_512				0x00000200

#define SZ_1K				0x00000400
#define SZ_2K				0x00000800
#define SZ_4K				0x00001000
#define SZ_8K				0x00002000
#define SZ_16K				0x00004000
#define SZ_28K				0x00007000
#define SZ_32K				0x00008000
#define SZ_64K				0x00010000
#define SZ_128K				0x00020000
#define SZ_256K				0x00040000
#define SZ_512K				0x00080000

#define SZ_1M				0x00100000

#define DSS_START_ADDR			0x92000000
#define DSS_HEADER_SIZE			SZ_28K
#define DSS_LOG_KEVENTS_SIZE		(8 * SZ_1M - SZ_28K)
#define DSS_LOG_KERNEL_SIZE		(2 * SZ_1M)
#define DSS_LOG_PLATFORM_SIZE		(4 * SZ_1M)
#define DSS_LOG_SFR_SIZE		(4 * SZ_1M)
#define DSS_LOG_ETM_SIZE		(0)
#define DSS_LOG_PSTORE_SIZE		SZ_32K

#define DSS_HEADER_OFFSET		0
#define DSS_LOG_KEVENTS_OFFSET		(DSS_HEADER_OFFSET + DSS_HEADER_SIZE)
#define DSS_LOG_KERNEL_OFFSET		(DSS_LOG_KEVENTS_OFFSET + DSS_LOG_KEVENTS_SIZE)
#define DSS_LOG_PLATFORM_OFFSET		(DSS_LOG_KERNEL_OFFSET + DSS_LOG_KERNEL_SIZE)
#define DSS_LOG_SFR_OFFSET		(DSS_LOG_PLATFORM_OFFSET + DSS_LOG_PLATFORM_SIZE)
#define DSS_LOG_ETM_OFFSET		(DSS_LOG_SFR_OFFSET + DSS_LOG_SFR_SIZE)
#define DSS_LOG_PSTORE_OFFSET		(DSS_LOG_ETM_OFFSET + DSS_LOG_ETM_SIZE)

#define DSS_HEADER_ADDR			(DSS_START_ADDR + DSS_HEADER_OFFSET)
#define DSS_LOG_KEVENTS_ADDR		(DSS_START_ADDR + DSS_LOG_KEVENTS_OFFSET)
#define DSS_LOG_KERNEL_ADDR		(DSS_START_ADDR + DSS_LOG_KERNEL_OFFSET)
#define DSS_LOG_PLATFORM_ADDR		(DSS_START_ADDR + DSS_LOG_PLATFORM_OFFSET)
#define DSS_LOG_SFR_ADDR		(DSS_START_ADDR + DSS_LOG_SFR_OFFSET)
#define DSS_LOG_ETM_ADDR		(DSS_START_ADDR + DSS_LOG_ETM_OFFSET)
#define DSS_LOG_PSTORE_ADDR		(DSS_START_ADDR + DSS_LOG_PSTORE_OFFSET)

#endif
