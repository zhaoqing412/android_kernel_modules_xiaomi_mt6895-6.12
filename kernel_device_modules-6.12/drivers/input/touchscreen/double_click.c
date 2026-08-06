// SPDX-License-Identifier: GPL-2.0
/*
 * Touch double-click state provider.
 * Ported from the 5.10 xaga kernel (drivers/input/touchscreen/double_click.c).
 * The touch panel driver (novatek) toggles the state via tp_enable_doubleclick();
 * display panels gate on is_tp_doubleclick_enable() to skip ESD recovery while
 * the touch is in double-click (always-on) mode.
 */

#include <linux/double_click.h>

static bool doubleclick;

void tp_enable_doubleclick(bool state)
{
	doubleclick = state;
}
EXPORT_SYMBOL_GPL(tp_enable_doubleclick);

bool is_tp_doubleclick_enable(void)
{
	return doubleclick;
}
EXPORT_SYMBOL_GPL(is_tp_doubleclick_enable);
MODULE_LICENSE("GPL");
