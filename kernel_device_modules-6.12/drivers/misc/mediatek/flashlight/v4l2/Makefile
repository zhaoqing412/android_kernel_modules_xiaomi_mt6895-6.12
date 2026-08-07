#obj-$(CONFIG_MTK_VIDEO_LM3643)     += lm3643.o
#obj-$(CONFIG_MTK_VIDEO_LM3644)     += lm3644.o
obj-$(CONFIG_MTK_VIDEO_KTD2687)     += ktd2687.o
MTK_CUSTOM_PATH := $(DEVICE_MODULES_PATH)/drivers/misc/mediatek
subdir-ccflags-y += \
	-I$(MTK_CUSTOM_PATH)/flashlight
subdir-ccflags-y += -D__XIAOMI_CAMERA__
