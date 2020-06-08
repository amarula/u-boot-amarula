// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2020 Amarula Solutions(India)
 */

#include <common.h>
#include <spl_gpio.h>
#include <asm/arch-rockchip/gpio.h>

#define GPIO0_BASE		0xff720000

static void led_setup(void)
{
	struct rockchip_gpio_regs * const gpio0 = (void *)GPIO0_BASE;

	/* Turn on red LED, indicating full power mode */
	spl_gpio_output(gpio0, GPIO(BANK_B, 5), 1);
}

void rk_spl_board_init(void)
{
	led_setup();
}
