// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Owen Chen <owen.chen@mediatek.com>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>

#include <dt-bindings/power/mt6895-power.h>

#if IS_ENABLED(CONFIG_DEVICE_MODULES_MTK_DEVAPC)
#include <linux/soc/mediatek/devapc_public.h>
#endif

#if IS_ENABLED(CONFIG_MTK_DVFSRC_HELPER)
#include <mt-plat/dvfsrc-exp.h>
#endif

#include "clkchk.h"
#include "clkchk-mt6895.h"

#define BUG_ON_CHK_ENABLE		0
#define CHECK_VCORE_FREQ		0
#define CG_CHK_PWRON_ENABLE		1

#define HWV_ADDR_HISTORY_0		0x1F04
#define HWV_DATA_HISTORY_0		0x1F44
#define HWV_IDX_POINTER			0x1F84
#define HWV_DOMAIN_KEY			0x155C
#define HWV_SECURE_KEY			0x10907
#define HWV_CG_SET(id)			(0x0 + (id * 0x8))
#define HWV_CG_STA(id)			(0x1800 + (id * 0x4))
#define HWV_CG_EN(id)			(0x1900 + (id * 0x4))
#define HWV_CG_SET_STA(id)		(0x1A00 + (id * 0x4))
#define HWV_CG_CLR_STA(id)		(0x1B00 + (id * 0x4))
#define HWV_CG_DONE(id)			(0x1C00 + (id * 0x4))

#define HWV_PLL_SET			(0x190)
#define HWV_PLL_CLR			(0x194)
#define HWV_PLL_EN			(0x1400)
#define HWV_PLL_STA			(0x1404)
#define HWV_PLL_DONE			(0x140C)
#define HWV_PLL_SET_STA			(0x1464)
#define HWV_PLL_CLR_STA			(0x1468)


/*
 * clkchk dump_regs
 */

#define REGBASE_V(_phys, _id_name, _pg, _pn) { .phys = _phys,	\
		.name = #_id_name, .pg = _pg, .pn = _pn}

static struct regbase rb[] = {
	[top] = REGBASE_V(0x10000000, top, PD_NULL, CLK_NULL),
	[ifrao] = REGBASE_V(0x10001000, ifrao, PD_NULL, CLK_NULL),
	[infracfg] = REGBASE_V(0x10001000, infracfg, PD_NULL, CLK_NULL),
	[apmixed] = REGBASE_V(0x1000C000, apmixed, PD_NULL, CLK_NULL),
	[nemi_reg] = REGBASE_V(0x10219000, nemi_reg, PD_NULL, CLK_NULL),
	[semi_reg] = REGBASE_V(0x1021d000, semi_reg, PD_NULL, CLK_NULL),
	[perao] = REGBASE_V(0x11036000, perao, PD_NULL, CLK_NULL),
	[usb_d] = REGBASE_V(0x11201000, usb_d, PD_NULL, CLK_NULL),
	[usb_sif] = REGBASE_V(0x11203e00, usb_sif, PD_NULL, CLK_NULL),
	[usb_sif_p1] = REGBASE_V(0x11213e00, usb_sif_p1, PD_NULL, CLK_NULL),
	[impc] = REGBASE_V(0x11282000, impc, PD_NULL, "i2c_ck"),
	[ufsao] = REGBASE_V(0x112b8000, ufsao, PD_NULL, CLK_NULL),
	[ufspdn] = REGBASE_V(0x112bc000, ufspdn, PD_NULL, CLK_NULL),
	[imps] = REGBASE_V(0x11D07000, imps, PD_NULL, "i2c_ck"),
	[impw] = REGBASE_V(0x11F41000, impw, PD_NULL, "i2c_ck"),
	[mfg_ao] = REGBASE_V(0x13fa0000, mfg_ao, PD_NULL, CLK_NULL),
	[mfgsc_ao] = REGBASE_V(0x13fa0c00, mfgsc_ao, PD_NULL, CLK_NULL),
	[mfgcfg] = REGBASE_V(0x13fbf000, mfgcfg, PD_NULL, CLK_NULL),
	[mm0] = REGBASE_V(0x14000000, mm0, MT6895_POWER_DOMAIN_DISP, CLK_NULL),
	[mm1] = REGBASE_V(0x14400000, mm1, MT6895_POWER_DOMAIN_DISP1, CLK_NULL),
	[img] = REGBASE_V(0x15000000, img, MT6895_POWER_DOMAIN_ISP_MAIN, CLK_NULL),
	[dip_top_dip1] = REGBASE_V(0x15110000, dip_top_dip1, MT6895_POWER_DOMAIN_ISP_DIP1,
		CLK_NULL),
	[dip_nr_dip1] = REGBASE_V(0x15130000, dip_nr_dip1, MT6895_POWER_DOMAIN_ISP_DIP1, CLK_NULL),
	[wpe1_dip1] = REGBASE_V(0x15220000, wpe1_dip1, MT6895_POWER_DOMAIN_ISP_DIP1, CLK_NULL),
	[ipe] = REGBASE_V(0x15330000, ipe, MT6895_POWER_DOMAIN_ISP_IPE, CLK_NULL),
	[wpe2_dip1] = REGBASE_V(0x15520000, wpe2_dip1, MT6895_POWER_DOMAIN_ISP_DIP1, CLK_NULL),
	[wpe3_dip1] = REGBASE_V(0x15620000, wpe3_dip1, MT6895_POWER_DOMAIN_ISP_DIP1, CLK_NULL),
	[vde1] = REGBASE_V(0x1600f000, vde1, MT6895_POWER_DOMAIN_VDE0, CLK_NULL),
	[vde2] = REGBASE_V(0x1602f000, vde2, MT6895_POWER_DOMAIN_VDE1, CLK_NULL),
	[ven1] = REGBASE_V(0x17000000, ven1, MT6895_POWER_DOMAIN_VEN0, CLK_NULL),
	[ven2] = REGBASE_V(0x17800000, ven2, MT6895_POWER_DOMAIN_VEN1, CLK_NULL),
	[apu0_ao] = REGBASE_V(0x190f3000, apu0_ao, PD_NULL, CLK_NULL),
	[npu_ao] = REGBASE_V(0x190f3400, npu_ao, PD_NULL, CLK_NULL),
	[apu1_ao] = REGBASE_V(0x190f3800, apu1_ao, PD_NULL, CLK_NULL),
	[spm] = REGBASE_V(0x1C001000, spm, PD_NULL, CLK_NULL),
	[vlpcfg] = REGBASE_V(0x1C00C000, vlpcfg, PD_NULL, CLK_NULL),
	[vlp_ck] = REGBASE_V(0x1C013000, vlp_ck, PD_NULL, CLK_NULL),
	[cam_m] = REGBASE_V(0x1a000000, cam_m, MT6895_POWER_DOMAIN_CAM_MAIN, CLK_NULL),
	[cam_ra] = REGBASE_V(0x1a04f000, cam_ra, MT6895_POWER_DOMAIN_CAM_SUBA, CLK_NULL),
	[cam_ya] = REGBASE_V(0x1a06f000, cam_ya, MT6895_POWER_DOMAIN_CAM_SUBA, CLK_NULL),
	[cam_rb] = REGBASE_V(0x1a08f000, cam_rb, MT6895_POWER_DOMAIN_CAM_SUBB, CLK_NULL),
	[cam_yb] = REGBASE_V(0x1a0af000, cam_yb, MT6895_POWER_DOMAIN_CAM_SUBB, CLK_NULL),
	[cam_rc] = REGBASE_V(0x1a0cf000, cam_rc, MT6895_POWER_DOMAIN_CAM_SUBC, CLK_NULL),
	[cam_yc] = REGBASE_V(0x1a0ef000, cam_yc, MT6895_POWER_DOMAIN_CAM_SUBC, CLK_NULL),
	[cam_mr] = REGBASE_V(0x1a170000, cam_mr, MT6895_POWER_DOMAIN_CAM_MRAW, CLK_NULL),
	[ccu] = REGBASE_V(0x1b200000, ccu, MT6895_POWER_DOMAIN_CAM_MAIN, CLK_NULL),
	[afe] = REGBASE_V(0x1e100000, afe, MT6895_POWER_DOMAIN_AUDIO, CLK_NULL),
	[mminfra_config] = REGBASE_V(0x1e800000, mminfra_config, MT6895_POWER_DOMAIN_MM_INFRA,
		CLK_NULL),
	[mdp] = REGBASE_V(0x1f000000, mdp, MT6895_POWER_DOMAIN_MDP0, CLK_NULL),
	[mdp1] = REGBASE_V(0x1f800000, mdp1, MT6895_POWER_DOMAIN_MDP1, CLK_NULL),
	[img_subcomm0] = REGBASE_V(0x15002000, img, MT6895_POWER_DOMAIN_ISP_MAIN, CLK_NULL),
	[img_subcomm1] = REGBASE_V(0x15003000, img, MT6895_POWER_DOMAIN_ISP_MAIN, CLK_NULL),
	[cam_mm_subcomm0] = REGBASE_V(0x1a005000, cam_m, MT6895_POWER_DOMAIN_CAM_MAIN, CLK_NULL),
	[cam_mdp_subcomm1] = REGBASE_V(0x1a006000, cam_m, MT6895_POWER_DOMAIN_CAM_MAIN, CLK_NULL),
	[cam_sys_subcomm1] = REGBASE_V(0x1a007000, cam_m, MT6895_POWER_DOMAIN_CAM_MAIN, CLK_NULL),
	[hwv] = REGBASE_V(0x10320000, hwv, PD_NULL, CLK_NULL),
	{},
};

