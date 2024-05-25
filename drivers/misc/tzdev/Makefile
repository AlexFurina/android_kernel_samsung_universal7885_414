subdir-ccflags-y += -I$(srctree)/$(src)/include
subdir-ccflags-y += -I$(srctree)/$(src)
ccflags-$(CONFIG_TZDEV_DEPLOY_TZAR) += -Wa,-I$(srctree)/$(src)

obj-$(CONFIG_TZDEV)			+= lib/
obj-$(CONFIG_TZDEV)			+= core/
obj-$(CONFIG_TZDEV)			+= teec/
obj-$(CONFIG_TZDEV)			+= debug/
obj-$(CONFIG_TZDEV)			+= extensions/

obj-$(CONFIG_TZDEV_DEBUG)		+= tests/

ccflags-$(CONFIG_TZ_PMF) += -DCONFIG_TZ_PMF
