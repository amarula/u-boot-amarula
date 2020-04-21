// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2016 Rockchip Electronics Co., Ltd
 */

#include <common.h>
#include <dm.h>
#include <asm/arch-rockchip/periph.h>
#include <power/regulator.h>
#include <spl_gpio.h>
#include <asm/io.h>
#include <asm/arch-rockchip/gpio.h>
#include <asm/arch-rockchip/grf_rk3399.h>

#ifndef CONFIG_SPL_BUILD
int board_early_init_f(void)
{
	struct udevice *regulator;
	int ret;

	ret = regulator_get_by_platname("vcc5v0_host", &regulator);
	if (ret) {
		debug("%s vcc5v0_host init fail! ret %d\n", __func__, ret);
		goto out;
	}

	ret = regulator_set_enable(regulator, true);
	if (ret)
		debug("%s vcc5v0-host-en set fail! ret %d\n", __func__, ret);
out:
	return 0;
}
#endif

#if defined(CONFIG_TPL_BUILD)

#define PMUGRF_BASE     0xff320000
#define GPIO0_BASE      0xff720000

int board_early_init_f(void)
{
	struct rockchip_gpio_regs * const gpio0 = (void *)GPIO0_BASE;
	struct rk3399_pmugrf_regs * const pmugrf = (void *)PMUGRF_BASE;

	/**
	 * 1. Glow yellow LED, termed as low power
	 * 2. Poll for on board power key press
	 * 3. Once 2 done, off yellow and glow red LED, termed as full power
	 * 4. Continue booting...
	 */
	spl_gpio_output(gpio0, GPIO(BANK_A, 2), 1);

	spl_gpio_set_pull(&pmugrf->gpio0_p, GPIO(BANK_A, 5), GPIO_PULL_NORMAL);
	while (readl(&gpio0->ext_port) & 0x20);

	spl_gpio_output(gpio0, GPIO(BANK_A, 2), 0);
	spl_gpio_output(gpio0, GPIO(BANK_B, 5), 1);

	return 0;
}
#endif

static void rk3399_force_pcie_power_on(void)
{
        ofnode node;
        struct gpio_desc pcie_pwr_gpio;

        printf("%s: trying to force pcie power on\n", __func__);

        node = ofnode_path("/config");
        if (!ofnode_valid(node)) {
                printf("%s: no /config node?\n", __func__);
                return;
        }

        if (gpio_request_by_name_nodev(node, "pcie-pwr-gpio", 0,
                                       &pcie_pwr_gpio, GPIOD_IS_OUT)) {
                printf("%s: could not find a /config/pcie-pwr-gpio\n", __func__);
                return;
        }

        dm_gpio_set_value(&pcie_pwr_gpio, 1);
}

int misc_init_r(void)
{
	 printf("Enable PCIE Power for ROCK960 board\n");
        __raw_writel(0xffff0001, (void __iomem *)0xff77e640);
        rk3399_force_pcie_power_on();

	return 0;
}