#define REGNAME(_base, _ofs, _name)	\
	{ .base = &rb[_base], .ofs = _ofs, .name = #_name }

static struct regname rn[] = {
	/* TOPCKGEN register */
	REGNAME(top, 0x0010, CLK_CFG_0),
	REGNAME(top, 0x0020, CLK_CFG_1),
	REGNAME(top, 0x0030, CLK_CFG_2),
	REGNAME(top, 0x0040, CLK_CFG_3),
	REGNAME(top, 0x0050, CLK_CFG_4),
	REGNAME(top, 0x0060, CLK_CFG_5),
	REGNAME(top, 0x0070, CLK_CFG_6),
	REGNAME(top, 0x0080, CLK_CFG_7),
	REGNAME(top, 0x0090, CLK_CFG_8),
	REGNAME(top, 0x00A0, CLK_CFG_9),
	REGNAME(top, 0x00B0, CLK_CFG_10),
	REGNAME(top, 0x00C0, CLK_CFG_11),
	REGNAME(top, 0x00D0, CLK_CFG_12),
	REGNAME(top, 0x00E0, CLK_CFG_13),
	REGNAME(top, 0x00F0, CLK_CFG_14),
	REGNAME(top, 0x0100, CLK_CFG_15),
	REGNAME(top, 0x0110, CLK_CFG_16),
	REGNAME(top, 0x0120, CLK_CFG_17),
	REGNAME(top, 0x0130, CLK_CFG_18),
	REGNAME(top, 0x0140, CLK_CFG_19),
	REGNAME(top, 0x0150, CLK_CFG_20),
	REGNAME(top, 0x0160, CLK_CFG_21),
	REGNAME(top, 0x01f0, CLK_CFG_30),
	REGNAME(top, 0x0320, CLK_AUDDIV_0),
	REGNAME(top, 0x0328, CLK_AUDDIV_2),
	REGNAME(top, 0x0334, CLK_AUDDIV_3),
	REGNAME(top, 0x0338, CLK_AUDDIV_4),
	REGNAME(top, 0x240, CLK_MISC_CFG_0),
	REGNAME(top, 0x0, CLK_MODE),
	/* INFRACFG_AO register */
	REGNAME(ifrao, 0x6C, HRE_INFRA_BUS_CTRL),
	REGNAME(ifrao, 0x70, INFRA_BUS_DCM_CTRL),
	REGNAME(ifrao, 0x90, MODULE_CG_0),
	REGNAME(ifrao, 0x94, MODULE_CG_1),
	REGNAME(ifrao, 0xAC, MODULE_CG_2),
	REGNAME(ifrao, 0xC8, MODULE_CG_3),
	REGNAME(ifrao, 0xE8, MODULE_CG_4),
	/* INFRACFG_AO_BUS register */
	REGNAME(infracfg, 0x0C50, INFRASYS_PROTECT_EN_1),
	REGNAME(infracfg, 0x0C5C, INFRASYS_PROTECT_RDY_STA_1),
	REGNAME(infracfg, 0x0C90, MCU_CONNSYS_PROTECT_EN_0),
	REGNAME(infracfg, 0x0C9C, MCU_CONNSYS_PROTECT_RDY_STA_0),
	REGNAME(infracfg, 0x0C40, INFRASYS_PROTECT_EN_0),
	REGNAME(infracfg, 0x0C4C, INFRASYS_PROTECT_RDY_STA_0),
	REGNAME(infracfg, 0x0C80, PERISYS_PROTECT_EN_0),
	REGNAME(infracfg, 0x0C8C, PERISYS_PROTECT_RDY_STA_0),
	REGNAME(infracfg, 0x0C30, MMSYS_PROTECT_EN_2),
	REGNAME(infracfg, 0x0C3C, MMSYS_PROTECT_RDY_STA_2),
	REGNAME(infracfg, 0x0C10, MMSYS_PROTECT_EN_0),
	REGNAME(infracfg, 0x0C1C, MMSYS_PROTECT_RDY_STA_0),
	REGNAME(infracfg, 0x0C20, MMSYS_PROTECT_EN_1),
	REGNAME(infracfg, 0x0C2C, MMSYS_PROTECT_RDY_STA_1),
	REGNAME(infracfg, 0x0CC0, DRAMC_CCUSYS_PROTECT_EN_0),
	REGNAME(infracfg, 0x0CCC, DRAMC_CCUSYS_PROTECT_RDY_STA_0),
	REGNAME(infracfg, 0x0CA0, MD_MFGSYS_PROTECT_EN_0),
	REGNAME(infracfg, 0x0CAC, MD_MFGSYS_PROTECT_RDY_STA_0),
	REGNAME(infracfg, 0x0C60, EMISYS_PROTECT_EN_0),
	REGNAME(infracfg, 0x0C6C, EMISYS_PROTECT_RDY_STA_0),
	REGNAME(infracfg, 0x0C70, EMISYS_PROTECT_EN_1),
	REGNAME(infracfg, 0x0C7C, EMISYS_PROTECT_RDY_STA_1),
	/* APMIXEDSYS register */
	REGNAME(apmixed, 0x208, ARMPLL_LL_CON0),
	REGNAME(apmixed, 0x20c, ARMPLL_LL_CON1),
	REGNAME(apmixed, 0x210, ARMPLL_LL_CON2),
	REGNAME(apmixed, 0x214, ARMPLL_LL_CON3),
	REGNAME(apmixed, 0x218, ARMPLL_BL_CON0),
	REGNAME(apmixed, 0x21c, ARMPLL_BL_CON1),
	REGNAME(apmixed, 0x220, ARMPLL_BL_CON2),
	REGNAME(apmixed, 0x224, ARMPLL_BL_CON3),
	REGNAME(apmixed, 0x228, ARMPLL_B_CON0),
	REGNAME(apmixed, 0x22c, ARMPLL_B_CON1),
	REGNAME(apmixed, 0x230, ARMPLL_B_CON2),
	REGNAME(apmixed, 0x234, ARMPLL_B_CON3),
	REGNAME(apmixed, 0x238, CCIPLL_CON0),
	REGNAME(apmixed, 0x23c, CCIPLL_CON1),
	REGNAME(apmixed, 0x240, CCIPLL_CON2),
	REGNAME(apmixed, 0x244, CCIPLL_CON3),
	REGNAME(apmixed, 0x350, MAINPLL_CON0),
	REGNAME(apmixed, 0x354, MAINPLL_CON1),
	REGNAME(apmixed, 0x358, MAINPLL_CON2),
	REGNAME(apmixed, 0x35c, MAINPLL_CON3),
	REGNAME(apmixed, 0x308, UNIVPLL_CON0),
	REGNAME(apmixed, 0x30c, UNIVPLL_CON1),
	REGNAME(apmixed, 0x310, UNIVPLL_CON2),
	REGNAME(apmixed, 0x314, UNIVPLL_CON3),
	REGNAME(apmixed, 0x360, MSDCPLL_CON0),
	REGNAME(apmixed, 0x364, MSDCPLL_CON1),
	REGNAME(apmixed, 0x368, MSDCPLL_CON2),
	REGNAME(apmixed, 0x36c, MSDCPLL_CON3),
	REGNAME(apmixed, 0x3a0, MMPLL_CON0),
	REGNAME(apmixed, 0x3a4, MMPLL_CON1),
	REGNAME(apmixed, 0x3a8, MMPLL_CON2),
	REGNAME(apmixed, 0x3ac, MMPLL_CON3),
	REGNAME(apmixed, 0x380, ADSPPLL_CON0),
	REGNAME(apmixed, 0x384, ADSPPLL_CON1),
	REGNAME(apmixed, 0x388, ADSPPLL_CON2),
	REGNAME(apmixed, 0x38c, ADSPPLL_CON3),
	REGNAME(apmixed, 0x248, TVDPLL_CON0),
	REGNAME(apmixed, 0x24c, TVDPLL_CON1),
	REGNAME(apmixed, 0x250, TVDPLL_CON2),
	REGNAME(apmixed, 0x254, TVDPLL_CON3),
	REGNAME(apmixed, 0x328, APLL1_CON0),
	REGNAME(apmixed, 0x32c, APLL1_CON1),
	REGNAME(apmixed, 0x330, APLL1_CON2),
	REGNAME(apmixed, 0x334, APLL1_CON3),
	REGNAME(apmixed, 0x338, APLL1_CON4),
	REGNAME(apmixed, 0x0040, APLL1_TUNER_CON0),
	REGNAME(apmixed, 0x000C, AP_PLL_CON3),
	REGNAME(apmixed, 0x33c, APLL2_CON0),
	REGNAME(apmixed, 0x340, APLL2_CON1),
	REGNAME(apmixed, 0x344, APLL2_CON2),
	REGNAME(apmixed, 0x348, APLL2_CON3),
	REGNAME(apmixed, 0x34c, APLL2_CON4),
	REGNAME(apmixed, 0x0044, APLL2_TUNER_CON0),
	REGNAME(apmixed, 0x000C, AP_PLL_CON3),
	REGNAME(apmixed, 0x390, MPLL_CON0),
	REGNAME(apmixed, 0x394, MPLL_CON1),
	REGNAME(apmixed, 0x398, MPLL_CON2),
	REGNAME(apmixed, 0x39c, MPLL_CON3),
	REGNAME(apmixed, 0x370, IMGPLL_CON0),
	REGNAME(apmixed, 0x374, IMGPLL_CON1),
	REGNAME(apmixed, 0x378, IMGPLL_CON2),
	REGNAME(apmixed, 0x37c, IMGPLL_CON3),
	/* NEMI_REG register */
	REGNAME(nemi_reg, 0x858, EMI_THRO_CTRL1),
	/* SEMI_REG register */
	REGNAME(semi_reg, 0x858, EMI_THRO_CTRL1),
	/* PERICFG_AO register */
	REGNAME(perao, 0x10, PERI_CG_0),
	REGNAME(perao, 0x14, PERI_CG_1),
	REGNAME(perao, 0x18, PERI_CG_2),
	/* SSUSB_DEVICE register */
	REGNAME(usb_d, 0xC84, MISC_CTRL),
	/* SSUSB_SIFSLV_IPPC register */
	REGNAME(usb_sif, 0x80, SSUSB_DMA_CTRL),
	REGNAME(usb_sif, 0x50, SSUSB_U2_CTRL_0P),
	REGNAME(usb_sif, 0x30, SSUSB_U3_CTRL_0P),
	/* SSUSB_SIFSLV_IPPC_P1 register */
	REGNAME(usb_sif_p1, 0x80, SSUSB_DMA_CTRL),
	REGNAME(usb_sif_p1, 0x50, SSUSB_U2_CTRL_0P),
	REGNAME(usb_sif_p1, 0x30, SSUSB_U3_CTRL_0P),
	/* IMP_IIC_WRAP_C register */
	REGNAME(impc, 0xE00, AP_CLOCK_CG_CEN),
	/* UFS_AO_CONFIG register */
	REGNAME(ufsao, 0x0, UFS_AO_CG_0),
	/* UFS_PDN_CFG register */
	REGNAME(ufspdn, 0x0, UFS_PDN_CG_0),
	/* IMP_IIC_WRAP_S register */
	REGNAME(imps, 0xE00, AP_CLOCK_CG_SOU),
	/* IMP_IIC_WRAP_W register */
	REGNAME(impw, 0xE00, AP_CLOCK_CG_WN),
	/* MFGPLL_PLL_CTRL register */
	REGNAME(mfg_ao, 0x8, MFGPLL_CON0),
	REGNAME(mfg_ao, 0xc, MFGPLL_CON1),
	REGNAME(mfg_ao, 0x10, MFGPLL_CON2),
	REGNAME(mfg_ao, 0x14, MFGPLL_CON3),
	/* MFGSCPLL_PLL_CTRL register */
	REGNAME(mfgsc_ao, 0x8, MFGSCPLL_CON0),
	REGNAME(mfgsc_ao, 0xc, MFGSCPLL_CON1),
	REGNAME(mfgsc_ao, 0x10, MFGSCPLL_CON2),
	REGNAME(mfgsc_ao, 0x14, MFGSCPLL_CON3),
	/* MFG_TOP_CONFIG register */
	REGNAME(mfgcfg, 0x0, MFG_CG),
	/* MMSYS0_CONFIG register */
	REGNAME(mm0, 0x100, MMSYS_CG_0),
	REGNAME(mm0, 0x110, MMSYS_CG_1),
	REGNAME(mm0, 0x1A0, MMSYS_CG_2),
	/* MMSYS1_CONFIG register */
	REGNAME(mm1, 0x100, MMSYS_CG_0),
	REGNAME(mm1, 0x110, MMSYS_CG_1),
	REGNAME(mm1, 0x1A0, MMSYS_CG_2),
	/* IMGSYS_MAIN register */
	REGNAME(img, 0x0, IMG_MAIN_CG),
	/* DIP_TOP_DIP1 register */
	REGNAME(dip_top_dip1, 0x0, MACRO_CG),
	/* DIP_NR_DIP1 register */
	REGNAME(dip_nr_dip1, 0x0, MACRO_CG),
	/* WPE1_DIP1 register */
	REGNAME(wpe1_dip1, 0x0, MACRO_CG),
	/* IPESYS register */
	REGNAME(ipe, 0x0, MACRO_CG),
	/* WPE2_DIP1 register */
	REGNAME(wpe2_dip1, 0x0, MACRO_CG),
	/* WPE3_DIP1 register */
	REGNAME(wpe3_dip1, 0x0, MACRO_CG),
	/* VDEC_SOC_GCON_BASE register */
	REGNAME(vde1, 0x8, LARB_CKEN_CON),
	REGNAME(vde1, 0x200, LAT_CKEN),
	REGNAME(vde1, 0x190, MINI_MDP_CFG_0),
	REGNAME(vde1, 0x0, VDEC_CKEN),
	/* VDEC_GCON_BASE register */
	REGNAME(vde2, 0x8, LARB_CKEN_CON),
	REGNAME(vde2, 0x200, LAT_CKEN),
	REGNAME(vde2, 0x0, VDEC_CKEN),
	/* VENC_GCON register */
	REGNAME(ven1, 0x0, VENCSYS_CG),
	/* VENC_GCON_CORE1 register */
	REGNAME(ven2, 0x0, VENCSYS_CG),
	/* APUPLL_PLL_CTRL register */
	REGNAME(apu0_ao, 0x8, APUPLL_CON0),
	REGNAME(apu0_ao, 0xc, APUPLL_CON1),
	REGNAME(apu0_ao, 0x10, APUPLL_CON2),
	REGNAME(apu0_ao, 0x14, APUPLL_CON3),
	/* NPUPLL_PLL_CTRL register */
	REGNAME(npu_ao, 0x8, NPUPLL_CON0),
	REGNAME(npu_ao, 0xc, NPUPLL_CON1),
	REGNAME(npu_ao, 0x10, NPUPLL_CON2),
	REGNAME(npu_ao, 0x14, NPUPLL_CON3),
	/* APUPLL1_PLL_CTRL register */
	REGNAME(apu1_ao, 0x8, APUPLL1_CON0),
	REGNAME(apu1_ao, 0xc, APUPLL1_CON1),
	REGNAME(apu1_ao, 0x10, APUPLL1_CON2),
	REGNAME(apu1_ao, 0x14, APUPLL1_CON3),
	/* SPM register */
	REGNAME(spm, 0xE00, MD1_PWR_CON),
	REGNAME(spm, 0xF30, SOC_BUCK_ISO_CON),
	REGNAME(spm, 0xF34, PWR_STATUS),
	REGNAME(spm, 0xF38, PWR_STATUS_2ND),
	REGNAME(spm, 0xF2C, MD_BUCK_ISO_CON),
	REGNAME(spm, 0xE04, CONN_PWR_CON),
	REGNAME(spm, 0xE10, UFS0_PWR_CON),
	REGNAME(spm, 0xE14, AUDIO_PWR_CON),
	REGNAME(spm, 0xE18, ADSP_TOP_PWR_CON),
	REGNAME(spm, 0xE1C, ADSP_INFRA_PWR_CON),
	REGNAME(spm, 0xE20, ADSP_AO_PWR_CON),
	REGNAME(spm, 0xE24, ISP_MAIN_PWR_CON),
	REGNAME(spm, 0xE28, ISP_DIP1_PWR_CON),
	REGNAME(spm, 0xE2C, ISP_IPE_PWR_CON),
	REGNAME(spm, 0xE30, ISP_VCORE_PWR_CON),
	REGNAME(spm, 0xE34, VDE0_PWR_CON),
	REGNAME(spm, 0xE38, VDE1_PWR_CON),
	REGNAME(spm, 0xE3C, VEN0_PWR_CON),
	REGNAME(spm, 0xE40, VEN1_PWR_CON),
	REGNAME(spm, 0xE44, CAM_MAIN_PWR_CON),
	REGNAME(spm, 0xE48, CAM_MRAW_PWR_CON),
	REGNAME(spm, 0xE4C, CAM_SUBA_PWR_CON),
	REGNAME(spm, 0xE50, CAM_SUBB_PWR_CON),
	REGNAME(spm, 0xE54, CAM_SUBC_PWR_CON),
	REGNAME(spm, 0xE58, CAM_VCORE_PWR_CON),
	REGNAME(spm, 0xE5C, MDP0_PWR_CON),
	REGNAME(spm, 0xE60, MDP1_PWR_CON),
	REGNAME(spm, 0xE64, DIS0_PWR_CON),
	REGNAME(spm, 0xE68, DIS1_PWR_CON),
	REGNAME(spm, 0xE6C, MM_INFRA_PWR_CON),
	REGNAME(spm, 0xE70, MM_PROC_PWR_CON),
	REGNAME(spm, 0xE74, DP_TX_PWR_CON),
	REGNAME(spm, 0xEB8, MFG0_PWR_CON),
	REGNAME(spm, 0xF3C, XPU_PWR_STATUS),
	REGNAME(spm, 0xF40, XPU_PWR_STATUS_2ND),
	REGNAME(spm, 0xEBC, MFG1_PWR_CON),
	REGNAME(spm, 0xEC0, MFG2_PWR_CON),
	REGNAME(spm, 0xEC4, MFG3_PWR_CON),
	REGNAME(spm, 0xEC8, MFG4_PWR_CON),
	REGNAME(spm, 0xECC, MFG5_PWR_CON),
	REGNAME(spm, 0xED0, MFG6_PWR_CON),
	REGNAME(spm, 0xED4, MFG7_PWR_CON),
	REGNAME(spm, 0xED8, MFG8_PWR_CON),
	REGNAME(spm, 0xEDC, MFG9_PWR_CON),
	REGNAME(spm, 0xEE0, MFG10_PWR_CON),
	REGNAME(spm, 0xEE4, MFG11_PWR_CON),
	REGNAME(spm, 0xEE8, MFG12_PWR_CON),
	REGNAME(spm, 0x670, SPM_CROSS_WAKE_M01_REQ),
	REGNAME(spm, 0x414, SPM2APU_CON),
	REGNAME(spm, 0xF08, EMI_HRE_SRAM_CON),
	/* VLPCFG_BUS register */
	REGNAME(vlpcfg, 0x0210, VLP_TOPAXI_PROTECTEN),
	REGNAME(vlpcfg, 0x0220, VLP_TOPAXI_PROTECTEN_STA1),
	REGNAME(vlpcfg, 0x0230, VLP_TOPAXI_PROTECTEN1),
	REGNAME(vlpcfg, 0x0240, VLP_TOPAXI_PROTECTEN1_STA1),
	/* VLP_CKSYS register */
	REGNAME(vlp_ck, 0x0008, VLP_CLK_CFG_0),
	REGNAME(vlp_ck, 0x0014, VLP_CLK_CFG_1),
	REGNAME(vlp_ck, 0x0020, VLP_CLK_CFG_2),
	REGNAME(vlp_ck, 0x002C, VLP_CLK_CFG_3),
	REGNAME(vlp_ck, 0x0038, VLP_CLK_CFG_4),
	/* CAM_MAIN_R1A register */
	REGNAME(cam_m, 0x0, CAM_MAIN_CG),
	/* CAMSYS_RAWA register */
	REGNAME(cam_ra, 0x0, CAMSYS_CG),
	/* CAMSYS_YUVA register */
	REGNAME(cam_ya, 0x0, CAMSYS_CG),
	/* CAMSYS_RAWB register */
	REGNAME(cam_rb, 0x0, CAMSYS_CG),
	/* CAMSYS_YUVB register */
	REGNAME(cam_yb, 0x0, CAMSYS_CG),
	/* CAMSYS_RAWC register */
	REGNAME(cam_rc, 0x0, CAMSYS_CG),
	/* CAMSYS_YUVC register */
	REGNAME(cam_yc, 0x0, CAMSYS_CG),
	/* CAMSYS_MRAW register */
	REGNAME(cam_mr, 0x0, CAMSYS_CG),
	/* AFE register */
	REGNAME(afe, 0x0, AUDIO_TOP_0),
	REGNAME(afe, 0x4, AUDIO_TOP_1),
	REGNAME(afe, 0x8, AUDIO_TOP_2),
	/* MMINFRA_CONFIG register */
	REGNAME(mminfra_config, 0x100, MMINFRA_CG_0),
	REGNAME(mminfra_config, 0x110, MMINFRA_CG_1),
	/* MDPSYS_CONFIG register */
	REGNAME(mdp, 0x100, MDPSYS_CG_0),
	/* MDPSYS1_CONFIG register */
	REGNAME(mdp1, 0x100, MDPSYS_CG_0),
	/* smi dump */
	REGNAME(img_subcomm0, 0x3C0, IMG_CLAMP_STA),
	REGNAME(img_subcomm1, 0x3C0, IMG_CLAMP_STA1),
	REGNAME(cam_mm_subcomm0, 0x3C0, MM_CLAMP_STA),
	REGNAME(cam_mdp_subcomm1, 0x3C0, MDP_CLAMP_STA),
	REGNAME(cam_sys_subcomm1, 0x3C0, SYS_CLAMP_STA),
	/* hwv dump */
	REGNAME(hwv, 0x1F04, HWV_ADDR_HISTORY_0),
	REGNAME(hwv, 0x1F08, HWV_ADDR_HISTORY_1),
	REGNAME(hwv, 0x1F0C, HWV_ADDR_HISTORY_2),
	REGNAME(hwv, 0x1F10, HWV_ADDR_HISTORY_3),
	REGNAME(hwv, 0x1F14, HWV_ADDR_HISTORY_4),
	REGNAME(hwv, 0x1F18, HWV_ADDR_HISTORY_5),
	REGNAME(hwv, 0x1F1C, HWV_ADDR_HISTORY_6),
	REGNAME(hwv, 0x1F20, HWV_ADDR_HISTORY_7),
	REGNAME(hwv, 0x1F24, HWV_ADDR_HISTORY_8),
	REGNAME(hwv, 0x1F28, HWV_ADDR_HISTORY_9),
	REGNAME(hwv, 0x1F2C, HWV_ADDR_HISTORY_10),
	REGNAME(hwv, 0x1F30, HWV_ADDR_HISTORY_11),
	REGNAME(hwv, 0x1F34, HWV_ADDR_HISTORY_12),
	REGNAME(hwv, 0x1F38, HWV_ADDR_HISTORY_13),
	REGNAME(hwv, 0x1F3C, HWV_ADDR_HISTORY_14),
	REGNAME(hwv, 0x1F40, HWV_ADDR_HISTORY_15),
	REGNAME(hwv, 0x1F44, HWV_DATA_HISTORY_0),
	REGNAME(hwv, 0x1F48, HWV_DATA_HISTORY_1),
	REGNAME(hwv, 0x1F4C, HWV_DATA_HISTORY_2),
	REGNAME(hwv, 0x1F50, HWV_DATA_HISTORY_3),
	REGNAME(hwv, 0x1F54, HWV_DATA_HISTORY_4),
	REGNAME(hwv, 0x1F58, HWV_DATA_HISTORY_5),
	REGNAME(hwv, 0x1F5C, HWV_DATA_HISTORY_6),
	REGNAME(hwv, 0x1F60, HWV_DATA_HISTORY_7),
	REGNAME(hwv, 0x1F64, HWV_DATA_HISTORY_8),
	REGNAME(hwv, 0x1F68, HWV_DATA_HISTORY_9),
	REGNAME(hwv, 0x1F6C, HWV_DATA_HISTORY_10),
	REGNAME(hwv, 0x1F70, HWV_DATA_HISTORY_11),
	REGNAME(hwv, 0x1F74, HWV_DATA_HISTORY_12),
	REGNAME(hwv, 0x1F78, HWV_DATA_HISTORY_13),
	REGNAME(hwv, 0x1F7C, HWV_DATA_HISTORY_14),
	REGNAME(hwv, 0x1F80, HWV_DATA_HISTORY_15),
	REGNAME(hwv, 0x1F84, HWV_IDX_POINTER),

	REGNAME(hwv, 0x060, HWV_XPU0_CG_SET_20),
	REGNAME(hwv, 0x064, HWV_XPU0_CG_CLR_20),
	REGNAME(hwv, 0x260, HWV_XPU1_CG_SET_20),
	REGNAME(hwv, 0x264, HWV_XPU1_CG_CLR_20),
	REGNAME(hwv, 0x460, HWV_XPU2_CG_SET_20),
	REGNAME(hwv, 0x464, HWV_XPU2_CG_CLR_20),
	REGNAME(hwv, 0x660, HWV_XPU3_CG_SET_20),
	REGNAME(hwv, 0x664, HWV_XPU3_CG_CLR_20),
	REGNAME(hwv, 0x860, HWV_XPU4_CG_SET_20),
	REGNAME(hwv, 0x864, HWV_XPU4_CG_CLR_20),
	REGNAME(hwv, 0xA60, HWV_XPU5_CG_SET_20),
	REGNAME(hwv, 0xA64, HWV_XPU5_CG_CLR_20),
	REGNAME(hwv, 0xC60, HWV_XPU6_CG_SET_20),
	REGNAME(hwv, 0xC64, HWV_XPU6_CG_CLR_20),
	REGNAME(hwv, 0xE60, HWV_XPU7_CG_SET_20),
	REGNAME(hwv, 0xE64, HWV_XPU7_CG_CLR_20),
	REGNAME(hwv, 0x1060, HWV_XPU8_CG_SET_20),
	REGNAME(hwv, 0x1064, HWV_XPU8_CG_CLR_20),
	REGNAME(hwv, 0x1260, HWV_XPU9_CG_SET_20),
	REGNAME(hwv, 0x1264, HWV_XPU9_CG_CLR_20),

	REGNAME(hwv, 0x1C00, HWV_VIP_CG_DONE_0),
	REGNAME(hwv, 0x1C04, HWV_VIP_CG_DONE_1),
	REGNAME(hwv, 0x1C08, HWV_VIP_CG_DONE_2),
	REGNAME(hwv, 0x1C0C, HWV_VIP_CG_DONE_3),
	REGNAME(hwv, 0x1C10, HWV_VIP_CG_DONE_4),
	REGNAME(hwv, 0x1C14, HWV_VIP_CG_DONE_5),
	REGNAME(hwv, 0x1C18, HWV_VIP_CG_DONE_6),
	REGNAME(hwv, 0x1C1C, HWV_VIP_CG_DONE_7),
	REGNAME(hwv, 0x1C20, HWV_VIP_CG_DONE_8),
	REGNAME(hwv, 0x1C24, HWV_VIP_CG_DONE_9),
	REGNAME(hwv, 0x1C28, HWV_VIP_CG_DONE_10),
	REGNAME(hwv, 0x1C2C, HWV_VIP_CG_DONE_11),
	REGNAME(hwv, 0x1C30, HWV_VIP_CG_DONE_12),
	REGNAME(hwv, 0x1C34, HWV_VIP_CG_DONE_13),
	REGNAME(hwv, 0x1C38, HWV_VIP_CG_DONE_14),
	REGNAME(hwv, 0x1C3C, HWV_VIP_CG_DONE_15),
	REGNAME(hwv, 0x1C40, HWV_VIP_CG_DONE_16),
	REGNAME(hwv, 0x1C44, HWV_VIP_CG_DONE_17),
	REGNAME(hwv, 0x1C48, HWV_VIP_CG_DONE_18),
	REGNAME(hwv, 0x1C4C, HWV_VIP_CG_DONE_19),
	REGNAME(hwv, 0x1C50, HWV_VIP_CG_DONE_20),
	REGNAME(hwv, 0x1C54, HWV_VIP_CG_DONE_21),
	REGNAME(hwv, 0x1C58, HWV_VIP_CG_DONE_22),
	REGNAME(hwv, 0x1C5C, HWV_VIP_CG_DONE_23),
	REGNAME(hwv, 0x1C60, HWV_VIP_CG_DONE_24),
	REGNAME(hwv, 0x1C64, HWV_VIP_CG_DONE_25),
	REGNAME(hwv, 0x1C68, HWV_VIP_CG_DONE_26),
	REGNAME(hwv, 0x1C6C, HWV_VIP_CG_DONE_27),
	REGNAME(hwv, 0x1C70, HWV_VIP_CG_DONE_28),
	REGNAME(hwv, 0x1C74, HWV_VIP_CG_DONE_29),
	REGNAME(hwv, 0x1C78, HWV_VIP_CG_DONE_30),
	REGNAME(hwv, 0x1C7C, HWV_VIP_CG_DONE_31),
	REGNAME(hwv, 0x1C80, HWV_VIP_CG_DONE_32),
	REGNAME(hwv, 0x1C84, HWV_VIP_CG_DONE_33),
	REGNAME(hwv, 0x1C88, HWV_VIP_CG_DONE_34),
	REGNAME(hwv, 0x1C8C, HWV_VIP_CG_DONE_35),
	REGNAME(hwv, 0x1C90, HWV_VIP_CG_DONE_36),
	REGNAME(hwv, 0x1C94, HWV_VIP_CG_DONE_37),
	REGNAME(hwv, 0x1C98, HWV_VIP_CG_DONE_38),
	REGNAME(hwv, 0x1C9C, HWV_VIP_CG_DONE_39),
	REGNAME(hwv, 0x1CA0, HWV_VIP_CG_DONE_40),
	REGNAME(hwv, 0x1CA4, HWV_VIP_CG_DONE_41),
	REGNAME(hwv, 0x1CA8, HWV_VIP_CG_DONE_42),
	REGNAME(hwv, 0x1CAC, HWV_VIP_CG_DONE_43),
	REGNAME(hwv, 0x1CB0, HWV_VIP_CG_DONE_44),
	REGNAME(hwv, 0x1CB4, HWV_VIP_CG_DONE_45),
	REGNAME(hwv, 0x1CB8, HWV_VIP_CG_DONE_46),
	REGNAME(hwv, 0x1CBC, HWV_VIP_CG_DONE_47),
	REGNAME(hwv, 0x1CC0, HWV_VIP_CG_DONE_48),
	REGNAME(hwv, 0x1CC4, HWV_VIP_CG_DONE_49),
	REGNAME(hwv, 0x1900, HWV_VIP_CG_ENABLE_0),
	REGNAME(hwv, 0x1904, HWV_VIP_CG_ENABLE_1),
	REGNAME(hwv, 0x1908, HWV_VIP_CG_ENABLE_2),
	REGNAME(hwv, 0x190C, HWV_VIP_CG_ENABLE_3),
	REGNAME(hwv, 0x1910, HWV_VIP_CG_ENABLE_4),
	REGNAME(hwv, 0x1914, HWV_VIP_CG_ENABLE_5),
	REGNAME(hwv, 0x1918, HWV_VIP_CG_ENABLE_6),
	REGNAME(hwv, 0x191C, HWV_VIP_CG_ENABLE_7),
	REGNAME(hwv, 0x1920, HWV_VIP_CG_ENABLE_8),
	REGNAME(hwv, 0x1924, HWV_VIP_CG_ENABLE_9),
	REGNAME(hwv, 0x1928, HWV_VIP_CG_ENABLE_10),
	REGNAME(hwv, 0x192C, HWV_VIP_CG_ENABLE_11),
	REGNAME(hwv, 0x1930, HWV_VIP_CG_ENABLE_12),
	REGNAME(hwv, 0x1934, HWV_VIP_CG_ENABLE_13),
	REGNAME(hwv, 0x1938, HWV_VIP_CG_ENABLE_14),
	REGNAME(hwv, 0x193C, HWV_VIP_CG_ENABLE_15),
	REGNAME(hwv, 0x1940, HWV_VIP_CG_ENABLE_16),
	REGNAME(hwv, 0x1944, HWV_VIP_CG_ENABLE_17),
	REGNAME(hwv, 0x1948, HWV_VIP_CG_ENABLE_18),
	REGNAME(hwv, 0x194C, HWV_VIP_CG_ENABLE_19),
	REGNAME(hwv, 0x1950, HWV_VIP_CG_ENABLE_20),
	REGNAME(hwv, 0x1954, HWV_VIP_CG_ENABLE_21),
	REGNAME(hwv, 0x1958, HWV_VIP_CG_ENABLE_22),
	REGNAME(hwv, 0x195C, HWV_VIP_CG_ENABLE_23),
	REGNAME(hwv, 0x1960, HWV_VIP_CG_ENABLE_24),
	REGNAME(hwv, 0x1964, HWV_VIP_CG_ENABLE_25),
	REGNAME(hwv, 0x1968, HWV_VIP_CG_ENABLE_26),
	REGNAME(hwv, 0x196C, HWV_VIP_CG_ENABLE_27),
	REGNAME(hwv, 0x1970, HWV_VIP_CG_ENABLE_28),
	REGNAME(hwv, 0x1974, HWV_VIP_CG_ENABLE_29),
	REGNAME(hwv, 0x1978, HWV_VIP_CG_ENABLE_30),
	REGNAME(hwv, 0x197C, HWV_VIP_CG_ENABLE_31),
	REGNAME(hwv, 0x1980, HWV_VIP_CG_ENABLE_32),
	REGNAME(hwv, 0x1984, HWV_VIP_CG_ENABLE_33),
	REGNAME(hwv, 0x1988, HWV_VIP_CG_ENABLE_34),
	REGNAME(hwv, 0x198C, HWV_VIP_CG_ENABLE_35),
	REGNAME(hwv, 0x1990, HWV_VIP_CG_ENABLE_36),
	REGNAME(hwv, 0x1994, HWV_VIP_CG_ENABLE_37),
	REGNAME(hwv, 0x1998, HWV_VIP_CG_ENABLE_38),
	REGNAME(hwv, 0x199C, HWV_VIP_CG_ENABLE_39),
	REGNAME(hwv, 0x19A0, HWV_VIP_CG_ENABLE_40),
	REGNAME(hwv, 0x19A4, HWV_VIP_CG_ENABLE_41),
	REGNAME(hwv, 0x19A8, HWV_VIP_CG_ENABLE_42),
	REGNAME(hwv, 0x19AC, HWV_VIP_CG_ENABLE_43),
	REGNAME(hwv, 0x19B0, HWV_VIP_CG_ENABLE_44),
	REGNAME(hwv, 0x19B4, HWV_VIP_CG_ENABLE_45),
	REGNAME(hwv, 0x19B8, HWV_VIP_CG_ENABLE_46),
	REGNAME(hwv, 0x19BC, HWV_VIP_CG_ENABLE_47),
	REGNAME(hwv, 0x19C0, HWV_VIP_CG_ENABLE_48),
	REGNAME(hwv, 0x19C4, HWV_VIP_CG_ENABLE_49),
	REGNAME(hwv, 0x1800, HWV_VIP_CG_STATUS_0),
	REGNAME(hwv, 0x1804, HWV_VIP_CG_STATUS_1),
	REGNAME(hwv, 0x1808, HWV_VIP_CG_STATUS_2),
	REGNAME(hwv, 0x180C, HWV_VIP_CG_STATUS_3),
	REGNAME(hwv, 0x1810, HWV_VIP_CG_STATUS_4),
	REGNAME(hwv, 0x1814, HWV_VIP_CG_STATUS_5),
	REGNAME(hwv, 0x1818, HWV_VIP_CG_STATUS_6),
	REGNAME(hwv, 0x181C, HWV_VIP_CG_STATUS_7),
	REGNAME(hwv, 0x1820, HWV_VIP_CG_STATUS_8),
	REGNAME(hwv, 0x1824, HWV_VIP_CG_STATUS_9),
	REGNAME(hwv, 0x1828, HWV_VIP_CG_STATUS_10),
	REGNAME(hwv, 0x182C, HWV_VIP_CG_STATUS_11),
	REGNAME(hwv, 0x1830, HWV_VIP_CG_STATUS_12),
	REGNAME(hwv, 0x1834, HWV_VIP_CG_STATUS_13),
	REGNAME(hwv, 0x1838, HWV_VIP_CG_STATUS_14),
	REGNAME(hwv, 0x183C, HWV_VIP_CG_STATUS_15),
	REGNAME(hwv, 0x1840, HWV_VIP_CG_STATUS_16),
	REGNAME(hwv, 0x1844, HWV_VIP_CG_STATUS_17),
	REGNAME(hwv, 0x1848, HWV_VIP_CG_STATUS_18),
	REGNAME(hwv, 0x184C, HWV_VIP_CG_STATUS_19),
	REGNAME(hwv, 0x1850, HWV_VIP_CG_STATUS_20),
	REGNAME(hwv, 0x1854, HWV_VIP_CG_STATUS_21),
	REGNAME(hwv, 0x1858, HWV_VIP_CG_STATUS_22),
	REGNAME(hwv, 0x185C, HWV_VIP_CG_STATUS_23),
	REGNAME(hwv, 0x1860, HWV_VIP_CG_STATUS_24),
	REGNAME(hwv, 0x1864, HWV_VIP_CG_STATUS_25),
	REGNAME(hwv, 0x1868, HWV_VIP_CG_STATUS_26),
	REGNAME(hwv, 0x186C, HWV_VIP_CG_STATUS_27),
	REGNAME(hwv, 0x1870, HWV_VIP_CG_STATUS_28),
	REGNAME(hwv, 0x1874, HWV_VIP_CG_STATUS_29),
	REGNAME(hwv, 0x1878, HWV_VIP_CG_STATUS_30),
	REGNAME(hwv, 0x187C, HWV_VIP_CG_STATUS_31),
	REGNAME(hwv, 0x1880, HWV_VIP_CG_STATUS_32),
	REGNAME(hwv, 0x1884, HWV_VIP_CG_STATUS_33),
	REGNAME(hwv, 0x1888, HWV_VIP_CG_STATUS_34),
	REGNAME(hwv, 0x188C, HWV_VIP_CG_STATUS_35),
	REGNAME(hwv, 0x1890, HWV_VIP_CG_STATUS_36),
	REGNAME(hwv, 0x1894, HWV_VIP_CG_STATUS_37),
	REGNAME(hwv, 0x1898, HWV_VIP_CG_STATUS_38),
	REGNAME(hwv, 0x189C, HWV_VIP_CG_STATUS_39),
	REGNAME(hwv, 0x18A0, HWV_VIP_CG_STATUS_40),
	REGNAME(hwv, 0x18A4, HWV_VIP_CG_STATUS_41),
	REGNAME(hwv, 0x18A8, HWV_VIP_CG_STATUS_42),
	REGNAME(hwv, 0x18AC, HWV_VIP_CG_STATUS_43),
	REGNAME(hwv, 0x18B0, HWV_VIP_CG_STATUS_44),
	REGNAME(hwv, 0x18B4, HWV_VIP_CG_STATUS_45),
	REGNAME(hwv, 0x18B8, HWV_VIP_CG_STATUS_46),
	REGNAME(hwv, 0x18BC, HWV_VIP_CG_STATUS_47),
	REGNAME(hwv, 0x18C0, HWV_VIP_CG_STATUS_48),
	REGNAME(hwv, 0x18C4, HWV_VIP_CG_STATUS_49),
	REGNAME(hwv, 0x0B98, HW_CCF_APU_MTCMOS_SET),
	REGNAME(hwv, 0x0B90, HW_CCF_APU_PLL_SET),
	REGNAME(hwv, 0x0198, HW_CCF_AP_MTCMOS_SET),
	REGNAME(hwv, 0x0190, HW_CCF_AP_PLL_SET),
	REGNAME(hwv, 0x1198, HW_CCF_GCE_MTCMOS_SET),
	REGNAME(hwv, 0x1190, HW_CCF_GCE_PLL_SET),
	REGNAME(hwv, 0x0798, HW_CCF_GPU_MTCMOS_SET),
	REGNAME(hwv, 0x0790, HW_CCF_GPU_PLL_SET),
	REGNAME(hwv, 0x1500, HW_CCF_INT_STATUS),
	REGNAME(hwv, 0x0598, HW_CCF_MD_MTCMOS_SET),
	REGNAME(hwv, 0x0590, HW_CCF_MD_PLL_SET),
	REGNAME(hwv, 0x0F98, HW_CCF_MMUP_MTCMOS_SET),
	REGNAME(hwv, 0x0F90, HW_CCF_MMUP_PLL_SET),
	REGNAME(hwv, 0x1470, HW_CCF_MTCMOS_CLR_STATUS),
	REGNAME(hwv, 0x141C, HW_CCF_MTCMOS_DONE),
	REGNAME(hwv, 0x1410, HW_CCF_MTCMOS_ENABLE),
	// REGNAME(hwv, 0x14AC, HW_CCF_MTCMOS_FLOW_FLAG_CLR),
	// REGNAME(hwv, 0x14A8, HW_CCF_MTCMOS_FLOW_FLAG_SET),
	REGNAME(hwv, 0x146C, HW_CCF_MTCMOS_SET_STATUS),
	REGNAME(hwv, 0x1414, HW_CCF_MTCMOS_STATUS),
	REGNAME(hwv, 0x1454, HW_CCF_MTCMOS_STATUS_CLR),
	REGNAME(hwv, 0x1468, HW_CCF_PLL_CLR_STATUS),
	REGNAME(hwv, 0x140C, HW_CCF_PLL_DONE),
	REGNAME(hwv, 0x1400, HW_CCF_PLL_ENABLE),
	REGNAME(hwv, 0x1464, HW_CCF_PLL_SET_STATUS),
	REGNAME(hwv, 0x1404, HW_CCF_PLL_STATUS),
	REGNAME(hwv, 0x1450, HW_CCF_PLL_STATUS_CLR),
	REGNAME(hwv, 0x1398, HW_CCF_SCP_MTCMOS_SET),
	REGNAME(hwv, 0x1390, HW_CCF_SCP_PLL_SET),
	REGNAME(hwv, 0x0D98, HW_CCF_SPM_MTCMOS_SET),
	REGNAME(hwv, 0x0D90, HW_CCF_SPM_PLL_SET),
	REGNAME(hwv, 0x0998, HW_CCF_SSPM_MTCMOS_SET),
	REGNAME(hwv, 0x0990, HW_CCF_SSPM_PLL_SET),
	REGNAME(hwv, 0x0398, HW_CCF_TEE_MTCMOS_SET),
	REGNAME(hwv, 0x0390, HW_CCF_TEE_PLL_SET),
	{},
};

static const struct regname *get_all_mt6895_regnames(void)
{
	return rn;
}

static void init_regbase(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rb) - 1; i++) {
		if (!rb[i].phys)
			continue;

		if (i == hwv)
			rb[i].virt = ioremap(rb[i].phys, 0x2000);
		else
			rb[i].virt = ioremap(rb[i].phys, 0x1000);
	}
}

