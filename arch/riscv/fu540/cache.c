// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2020 SiFive, Inc
 *
 * Authors:
 *   Pragnesh Patel <pragnesh.patel@sifive.com>
 */

#include <common.h>
#include <asm/io.h>
#include <linux/bitops.h>

#ifdef CONFIG_TARGET_SIFIVE_FU540
/* Register offsets */
#define CACHE_CONFIG	0x000
#define CACHE_ENABLE	0x008

#define MASK_NUM_WAYS	GENMASK(15, 8)
#define NUM_WAYS_SHIFT	8
#endif

DECLARE_GLOBAL_DATA_PTR;

int cache_enable_ways(void)
{
	const void *blob = gd->fdt_blob;
	int node = (-FDT_ERR_NOTFOUND);
	fdt_addr_t base;
	u32 config;
	u32 ways;

	volatile u32 *enable;

#ifdef CONFIG_TARGET_SIFIVE_FU540
	node = fdt_node_offset_by_compatible(blob, -1,
					     "sifive,fu540-c000-ccache");

	if (node < 0)
		return node;

	base = fdtdec_get_addr(blob, node, "reg");
	if (base == FDT_ADDR_T_NONE)
		return FDT_ADDR_T_NONE;

	config = readl((volatile u32 *)base + CACHE_CONFIG);
	ways = (config & MASK_NUM_WAYS) >> NUM_WAYS_SHIFT;

	enable = (volatile u32 *)(base + CACHE_ENABLE);

	/* memory barrier */
	mb();
	(*enable) = ways - 1;
	/* memory barrier */
	mb();
#endif /* CONFIG_TARGET_SIFIVE_FU540 */

	return 0;
}
