# SPDX-License-Identifier: GPL-2.0
dtb-y += exynos/exynos7885.dtb
dtbo-y += exynos/a40/exynos7904-a40_eur_open_01.dtbo
dtbo-y += exynos/xcover4s/exynos7884-xcover4s_eur_open_02.dtbo

targets += dtbs
DTB_LIST  := $(dtb-y) $(dtbo-y)
always    := $(DTB_LIST)

dtbs: $(addprefix $(obj)/, $(DTB_LIST))

clean-files := *.dtb*
