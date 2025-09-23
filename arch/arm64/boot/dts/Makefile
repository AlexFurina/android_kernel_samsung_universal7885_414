# SPDX-License-Identifier: GPL-2.0


dtb-$(CONFIG_MACH_EXYNOS7885_JACKPOTLTE_GLOBAL) := \
    exynos/exynos7885.dtb \
    exynos/exynos7885_4.4_bl.dtb \
    exynos/exynos7885-jackpotlte_eur_open_07.dtb

targets += dtbs
DTB_LIST  := $(dtb-y) $(dtbo-y)
always    := $(DTB_LIST)

dtbs: $(addprefix $(obj)/, $(DTB_LIST))

clean-files := *.dtb*
