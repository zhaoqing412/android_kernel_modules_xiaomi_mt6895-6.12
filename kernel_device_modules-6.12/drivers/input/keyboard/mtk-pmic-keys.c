// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 MediaTek, Inc.
 *
 * Author: Chen Zhong <chen.zhong@mediatek.com>
 */

#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeup.h>
#include <linux/regmap.h>
#include <linux/device.h>
#include <linux/mfd/mt6323/registers.h>
#include <linux/mfd/mt6359p/registers.h>
#include <linux/mfd/mt6363/registers.h>
#include <linux/mfd/mt6397/registers.h>
#include <linux/mfd/mt6358/core.h>
#include <linux/mfd/mt6363/core.h>
#include <linux/mfd/mt6366/core.h>
#include <linux/mfd/mt6397/core.h>
#include <linux/mfd/mt6357/registers.h>
#include <linux/mfd/mt6357/core.h>
#include <linux/mfd/mt6661/registers.h>
#include <linux/mfd/mt6661/core.h>
#include <linux/pinctrl/consumer.h>
#include <linux/atomic.h>
#include <soc/oplus/boot/oplus_project.h>
//#ifdef OPLUS_BUG_STABILITY
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/of_gpio.h>
#include <linux/seq_file.h>
//#endif /*OPLUS_BUG_STABILITY*/

/* 6358 pmic define */
#define MT6358_TOPSTATUS			(0x28)
#define MT6358_PSC_TOP_INT_CON0			(0x910)
#define MT6358_TOP_RST_MISC			(0x14c)
#define MT6358_PWRKEY_DEB_MASK			1
#define MT6358_HOMEKEY_DEB_MASK			3
#define MT6358_RG_INT_EN_HOMEKEY_MASK           1
#define MT6358_RG_INT_EN_PWRKEY_MASK            0
#define MT6358_PWRKEY_RST_SHIFT                 9
#define MT6358_HOMEKEY_RST_SHIFT                8
#define MT6358_RST_DU_SHIFT                     12
#define MT6366_TOPSTATUS			(0x28)
#define MT6366_PSC_TOP_INT_CON0			(0x910)
#define MT6366_TOP_RST_MISC			(0x14c)
#define MT6366_PWRKEY_DEB_MASK			1
#define MT6366_HOMEKEY_DEB_MASK			3
#define MT6366_RG_INT_EN_HOMEKEY_MASK           1
#define MT6366_RG_INT_EN_PWRKEY_MASK            0
#define MT6366_PWRKEY_RST_SHIFT                 9
#define MT6366_HOMEKEY_RST_SHIFT                8
#define MT6366_RST_DU_SHIFT                     12
#define MTK_PMIC_PWRKEY_INDEX			0
#define MTK_PMIC_HOMEKEY_INDEX			1
#define MTK_PMIC_HOMEKEY2_INDEX			2
#define MTK_PMIC_MAX_KEY_COUNT			3
#define MT6397_PWRKEY_RST_SHIFT			6
#define MT6397_HOMEKEY_RST_SHIFT		5
#define MT6397_RST_DU_SHIFT			8
#define MT6359_PWRKEY_RST_SHIFT			9
#define MT6359_HOMEKEY_RST_SHIFT		8
#define MT6359_RST_DU_SHIFT			12
#define MT6363_PWRKEY_RST_SHIFT			2
#define MT6363_HOMEKEY_RST_SHIFT		4
#define MT6363_RST_DU_SHIFT			6
#define MT6661_PWRKEY_RST_SHIFT			2
#define MT6661_HOMEKEY_RST_SHIFT		4
#define MT6661_RST_DU_SHIFT			6
#define PWRKEY_RST_EN				1
#define HOMEKEY_RST_EN				1
#define RST_DU_MASK				3
#define RST_MODE_MASK				3
#define RST_PWRKEY_MODE				0
#define RST_PWRKEY_HOME_MODE			1
#define RST_PWRKEY_HOME2_MODE			2
#define RST_PWRKEY_HOME_HOME2_MODE		3
#define INVALID_VALUE				0
#define MT6357_PWRKEY_RST_SHIFT			9
#define MT6357_HOMEKEY_RST_SHIFT		8
#define MT6357_RST_DU_SHIFT			12
#define INT_MASK_PWRKEY				0x09

struct mtk_pmic_keys_regs {
	u32 deb_reg;
	u32 deb_mask;
	u32 intsel_reg;
	u32 intsel_mask;
};

#define MTK_PMIC_KEYS_REGS(_deb_reg, _deb_mask,		\
	_intsel_reg, _intsel_mask)			\
{							\
	.deb_reg		= _deb_reg,		\
	.deb_mask		= _deb_mask,		\
	.intsel_reg		= _intsel_reg,		\
	.intsel_mask		= _intsel_mask,		\
}

struct mtk_pmic_regs {
	const struct mtk_pmic_keys_regs keys_regs[MTK_PMIC_MAX_KEY_COUNT];
	bool release_irq;
	u32 pmic_rst_reg;
	u32 pmic_rst_para_reg;
	u32 pwrkey_rst_shift;
	u32 homekey_rst_shift;
	u32 rst_du_shift;
};

static const struct mtk_pmic_regs mt6397_regs = {
	.keys_regs[MTK_PMIC_PWRKEY_INDEX] =
		MTK_PMIC_KEYS_REGS(MT6397_CHRSTATUS,
		0x8, MT6397_INT_RSV, 0x10),
	.keys_regs[MTK_PMIC_HOMEKEY_INDEX] =
		MTK_PMIC_KEYS_REGS(MT6397_OCSTATUS2,
		0x10, MT6397_INT_RSV, 0x8),
	.release_irq = false,
	.pmic_rst_reg = MT6397_TOP_RST_MISC,
	.pwrkey_rst_shift = MT6397_PWRKEY_RST_SHIFT,
	.homekey_rst_shift = MT6397_HOMEKEY_RST_SHIFT,
	.rst_du_shift = MT6397_RST_DU_SHIFT,
};