/*
 * clkchk pwr_status
 */
static u32 pwr_ofs[STA_NUM] = {
	[PWR_STA] = 0xF34,
	[PWR_STA2] = 0xF38,
	[XPU_PWR_STA] = 0xF3C,
	[XPU_PWR_STA2] = 0xF40,
	[OTHER_STA] = 0x414,
};

static u32 pwr_sta[STA_NUM];

u32 *get_spm_pwr_status_array(void)
{
	static void __iomem *pwr_addr[STA_NUM];
	int i;

	for (i = 0; i < STA_NUM; i++) {
		if (pwr_ofs[i]) {
			pwr_addr[i] = rb[spm].virt + pwr_ofs[i];
			pwr_sta[i] = clk_readl(pwr_addr[i]);
		}
	}

	return pwr_sta;
}

/*
 * clkchk pwr_msk
 */
static struct pvd_msk pvd_pwr_mask[] = {
	{"topckgen", PWR_STA, 0x00000000},
	{"apmixedsys", PWR_STA, 0x00000000},
	{"vlp_cksys", PWR_STA, 0x00000000},
	{"mfgpll_pll_ctrl", PWR_STA, 0x00000000},
	{"mfgscpll_pll_ctrl", PWR_STA, 0x00000000},
	{"apupll_pll_ctrl", PWR_STA, 0x00000000},
	{"npupll_pll_ctrl", PWR_STA, 0x00000000},
	{"apupll1_pll_ctrl", PWR_STA, 0x00000000},
	{"afe", PWR_STA, 0x00000020},
	{"camsys_mraw", PWR_STA, 0x00040000},
	{"camsys_rawa", PWR_STA, 0x00080000},
	{"camsys_rawb", PWR_STA, 0x00100000},
	{"camsys_rawc", PWR_STA, 0x00200000},
	{"camsys_yuva", PWR_STA, 0x00080000},
	{"camsys_yuvb", PWR_STA, 0x00100000},
	{"camsys_yuvc", PWR_STA, 0x00200000},
	{"cam_main_r1a", PWR_STA, 0x00020000},
	{"ccu", PWR_STA, 0x00020000},
	{"dip_nr_dip1", PWR_STA, 0x00000400},
	{"dip_top_dip1", PWR_STA, 0x00000400},
	{"imgsys_main", PWR_STA, 0x00000200},
	{"imp_iic_wrap_c", PWR_STA, 0x00000000},
	{"imp_iic_wrap_s", PWR_STA, 0x00000000},
	{"imp_iic_wrap_w", PWR_STA, 0x00000000},
	{"infracfg_ao", PWR_STA, 0x00000000},
	{"ipesys", PWR_STA, 0x00000800},
	{"mdpsys1", PWR_STA, 0x01000000},
	{"mdpsys", PWR_STA, 0x00800000},
	{"mfg", PWR_STA, 0x00000000},
	{"mminfra_config", PWR_STA, 0x08000000},
	{"mmsys0", PWR_STA, 0x02000000},
	{"mmsys1", PWR_STA, 0x04000000},
	{"nemi_reg", PWR_STA, 0x00000000},
	{"pericfg_ao", PWR_STA, 0x00000000},
	{"semi_reg", PWR_STA, 0x00000000},
	{"ssusb_device", PWR_STA, 0x00000000},
	{"ssusb_sifslv_ippc", PWR_STA, 0x00000000},
	{"ssusb_sifslv_ippc_p1", PWR_STA, 0x00000000},
	{"ufs_ao_config", PWR_STA, 0x00000000},
	{"ufs_pdn_cfg", PWR_STA, 0x00000000},
	{"vdec_gcon_base", PWR_STA, 0x00004000},
	{"vdec_soc_gcon_base", PWR_STA, 0x00002000},
	{"vencsys", PWR_STA, 0x00008000},
	{"vencsys_c1", PWR_STA, 0x00010000},
	{"wpe1_dip1", PWR_STA, 0x00000400},
	{"wpe2_dip1", PWR_STA, 0x00000400},
	{"wpe3_dip1", PWR_STA, 0x00000400},
	{},
};

