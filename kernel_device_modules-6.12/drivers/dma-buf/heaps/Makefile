# SPDX-License-Identifier: GPL-2.0
# xaga: platform module wiring (alps BUILD.bazel system_heap; exports
# dma_buf_get_gid for mediatek-drm)
ccflags-y += -I$(src)
obj-m += system_heap.o
system_heap-y := mtk-deferred-free-helper.o mtk_cache_pool.o \
	mtk_dmabuf_monitor.o mtk_page_pool.o mtk_system_heap.o
