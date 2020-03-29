# SPDX-License-Identifier: GPL-2.0+
#
# Copyright (C) 2020 SiFive, Inc
# Pragnesh Patel <pragnesh.patel@sifive.com>

ifeq ($(CONFIG_SPL_BUILD),y)
obj-$(CONFIG_TARGET_SIFIVE_FU540) += spl.o
else
obj-y += cache.o
endif