static struct pvd_msk *get_pvd_pwr_mask(void)
{
	return pvd_pwr_mask;
}

/*
 * clkchk vf table
 */



static int get_vcore_opp(void)
{
#if IS_ENABLED(CONFIG_MTK_DVFSRC_HELPER) && CHECK_VCORE_FREQ
	return mtk_dvfsrc_query_opp_info(MTK_DVFSRC_SW_REQ_VCORE_OPP);
#else
	return VCORE_NULL;
#endif
}

static void set_mt6895_reg_value(u32 id, u32 ofs, u32 val)
{
	if (id >= chk_sys_num)
		return;

	clk_writel(rb[id].virt + ofs, val);
}

void print_subsys_reg_mt6895(enum chk_sys_id id)
{
	struct regbase *rb_dump;
	const struct regname *rns = &rn[0];
	int i;

	if (id >= chk_sys_num) {
		pr_info("wrong id:%d\n", id);
		return;
	}

	if (id == hwv)
		set_mt6895_reg_value(hwv, HWV_DOMAIN_KEY, HWV_SECURE_KEY);

	rb_dump = &rb[id];

	for (i = 0; i < ARRAY_SIZE(rn) - 1; i++, rns++) {
		if (!is_valid_reg(ADDR(rns)))
			return;

		/* filter out the subsys that we don't want */
		if (rns->base != rb_dump)
			continue;

		pr_info("%-18s: [0x%08x] = 0x%08x\n",
			rns->name, PHYSADDR(rns), clk_readl(ADDR(rns)));
	}
}
EXPORT_SYMBOL(print_subsys_reg_mt6895);