static const struct mtk_pmic_regs mt6323_regs = {
	.keys_regs[MTK_PMIC_PWRKEY_INDEX] =
		MTK_PMIC_KEYS_REGS(MT6323_CHRSTATUS,
		0x2, MT6323_INT_MISC_CON, 0x10),
	.keys_regs[MTK_PMIC_HOMEKEY_INDEX] =
		MTK_PMIC_KEYS_REGS(MT6323_CHRSTATUS,
		0x4, MT6323_INT_MISC_CON, 0x8),
	.release_irq = false,
	.pmic_rst_reg = MT6323_TOP_RST_MISC,
	.pwrkey_rst_shift = MT6397_PWRKEY_RST_SHIFT,
	.homekey_rst_shift = MT6397_HOMEKEY_RST_SHIFT,
	.rst_du_shift = MT6397_RST_DU_SHIFT,
};

static const struct mtk_pmic_regs mt6359p_regs = {
	.keys_regs[MTK_PMIC_PWRKEY_INDEX] =
		MTK_PMIC_KEYS_REGS(INVALID_VALUE,
		INVALID_VALUE, MT6359P_PSC_TOP_INT_CON0, 0x1),
	.keys_regs[MTK_PMIC_HOMEKEY_INDEX] =
		MTK_PMIC_KEYS_REGS(INVALID_VALUE,
		INVALID_VALUE, MT6359P_PSC_TOP_INT_CON0, 0x2),
	.release_irq = true,
	.pmic_rst_reg = MT6359P_TOP_RST_MISC,
	.pwrkey_rst_shift = MT6359_PWRKEY_RST_SHIFT,
	.homekey_rst_shift = MT6359_HOMEKEY_RST_SHIFT,
	.rst_du_shift = MT6359_RST_DU_SHIFT,
};

static const struct mtk_pmic_regs mt6363_regs = {
	.keys_regs[MTK_PMIC_PWRKEY_INDEX] =
		MTK_PMIC_KEYS_REGS(MT6363_TOPSTATUS,
		0x1, MT6363_PSC_TOP_INT_CON0, 0x0),
	.keys_regs[MTK_PMIC_HOMEKEY_INDEX] =
		MTK_PMIC_KEYS_REGS(MT6363_TOPSTATUS,
		0x3, MT6363_PSC_TOP_INT_CON0, 0x12),
	.keys_regs[MTK_PMIC_HOMEKEY2_INDEX] =
		MTK_PMIC_KEYS_REGS(MT6363_TOPSTATUS,
		0x4, MT6363_PSC_TOP_INT_CON0, 0x24),
	.release_irq = true,
	.pmic_rst_reg = MT6363_STRUP_CON11,
	.pmic_rst_para_reg = MT6363_STRUP_CON12,
	.pwrkey_rst_shift = MT6363_PWRKEY_RST_SHIFT,
	.homekey_rst_shift = MT6363_HOMEKEY_RST_SHIFT,
	.rst_du_shift = MT6363_RST_DU_SHIFT,
};

static const struct mtk_pmic_regs mt6366_regs = {
	.keys_regs[MTK_PMIC_PWRKEY_INDEX] =
		MTK_PMIC_KEYS_REGS(MT6366_TOPSTATUS,
		MT6366_PWRKEY_DEB_MASK,
		MT6366_PSC_TOP_INT_CON0,
		MT6366_RG_INT_EN_PWRKEY_MASK),
	.keys_regs[MTK_PMIC_HOMEKEY_INDEX] =
		MTK_PMIC_KEYS_REGS(MT6366_TOPSTATUS,
		MT6366_HOMEKEY_DEB_MASK,
		MT6366_PSC_TOP_INT_CON0,
		MT6366_RG_INT_EN_HOMEKEY_MASK),
	.release_irq = true,
	.pmic_rst_reg = MT6366_TOP_RST_MISC,
	.pwrkey_rst_shift = MT6366_PWRKEY_RST_SHIFT,
	.homekey_rst_shift = MT6366_HOMEKEY_RST_SHIFT,
	.rst_du_shift = MT6366_RST_DU_SHIFT,
};

static const struct mtk_pmic_regs mt6357_regs = {
	.keys_regs[MTK_PMIC_PWRKEY_INDEX] =
		MTK_PMIC_KEYS_REGS(MT6357_TOPSTATUS,
		MT6357_PWRKEY_DEB_MASK,
		MT6357_PSC_TOP_INT_CON0,
		MT6357_RG_INT_EN_PWRKEY_MASK),
	.keys_regs[MTK_PMIC_HOMEKEY_INDEX] =
		MTK_PMIC_KEYS_REGS(MT6357_TOPSTATUS,
		MT6357_HOMEKEY_DEB_MASK,
		MT6357_PSC_TOP_INT_CON0,
		MT6357_RG_INT_EN_HOMEKEY_MASK),
	.release_irq = true,
	.pmic_rst_reg = MT6357_TOP_RST_MISC,
	.pwrkey_rst_shift = MT6357_PWRKEY_RST_SHIFT,
	.homekey_rst_shift = MT6357_HOMEKEY_RST_SHIFT,
	.rst_du_shift = MT6357_RST_DU_SHIFT,
};

static const struct mtk_pmic_regs mt6358_regs = {
	.keys_regs[MTK_PMIC_PWRKEY_INDEX] =
		MTK_PMIC_KEYS_REGS(MT6358_TOPSTATUS,
		MT6358_PWRKEY_DEB_MASK,
		MT6358_PSC_TOP_INT_CON0,
		MT6358_RG_INT_EN_PWRKEY_MASK),
	.keys_regs[MTK_PMIC_HOMEKEY_INDEX] =
		MTK_PMIC_KEYS_REGS(MT6358_TOPSTATUS,
		MT6358_HOMEKEY_DEB_MASK,
		MT6358_PSC_TOP_INT_CON0,
		MT6358_RG_INT_EN_HOMEKEY_MASK),
	.release_irq = true,
	.pmic_rst_reg = MT6358_TOP_RST_MISC,
	.pwrkey_rst_shift = MT6358_PWRKEY_RST_SHIFT,
	.homekey_rst_shift = MT6358_HOMEKEY_RST_SHIFT,
	.rst_du_shift = MT6358_RST_DU_SHIFT,
};

