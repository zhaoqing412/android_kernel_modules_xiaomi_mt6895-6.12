#
# Makefile for the Novatek NT36xxx touchscreen driver.
#

# Each configuration option enables a list of files.

ccflags-$(CONFIG_DRM_MEDIATEK_V2) += -I$(DEVICE_MODULES_PATH)/drivers/gpu/drm/mediatek/mediatek_v2/
obj-$(CONFIG_TOUCHSCREEN_NVT36672C_HOSTDL_SPI) += nvt36672c.o
nvt36672c-y += nt36xxx.o
nvt36672c-y += nt36xxx_fw_update.o
nvt36672c-y += nt36xxx_ext_proc.o
nvt36672c-y += nt36xxx_mp_ctrlram.o