#if IS_ENABLED(CONFIG_DEVICE_MODULES_MTK_DEVAPC)
static void devapc_dump(void)
{
	print_subsys_reg_mt6895(spm);
	print_subsys_reg_mt6895(top);
	print_subsys_reg_mt6895(infracfg);
	print_subsys_reg_mt6895(apmixed);
	print_subsys_reg_mt6895(mfg_ao);
	print_subsys_reg_mt6895(mfgsc_ao);
	print_subsys_reg_mt6895(apu0_ao);
	print_subsys_reg_mt6895(npu_ao);
	print_subsys_reg_mt6895(apu1_ao);
	print_subsys_reg_mt6895(vlpcfg);
	print_subsys_reg_mt6895(vlp_ck);
}

static struct devapc_vio_callbacks devapc_vio_handle = {
	.id = DEVAPC_SUBSYS_CLKMGR,
	.debug_dump = devapc_dump,
};

#endif

static const char * const off_pll_names[] = {
	"univpll",
	"msdcpll",
	"mmpll",
	"tvdpll",
	"imgpll",
	"mfg_ao_mfgpll",
	"mfgsc_ao_mfgscpll",
	"apu0_ao_apupll",
	"npu_ao_npupll",
	"apu1_ao_apupll1",
	NULL
};