static const struct mtk_pmic_regs mt6661_regs = {
	.keys_regs[MTK_PMIC_PWRKEY_INDEX] =
		MTK_PMIC_KEYS_REGS(MT6661_TOPSTATUS,
		0x1, MT6661_PSC_TOP_INT_CON0, 0x09),
	.keys_regs[MTK_PMIC_HOMEKEY_INDEX] =
		MTK_PMIC_KEYS_REGS(MT6661_TOPSTATUS,
		0x3, MT6661_PSC_TOP_INT_CON0, 0x12),
	.keys_regs[MTK_PMIC_HOMEKEY2_INDEX] =
		MTK_PMIC_KEYS_REGS(MT6661_TOPSTATUS,
		0x4, MT6661_PSC_TOP_INT_CON0, 0x24),
	.release_irq = true,
	.pmic_rst_reg = MT6661_STRUP_CON11,
	.pmic_rst_para_reg = MT6661_STRUP_CON12,
	.pwrkey_rst_shift = MT6661_PWRKEY_RST_SHIFT,
	.homekey_rst_shift = MT6661_HOMEKEY_RST_SHIFT,
	.rst_du_shift = MT6661_RST_DU_SHIFT,
};

struct mtk_pmic_keys_info {
	struct mtk_pmic_keys *keys;
	const struct mtk_pmic_keys_regs *regs;
	unsigned int keycode;
	int irq;
	int release_irq_num;
	struct wakeup_source *suspend_lock;
};

struct mtk_pmic_keys {
	struct input_dev *input_dev;
	struct device *dev;
	struct regmap *regmap;
	struct mtk_pmic_keys_info keys[MTK_PMIC_MAX_KEY_COUNT];
	struct pinctrl *pinctrl;
	struct pinctrl_state *kpcol0_pins_mode;
};

enum mtk_pmic_keys_lp_mode {
	LP_DISABLE,
	LP_ONEKEY,
	LP_TWOKEY_HOMEKEY,
	LP_TWOKEY_HOMEKEY2,
};

static struct platform_device *ktf_pmic_pdev;
static struct mtk_pmic_keys *ktf_pmic_key;
static atomic_t last_key_status = ATOMIC_INIT(0);
static bool need_reset_home;

//#ifdef OPLUS_BUG_STABILITY
/* for AEE manual dump */
#define AEE_VOLUMEUP_BIT	0
#define AEE_VOLUMEDOWN_BIT	1
#define AEE_DELAY_TIME		15
#define VOLUMEDOWN_PRESSED	1

unsigned long vol_key_password = 0;
unsigned long start_timer_last = 0;
u16 TPLGPASSWORD = 3640;

static struct hrtimer aee_timer;
static unsigned long aee_pressed_keys;
static bool aee_timer_started;
int aee_kpd_enable = 0;
EXPORT_SYMBOL(aee_kpd_enable);

static struct hrtimer voldown_timer;
static int volume_down_gpio = -ENOENT;
static bool voldown_pressed = false;
static bool voldown_debounce = false;
static int volume_down_debounce_ms = 0;

void kpd_aee_handler(u32 keycode, u16 pressed);
EXPORT_SYMBOL(kpd_aee_handler);

static inline void kpd_update_aee_state(void);

static inline void kpd_update_aee_state(void)
{
	if (aee_pressed_keys == ((1 << AEE_VOLUMEUP_BIT) | (1 << AEE_VOLUMEDOWN_BIT))) {
		/* if volumeup and volumedown was pressed the same time then start the time of ten seconds */
		aee_timer_started = true;
		if (!hrtimer_active(&aee_timer)) {
			hrtimer_start(&aee_timer, ktime_set(AEE_DELAY_TIME, 0), HRTIMER_MODE_REL);
			pr_info("aee_timer started\n");
		}
	} else {
		/*
		 * hrtimer_cancel - cancel a timer and wait for the handler to finish.
		 * Returns:
		 * 0 when the timer was not active.
		 * 1 when the timer was active.
		 */
		if (aee_timer_started) {
			if (hrtimer_cancel(&aee_timer))
				pr_info("try to cancel hrtimer\n");

			aee_timer_started = false;
			pr_info("aee_timer canceled\n");
		}
	}
}
void kpd_aee_handler(u32 keycode, u16 pressed)
{
	if (pressed) {
		if (keycode == KEY_VOLUMEUP)
			__set_bit(AEE_VOLUMEUP_BIT, &aee_pressed_keys);
		else if (keycode == KEY_VOLUMEDOWN)
			__set_bit(AEE_VOLUMEDOWN_BIT, &aee_pressed_keys);
		else
			return;
		kpd_update_aee_state();
	} else {
		if (keycode == KEY_VOLUMEUP)
			__clear_bit(AEE_VOLUMEUP_BIT, &aee_pressed_keys);
		else if (keycode == KEY_VOLUMEDOWN)
			__clear_bit(AEE_VOLUMEDOWN_BIT, &aee_pressed_keys);
		else
			return;
		kpd_update_aee_state();
	}
}

static enum hrtimer_restart aee_timer_func(struct hrtimer *timer)
{
	/* kpd_info("kpd: vol up+vol down AEE manual dump!\n"); */
	if (aee_kpd_enable && aee_timer_started) {
		pr_err("%s call bug for aee manual dump.", __func__);
		BUG();
	}

	return HRTIMER_NORESTART;
}
//#endif /*OPLUS_BUG_STABILITY*/

