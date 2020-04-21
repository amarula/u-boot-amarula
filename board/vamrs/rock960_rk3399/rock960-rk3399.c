// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>
 */

#include <common.h>
#include <dm.h>
#include <asm/io.h>
#include <asm/gpio.h>

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