static const char * const notice_pll_names[] = {
	"adsppll",
	"apll1",
	"apll2",
	NULL
};

static const char * const *get_off_pll_names(void)
{
	return off_pll_names;
}

static const char * const *get_notice_pll_names(void)
{
	return notice_pll_names;
}

static bool is_pll_chk_bug_on(void)
{
#if BUG_ON_CHK_ENABLE
	return true;
#endif
	return false;
}


static bool is_cg_chk_pwr_on(void)
{
#if CG_CHK_PWRON_ENABLE
	return true;
#endif
	return false;
}



/*
 * init functions
 */

static struct clkchk_ops clkchk_mt6895_ops = {
	.get_all_regnames = get_all_mt6895_regnames,
	.get_spm_pwr_status_array = get_spm_pwr_status_array,
	.get_pvd_pwr_mask = get_pvd_pwr_mask,
	.get_off_pll_names = get_off_pll_names,
	.get_notice_pll_names = get_notice_pll_names,
	.is_pll_chk_bug_on = is_pll_chk_bug_on,
	//.get_vf_table = get_vf_table,
	.get_vcore_opp = get_vcore_opp,
#if IS_ENABLED(CONFIG_DEVICE_MODULES_MTK_DEVAPC)
	.devapc_dump = devapc_dump,
#endif
	//.dump_hwv_history = dump_hwv_history,
	.is_cg_chk_pwr_on = is_cg_chk_pwr_on,
	//.dump_hwv_pll_reg = dump_hwv_pll_reg,
	//.suspend_retry = suspend_retry,
};