static void mtk_pmic_keys_lp_reset_setup(struct mtk_pmic_keys *keys,
		const struct mtk_pmic_regs *pmic_regs)
{
	int ret;
	u32 long_press_mode, long_press_debounce;
	u32 pmic_rst_reg = pmic_regs->pmic_rst_reg;
	u32 pmic_rst_para_reg = pmic_regs->pmic_rst_para_reg;
	u32 pwrkey_rst_shift =
		PWRKEY_RST_EN << pmic_regs->pwrkey_rst_shift;
	u32 homekey_rst_shift =
		RST_MODE_MASK << pmic_regs->homekey_rst_shift;

	if (pmic_rst_para_reg == INVALID_VALUE) {
		pmic_rst_para_reg = pmic_rst_reg;
		homekey_rst_shift = HOMEKEY_RST_EN << pmic_regs->homekey_rst_shift;
	}

	ret = of_property_read_u32(keys->dev->of_node,
		"power-off-time-sec", &long_press_debounce);
	if (ret)
		long_press_debounce = 0;

	ret = regmap_update_bits(keys->regmap, pmic_rst_para_reg,
			   RST_DU_MASK << pmic_regs->rst_du_shift,
			   long_press_debounce << pmic_regs->rst_du_shift);
	if (ret < 0) {
		dev_dbg(keys->dev,
			"regmap_update_bits fail: %d\n", ret);
	}

	ret = of_property_read_u32(keys->dev->of_node,
		"mediatek,long-press-mode", &long_press_mode);
	if (ret)
		long_press_mode = LP_DISABLE;

	switch (long_press_mode) {
	case LP_ONEKEY:
		ret = regmap_update_bits(keys->regmap, pmic_rst_reg,
				   pwrkey_rst_shift,
				   pwrkey_rst_shift);
		if (ret < 0) {
			dev_dbg(keys->dev,
				"regmap_update_bits fail LP_ONEKEY: %d\n", ret);
		}
		if (need_reset_home == true) {
			ret = regmap_update_bits(keys->regmap, pmic_rst_reg,
				   pwrkey_rst_shift << RST_PWRKEY_HOME_MODE,
				   0);
			if (ret < 0) {
				dev_dbg(keys->dev,
					"regmap_update_bits fail LP_ONEKEY: %d\n", ret);
			}
			ret = regmap_update_bits(keys->regmap, pmic_rst_reg,
				   pwrkey_rst_shift << RST_PWRKEY_HOME2_MODE,
				   0);
			if (ret < 0) {
				dev_dbg(keys->dev,
					"regmap_update_bits fail LP_ONEKEY: %d\n", ret);
			}
		}
		ret = regmap_update_bits(keys->regmap, pmic_rst_para_reg,
				   homekey_rst_shift,
				   RST_PWRKEY_MODE);
		if (ret < 0) {
			dev_dbg(keys->dev,
				"regmap_update_bits fail LP_ONEKEY: %d\n", ret);
		}
		break;
	case LP_TWOKEY_HOMEKEY:
		ret = regmap_update_bits(keys->regmap, pmic_rst_reg,
				   pwrkey_rst_shift,
				   pwrkey_rst_shift);
		if (ret < 0) {
			dev_dbg(keys->dev,
				"regmap_update_bits fail LP_TWOKEY_HOMEKEY: %d\n", ret);
		}
		if (need_reset_home == true) {
			ret = regmap_update_bits(keys->regmap, pmic_rst_reg,
				   pwrkey_rst_shift << RST_PWRKEY_HOME_MODE,
				   pwrkey_rst_shift << RST_PWRKEY_HOME_MODE);
			if (ret < 0) {
				dev_dbg(keys->dev,
					"regmap_update_bits fail LP_TWOKEY_HOMEKEY: %d\n", ret);
			}
			ret = regmap_update_bits(keys->regmap, pmic_rst_reg,
				   pwrkey_rst_shift << RST_PWRKEY_HOME2_MODE,
				   0);
			if (ret < 0) {
				dev_dbg(keys->dev,
					"regmap_update_bits fail LP_TWOKEY_HOMEKEY: %d\n", ret);
			}
		}
		ret = regmap_update_bits(keys->regmap, pmic_rst_para_reg,
				   homekey_rst_shift,
				   RST_PWRKEY_HOME_MODE << pmic_regs->homekey_rst_shift);
		if (ret < 0) {
			dev_dbg(keys->dev,
				"regmap_update_bits fail LP_TWOKEY_HOMEKEY: %d\n", ret);
		}
		break;
	case LP_TWOKEY_HOMEKEY2:
		ret = regmap_update_bits(keys->regmap, pmic_rst_reg,
				   pwrkey_rst_shift,
				   pwrkey_rst_shift);
		if (ret < 0) {
			dev_dbg(keys->dev,
				"regmap_update_bits fail LP_TWOKEY_HOMEKEY2: %d\n", ret);
		}
		if (need_reset_home == true) {
			ret = regmap_update_bits(keys->regmap, pmic_rst_reg,
				   pwrkey_rst_shift << RST_PWRKEY_HOME_MODE,
				   0);
			if (ret < 0) {
				dev_dbg(keys->dev,
					"regmap_update_bits fail LP_TWOKEY_HOMEKEY2: %d\n", ret);
			}
			ret = regmap_update_bits(keys->regmap, pmic_rst_reg,
				   pwrkey_rst_shift << RST_PWRKEY_HOME2_MODE,
				   pwrkey_rst_shift << RST_PWRKEY_HOME2_MODE);
			if (ret < 0) {
				dev_dbg(keys->dev,
					"regmap_update_bits fail LP_TWOKEY_HOMEKEY2: %d\n", ret);
			}
		}
		ret = regmap_update_bits(keys->regmap, pmic_rst_para_reg,
				   homekey_rst_shift,
				   RST_PWRKEY_HOME2_MODE << pmic_regs->homekey_rst_shift);
		if (ret < 0) {
			dev_dbg(keys->dev,
				"regmap_update_bits fail LP_TWOKEY_HOMEKEY2: %d\n", ret);
		}
		break;
	case LP_DISABLE:
		ret = regmap_update_bits(keys->regmap, pmic_rst_reg,
				   pwrkey_rst_shift,
				   0);
		if (ret < 0) {
			dev_dbg(keys->dev,
				"regmap_update_bits fail LP_DISABLE: %d\n", ret);
		}
		if (need_reset_home == true) {
			ret = regmap_update_bits(keys->regmap, pmic_rst_reg,
				   pwrkey_rst_shift << RST_PWRKEY_HOME_MODE,
				   0);
			if (ret < 0) {
				dev_dbg(keys->dev,
					"regmap_update_bits fail LP_DISABLE: %d\n", ret);
			}
			ret = regmap_update_bits(keys->regmap, pmic_rst_reg,
				   pwrkey_rst_shift << RST_PWRKEY_HOME2_MODE,
				   0);
			if (ret < 0) {
				dev_dbg(keys->dev,
					"regmap_update_bits fail LP_DISABLE: %d\n", ret);
			}
		}
		ret = regmap_update_bits(keys->regmap, pmic_rst_para_reg,
				   homekey_rst_shift,
				   RST_PWRKEY_HOME_HOME2_MODE << pmic_regs->homekey_rst_shift);
		if (ret < 0) {
			dev_dbg(keys->dev,
				"regmap_update_bits fail LP_DISABLE: %d\n", ret);
		}
		break;
	default:
		break;
	}
}

