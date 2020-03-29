# SPDX-License-Identifier: GPL-2.0+
#
# Copyright (C) 2020 SiFive, Inc
# Pragnesh Patel <pragnesh.patel@sifive.com>

ifeq ($(CONFIG_SPL_BUILD),y)
obj-$(CONFIG_TARGET_SIFIVE_FU540) += spl.o
else
# necessary to create built-in.o
obj- += __dummy__.o
endif