static int clk_chk_mt6895_probe(struct platform_device *pdev)
{

	init_regbase();

	set_clkchk_notify();

	set_clkchk_ops(&clkchk_mt6895_ops);

#if IS_ENABLED(CONFIG_DEVICE_MODULES_MTK_DEVAPC)
	register_devapc_vio_callback(&devapc_vio_handle);
#endif

	return 0;
}

static const struct of_device_id of_match_clkchk_mt6895[] = {
	{
		.compatible = "mediatek,mt6895-clkchk",
	}, {
		/* sentinel */
	}
};

static struct platform_driver clk_chk_mt6895_drv = {
	.probe = clk_chk_mt6895_probe,
	.driver = {
		.name = "clk-chk-mt6895",
		.owner = THIS_MODULE,
		.pm = &clk_chk_dev_pm_ops,
		.of_match_table = of_match_clkchk_mt6895,
	},
};

/*
 * init functions
 */

static int __init clkchk_mt6895_init(void)
{
	return platform_driver_register(&clk_chk_mt6895_drv);
}

static void __exit clkchk_mt6895_exit(void)
{
	platform_driver_unregister(&clk_chk_mt6895_drv);
}

subsys_initcall(clkchk_mt6895_init);
module_exit(clkchk_mt6895_exit);
MODULE_LICENSE("GPL");