static irqreturn_t mtk_pmic_keys_release_irq_handler_thread(
				int irq, void *data)
{
	struct mtk_pmic_keys_info *info = data;

	input_report_key(info->keys->input_dev, info->keycode, 0);
	input_sync(info->keys->input_dev);
	if (info->suspend_lock)
		__pm_relax(info->suspend_lock);
	dev_dbg(info->keys->dev, "release key =%d using PMIC\n",
			info->keycode);
//#ifdef OPLUS_BUG_STABILITY
	if (aee_kpd_enable && info->keycode == KEY_VOLUMEUP) {
		pr_err("pmic volup key triggered, pressed is %u\n", 0);
		kpd_aee_handler(KEY_VOLUMEUP, 0);
	}
	if (aee_kpd_enable && info->keycode == KEY_VOLUMEDOWN) {
		pr_err("pmic voldown key triggered, pressed is %u\n", 0);
		kpd_aee_handler(KEY_VOLUMEDOWN, 0);
	}
//#endif /*OPLUS_BUG_STABILITY*/

	return IRQ_HANDLED;
}

static irqreturn_t mtk_pmic_keys_irq_handler_thread(int irq, void *data)
{
	struct mtk_pmic_keys_info *info = data;
	u32 key_deb, pressed;
	int ret;

	if (info->release_irq_num > 0) {
		pressed = 1;
	} else {
		ret = regmap_read(info->keys->regmap, info->regs->deb_reg, &key_deb);
		if (ret < 0) {
			dev_dbg(info->keys->dev,
				"regmap_read fail: %d\n", ret);
		}
		key_deb &= info->regs->deb_mask;
		pressed = !key_deb;
	}

	input_report_key(info->keys->input_dev, info->keycode, pressed);
	input_sync(info->keys->input_dev);

//#ifndef OPLUS_FEATURE_TP_BASIC
//Qicai.gu 2025/4/18 add for powerkey lost up event
//	if (pressed && info->suspend_lock)
//		__pm_stay_awake(info->suspend_lock);
//#else
	if (pressed && info->suspend_lock)
		__pm_wakeup_event(info->suspend_lock, msecs_to_jiffies(200000));//200s
//#endif
	else if (info->suspend_lock)
		__pm_relax(info->suspend_lock);
	dev_dbg(info->keys->dev, "(%s) key =%d using PMIC\n",
		 pressed ? "pressed" : "released", info->keycode);

	//#ifdef OPLUS_BUG_STABILITY
	if (aee_kpd_enable && info->keycode == KEY_VOLUMEUP) {
		pr_err("pmic volup key triggered, pressed is %u\n", pressed);
		kpd_aee_handler(KEY_VOLUMEUP, pressed);
	}
	if (aee_kpd_enable && info->keycode == KEY_VOLUMEDOWN) {
		pr_err("pmic voldown key triggered, pressed is %u\n", pressed);
		kpd_aee_handler(KEY_VOLUMEDOWN, pressed);
	}
	//#endif /*OPLUS_BUG_STABILITY*/

	return IRQ_HANDLED;
}

static enum hrtimer_restart voldown_timer_func(struct hrtimer *timer)
{
	int gpio_state = gpio_get_value(volume_down_gpio);

	if (gpio_state == 0) {
		if (!voldown_pressed) {
			voldown_pressed = true;
			pr_err("%s: pmic voldown key pressed\n", __func__);
			kpd_aee_handler(KEY_VOLUMEDOWN, 1);
		}
	}
	voldown_debounce = false;
	return HRTIMER_NORESTART;
}

static irqreturn_t volume_down_irq_handler(int irq, void *dev_id)
{
	int gpio_state = gpio_get_value(volume_down_gpio);

	if (gpio_state == 0) {
		if (!voldown_pressed && !voldown_debounce) {
			voldown_debounce = true;
			hrtimer_start(&voldown_timer, ms_to_ktime(volume_down_debounce_ms), HRTIMER_MODE_REL);
		}
	} else {
		if (voldown_debounce) {
			voldown_debounce = false;
			hrtimer_cancel(&voldown_timer);
		}

		if (voldown_pressed) {
			voldown_pressed = false;
			pr_err("%s: pmic voldown key release\n", __func__);
			kpd_aee_handler(KEY_VOLUMEDOWN, 0);
		}
	}
	return IRQ_HANDLED;
}

