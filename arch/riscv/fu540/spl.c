// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2020 SiFive, Inc
 * Pragnesh Patel <pragnesh.patel@sifive.com>
 */

#include <common.h>
#include <dm.h>

int soc_spl_init(void)
{
	int ret;
	struct udevice *dev;

	/* PRCI init */
	ret = uclass_get_device(UCLASS_CLK, 0, &dev);
	if (ret) {
		debug("Clock init failed: %d\n", ret);
		return ret;
	}

	/* DDR init */
	ret = uclass_get_device(UCLASS_RAM, 0, &dev);
	if (ret) {
		debug("DRAM init failed: %d\n", ret);
		return ret;
	}

	return 0;
}
