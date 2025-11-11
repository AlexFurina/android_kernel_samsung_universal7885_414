# SPDX-License-Identifier: GPL-2.0
dtbo-y += samsung/exynos7884B-a10eu_jpn_dcm_01.dtbo
dtbo-y += samsung/exynos7884B-a10eu_jpn_dcm_00.dtbo
dtb-y += exynos/exynos7885.dtb

targets += dtbs
DTB_LIST  := $(dtb-y) $(dtbo-y)
always    := $(DTB_LIST)

dtbs: $(addprefix $(obj)/, $(DTB_LIST))

clean-files := *.dtb*