/* val: 0, disable powerkey irq; 1, enable powerkey irq */
static int powerkey_irq_control(int val)
{
	int ret;

	struct mtk_pmic_keys_info *info = &(ktf_pmic_key->keys[MTK_PMIC_PWRKEY_INDEX]);

	dev_dbg(info->keys->dev, "(%s) last mode\n", val ? "exit" : "enter");
	if (val == 1)
		ret = regmap_update_bits(info->keys->regmap, info->regs->intsel_reg,
				INT_MASK_PWRKEY, INT_MASK_PWRKEY);
	else
		ret = regmap_update_bits(info->keys->regmap, info->regs->intsel_reg,
				INT_MASK_PWRKEY, 0);
	if (ret < 0)
		dev_info(info->keys->dev, "regmap update PMIC failed %d\n", ret);

	return ret;
}

/* mode:true, last mode disable powerkey irq; mode:false, normal mode enable powerkey irq */
int last_key_set(bool mode)
{
	int ret;

	ret = powerkey_irq_control(mode ? 0 : 1);
	if (!ret)
		atomic_set(&last_key_status, mode ? 1 : 0);

	return ret;
}
EXPORT_SYMBOL(last_key_set);

int last_key_get(bool mode)
{
	return atomic_read(&last_key_status);
}
EXPORT_SYMBOL(last_key_get);

/* powerkey irq show */
static ssize_t powerkey_irq_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	unsigned int val;
	int err;

	struct mtk_pmic_keys_info *info = &(ktf_pmic_key->keys[MTK_PMIC_PWRKEY_INDEX]);

	err = regmap_read(info->keys->regmap, info->regs->intsel_reg, &val);
	if (err < 0) {
		dev_info(info->keys->dev, "Failed to read powerkey IRQ status\n");
		return err;
	}

	val = ((val & INT_MASK_PWRKEY) == INT_MASK_PWRKEY) ? 1 : 0;

	return scnprintf(buf, PAGE_SIZE, "%u\n", val);
}

/* powerkey irq store */
static ssize_t powerkey_irq_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	int value, ret;

	struct mtk_pmic_keys_info *info = &(ktf_pmic_key->keys[MTK_PMIC_PWRKEY_INDEX]);

	ret = kstrtoint(buf, 10, &value);
	if (ret)
		return ret;

	if (value != 0 && value != 1)
		return -EINVAL;

	ret = powerkey_irq_control(value);
	if (ret < 0) {
		dev_info(info->keys->dev, "Failed to control powerkey IRQ, value = %d\n", value);
		return ret;
	}

	return count;
}

static DEVICE_ATTR(powerkey_irq, 0660, powerkey_irq_show, powerkey_irq_store);

void gpio_volume_down_key_init(struct platform_device *pdev)
{
	int volume_down_irq = -ENOENT;
	struct device_node *np;
	int ret;

	/* get volume_down_key node */
	np = of_find_node_by_name(NULL, "volume_down_key");
	if (!np) {
		pr_err("%s: not find volume_down_key node\n", __func__);
		return;
	}

	/* get volume-down-button node */
	np = of_get_child_by_name(np, "volume-down-button");
	if (!np) {
		pr_err("%s: not find volume-down-button node\n", __func__);
		return;
	}

	/* get gpio */
	volume_down_gpio = of_get_named_gpio(np, "gpios", 0);
	if (volume_down_gpio < 0) {
		pr_err("get gpio failed: %d\n", volume_down_gpio);
		return;
	}

	ret = of_property_read_u32(np, "debounce-interval", &volume_down_debounce_ms);
	if (ret) {
		pr_info("%s: debounce-interval not found, use default value\n", __func__);
		volume_down_debounce_ms = 32;
	} else {
		pr_err("%s: get debounce time is %d\n", __func__, volume_down_debounce_ms);
	}

	ret = gpio_direction_input(volume_down_gpio);
	if (ret) {
		pr_err("%s: set gpio direction input failed, ret is %d\n", __func__, ret);
		return;
	}

	volume_down_irq = gpio_to_irq(volume_down_gpio);
	if (volume_down_irq < 0) {
		pr_err("get gpio irq failed: %d\n", volume_down_irq);
		return;
	}

	ret = devm_request_any_context_irq(&pdev->dev, volume_down_irq,
		volume_down_irq_handler,
		IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_SHARED,
		"volume_down_key", &pdev->dev);
	if (ret) {
		pr_err("request gpio irq failed, ret is %d\n", ret);
		return;
	}
	pr_err("%s: init success\n", __func__);
}

static int mtk_pmic_key_setup(struct mtk_pmic_keys *keys,
		struct mtk_pmic_keys_info *info)
{
	int ret;

	info->keys = keys;

	ret = regmap_update_bits(keys->regmap, info->regs->intsel_reg,
				 info->regs->intsel_mask,
				 info->regs->intsel_mask);
	if (ret < 0)
		return ret;

	ret = devm_request_threaded_irq(keys->dev, info->irq, NULL,
					mtk_pmic_keys_irq_handler_thread,
					IRQF_ONESHOT | IRQF_TRIGGER_HIGH,
					"mtk-pmic-keys", info);
	if (ret) {
		dev_err(keys->dev, "Failed to request IRQ: %d: %d\n",
			info->irq, ret);
		return ret;
	}
	if (info->release_irq_num > 0) {
		ret = devm_request_threaded_irq(keys->dev,
				info->release_irq_num,
				NULL, mtk_pmic_keys_release_irq_handler_thread,
				IRQF_ONESHOT | IRQF_TRIGGER_HIGH,
				"mtk-pmic-keys", info);
		if (ret) {
			dev_dbg(keys->dev, "Failed to request IRQ: %d: %d\n",
				info->release_irq_num, ret);
			return ret;
		}
	}

	input_set_capability(keys->input_dev, EV_KEY, info->keycode);

	return 0;
}

