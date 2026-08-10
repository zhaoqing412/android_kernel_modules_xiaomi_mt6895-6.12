# SPDX-License-Identifier: GPL-2.0
obj-m += pd_dbg_info.o

# xaga: platform module wiring (alps BUILD.bazel tcpc_class; exports
# register_tcp_dev_notifier / tcpm_* for mtk_pd_adapter / mtk_chg_type_det)
ccflags-y += -DCONFIG_RT_REGMAP
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/power/supply/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/mfd/
obj-m += tcpc_class.o
tcpc_class-y := rt-regmap.o tcpci.o tcpci_alert.o tcpci_core.o \
		tcpci_timer.o tcpci_typec.o tcpm.o
tcpc_class-$(CONFIG_USB_POWER_DELIVERY) += pd_core.o pd_dpm_alt_mode_dp.o \
		pd_dpm_core.o pd_dpm_pdo_select.o pd_dpm_reaction.o \
		pd_policy_engine.o pd_policy_engine_com.o pd_policy_engine_dbg.o \
		pd_policy_engine_dfp.o pd_policy_engine_dr.o pd_policy_engine_drs.o \
		pd_policy_engine_prs.o pd_policy_engine_snk.o pd_policy_engine_src.o \
		pd_policy_engine_ufp.o pd_policy_engine_vcs.o pd_process_evt.o \
		pd_process_evt_com.o pd_process_evt_dbg.o pd_process_evt_drs.o \
		pd_process_evt_prs.o pd_process_evt_snk.o pd_process_evt_src.o \
		pd_process_evt_tcp.o pd_process_evt_vcs.o pd_process_evt_vdm.o \
		tcpci_event.o

# xaga: platform module wiring
obj-m += rt_pd_manager.o

# xaga: platform module wiring
obj-m += tcpci_late_sync.o

# xaga: platform module wiring
obj-m += tcpc_mt6375.o