static int keypad_pinctrl_init(struct mtk_pmic_keys *keys)
{
	int ret = 0;

	keys->pinctrl = devm_pinctrl_get(keys->dev);
	if (IS_ERR(keys->pinctrl)) {
		dev_dbg(keys->dev, "Failed to get keypad pinctrl handler");
		return PTR_ERR(keys->pinctrl);
	}

	keys->kpcol0_pins_mode = pinctrl_lookup_state(keys->pinctrl, "kpcol0_mode");
	if (!IS_ERR(keys->kpcol0_pins_mode)) {
		ret = pinctrl_select_state(keys->pinctrl, keys->kpcol0_pins_mode);
		if (ret) {
			dev_dbg(keys->dev, "failed to switch kpcol0 to gpio mode, ret: %d\n", ret);
			return ret;
		}
	} else {
		dev_dbg(keys->dev, "failed to get pinctrl state: %s\n", "kpcol0_mode");
		return PTR_ERR(keys->kpcol0_pins_mode);
	}

	return ret;
}

//#ifdef OPLUS_BUG_STABILITY
static int aee_kpd_enable_show(struct seq_file *s, void *v)
{
	seq_printf(s, "%d\n", aee_kpd_enable);
	return 0;
}

static int aee_kpd_enable_open(struct inode *inode, struct file *file)
{
	return single_open(file, aee_kpd_enable_show, inode->i_private);
}

static ssize_t aee_kpd_enable_read(struct file *filp, char __user *buff,
				size_t count, loff_t *off)
{
	char page[256] = {0};
	char read_data[16] = {0};
	int len = 0;

	if (aee_kpd_enable)
		read_data[0] = '1';
	else
		read_data[0] = '0';

	len = sprintf(page, "%s", read_data);

	if(len > *off)
		len -= *off;
	else
		len = 0;
	if (copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static ssize_t aee_kpd_enable_write(struct file *filp, const char __user *buff,
				size_t len, loff_t *data)
{
	char temp[16] = {0};
	if (len >= 16) {
		pr_err("aee_kpd_enable_write get an illegal value over 16 characters.\n");
		return -EFAULT;
	}
	if (copy_from_user(temp, buff, len)) {
		pr_err("aee_kpd_enable_write error.\n");
		return -EFAULT;
	}

	if(kstrtoint(temp, 10, &aee_kpd_enable) !=0) {
		return -EINVAL;
	}

	//#ifdef OPLUS_BUG_STABILITY
	if( get_eng_version() == PREVERSION ) {
		pr_err("%s force to enable volumekey dump in preversion build\n", __func__);
		aee_kpd_enable = 1;
	}
	//#endif /* OPLUS_BUG_STABILITY */

	pr_err("%s enable:%d\n", __func__, aee_kpd_enable);

	return len;
}

static const struct proc_ops aee_kpd_enable_proc_fops = {
	.proc_open    = aee_kpd_enable_open,
	.proc_write   = aee_kpd_enable_write,
	.proc_read    = aee_kpd_enable_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};

static void init_proc_aee_kpd_enable(void)
{
	struct proc_dir_entry *p = NULL;

	p = proc_create("aee_kpd_enable", 0664,
				NULL, &aee_kpd_enable_proc_fops);
	if (!p)
		pr_err("proc_create aee_kpd_enable ops fail!\n");

	return;
}
//#endif /*OPLUS_BUG_STABILITY*/

static int __maybe_unused mtk_pmic_keys_suspend(struct device *dev)
{
	struct mtk_pmic_keys *keys = dev_get_drvdata(dev);
	int index;

	for (index = 0; index < MTK_PMIC_MAX_KEY_COUNT; index++) {
		if (keys->keys[index].suspend_lock)
			enable_irq_wake(keys->keys[index].irq);
	}

	return 0;
}

static int __maybe_unused mtk_pmic_keys_resume(struct device *dev)
{
	struct mtk_pmic_keys *keys = dev_get_drvdata(dev);
	int index;

	for (index = 0; index < MTK_PMIC_MAX_KEY_COUNT; index++) {
		if (keys->keys[index].suspend_lock)
			disable_irq_wake(keys->keys[index].irq);
	}

	return 0;
}

static SIMPLE_DEV_PM_OPS(mtk_pmic_keys_pm_ops, mtk_pmic_keys_suspend,
			mtk_pmic_keys_resume);

static const struct of_device_id of_mtk_pmic_keys_match_tbl[] = {
	{
		.compatible = "mediatek,mt6359p-keys",
		.data = &mt6359p_regs,
	}, {
		.compatible = "mediatek,mt6397-keys",
		.data = &mt6397_regs,
	}, {
		.compatible = "mediatek,mt6323-keys",
		.data = &mt6323_regs,
	}, {
		.compatible = "mediatek,mt6363-keys",
		.data = &mt6363_regs,
	}, {
		.compatible = "mediatek,mt6366-keys",
		.data = &mt6366_regs,
	}, {
		.compatible = "mediatek,mt6358-keys",
		.data = &mt6358_regs,
	}, {
		.compatible = "mediatek,mt6357-keys",
		.data = &mt6357_regs,
	}, {
		.compatible = "mediatek,mt6661-keys",
		.data = &mt6661_regs,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, of_mtk_pmic_keys_match_tbl);

static int mtk_pmic_keys_probe(struct platform_device *pdev)
{
	int error, index = 0;
	unsigned int keycount;
	unsigned int release_irq_interval;
	struct mt6397_chip *pmic_chip;
	struct device_node *node = pdev->dev.of_node, *child;
	struct mtk_pmic_keys *keys;
	const struct mtk_pmic_regs *mtk_pmic_regs;
	struct input_dev *input_dev;
	const struct of_device_id *of_id =
		of_match_device(of_mtk_pmic_keys_match_tbl, &pdev->dev);
	bool pwrkey_scp_enable = false;

	ktf_pmic_pdev = pdev;
	keys = devm_kzalloc(&pdev->dev, sizeof(*keys), GFP_KERNEL);
	if (!keys)
		return -ENOMEM;

	keys->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!keys->regmap) {
		pmic_chip =  dev_get_drvdata(pdev->dev.parent);
		if (!pmic_chip || !pmic_chip->regmap) {
			dev_info(keys->dev, "failed to get pmic key regmap\n");
			return -ENODEV;
		}

		keys->regmap = pmic_chip->regmap;
	}

	keys->dev = &pdev->dev;
	if (!of_id) {
		dev_info(keys->dev, "failed to get of_match_device\n");
		return -ENODEV;
	}
	mtk_pmic_regs = of_id->data;

	keys->input_dev = input_dev = devm_input_allocate_device(keys->dev);
	if (!input_dev) {
		dev_dbg(keys->dev, "input allocate device fail.\n");
		return -ENOMEM;
	}

	input_dev->name = "mtk-pmic-keys";
	input_dev->id.bustype = BUS_HOST;
	input_dev->id.vendor = 0x0001;
	input_dev->id.product = 0x0001;
	input_dev->id.version = 0x0001;

	__set_bit(EV_KEY, input_dev->evbit);
	keycount = of_get_available_child_count(node);
	if(strncmp(of_id->compatible, "mediatek,mt6363-keys", 20) == 0 ||
		strncmp(of_id->compatible, "mediatek,mt6661-keys", 20) == 0)
		release_irq_interval = 3;
	else
		release_irq_interval = 2;
	need_reset_home = false;
	if(strncmp(of_id->compatible, "mediatek,mt6661-keys", 20) == 0)
		need_reset_home = true;
	ktf_pmic_key = keys;
	if (keycount > MTK_PMIC_MAX_KEY_COUNT) {
		dev_err(keys->dev, "too many keys defined (%d)\n", keycount);
		return -EINVAL;
	}

	for_each_child_of_node(node, child) {
		keys->keys[index].regs = &mtk_pmic_regs->keys_regs[index];

		keys->keys[index].irq = platform_get_irq(pdev, index);
		if (keys->keys[index].irq < 0)
			return keys->keys[index].irq;
		if (mtk_pmic_regs->release_irq) {
			keys->keys[index].release_irq_num = platform_get_irq(
						pdev,
						index + release_irq_interval);
			if (keys->keys[index].release_irq_num < 0)
				return keys->keys[index].release_irq_num;
		}

		error = of_property_read_u32(child,
			"linux,keycodes", &keys->keys[index].keycode);
		if (error) {
			dev_dbg(keys->dev,
				"failed to read key:%d linux,keycode property: %d\n",
				index, error);
			of_node_put(child);
			return error;
		}

		if (of_property_read_bool(child, "wakeup-source"))
			keys->keys[index].suspend_lock =
				wakeup_source_register(NULL, "pwrkey wakelock");

		error = mtk_pmic_key_setup(keys, &keys->keys[index]);
		if (error) {
			of_node_put(child);
			return error;
		}

		if (index == 2) {
			error = keypad_pinctrl_init(keys);
			if (error < 0) {
				dev_dbg(keys->dev, "failed to init keypad gpio\n");
				return error;
			}
		}

		index++;
	}

	pwrkey_scp_enable = of_property_read_bool(keys->dev->of_node,
						"pwrkey-scp-enable");

	if (pwrkey_scp_enable) {
		dev_info(keys->dev, "powerkey on scp is enabled\n");
		error = device_create_file(&pdev->dev, &dev_attr_powerkey_irq);
		if (error) {
			dev_info(&pdev->dev,
				"create powerkey_irq failed (%d)\n", error);
			return error;
		}
	}

	error = input_register_device(input_dev);
	if (error) {
		dev_err(&pdev->dev,
			"register input device failed (%d)\n", error);
		return error;
	}

	mtk_pmic_keys_lp_reset_setup(keys, mtk_pmic_regs);

	platform_set_drvdata(pdev, keys);

	if (of_find_node_by_name(NULL, "volume_down_key")) {
		pr_err("%s: find volume_down key node, init volume_down_key\n", __func__);
		gpio_volume_down_key_init(pdev);
	}

	//#ifdef OPLUS_BUG_STABILITY
	hrtimer_init(&aee_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	aee_timer.function = aee_timer_func;

	hrtimer_init(&voldown_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	voldown_timer.function = voldown_timer_func;

	init_proc_aee_kpd_enable();
	if(get_eng_version() == AGING ||
	   get_eng_version() == PREVERSION ||
	   get_eng_version() == HIGH_TEMP_AGING ||
	   get_eng_version() == FACTORY) {
		aee_kpd_enable = 1;
	} else {
		aee_kpd_enable = 0;
	}
	//#endif /* OPLUS_BUG_STABILITY */

	return 0;
}

static struct platform_driver pmic_keys_pdrv = {
	.probe = mtk_pmic_keys_probe,
	.driver = {
		   .name = "mtk-pmic-keys",
		   .of_match_table = of_mtk_pmic_keys_match_tbl,
		   .pm = &mtk_pmic_keys_pm_ops,
	},
};

module_platform_driver(pmic_keys_pdrv);

int ktf_mtk_pmic_kpd_test(char *str)
{
	int ret = 0;

	if (!str)
		return -EINVAL;
	if (!ktf_pmic_pdev)
		return -ENODEV;
	if (!ktf_pmic_key)
		return -ENODEV;
	if (!strncmp(str, "pmicsuspend", 11)) {
		mtk_pmic_keys_suspend(ktf_pmic_key->dev);
		ret = mtk_pmic_keys_resume(ktf_pmic_key->dev);
	} else if (!strncmp(str, "pmicprobe", 9)) {
		ret = mtk_pmic_keys_probe(ktf_pmic_pdev);
	} else {
		pr_info("%s is fail", __func__);
		ret = -ENODEV;
	}
	return ret;
}
EXPORT_SYMBOL(ktf_mtk_pmic_kpd_test);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Chen Zhong <chen.zhong@mediatek.com>");
MODULE_DESCRIPTION("MTK pmic-keys driver v0.1");
