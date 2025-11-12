obj-$(CONFIG_EXYNOS_DECON_LCD_S6E3FA0)	+= s6e3fa0_mipi_lcd.o s6e3fa0_lcd_ctrl.o
obj-$(CONFIG_EXYNOS_DECON_LCD_S6E3FA3)	+= s6e3fa3_mipi_lcd.o s6e3fa3_lcd_ctrl.o
obj-$(CONFIG_EXYNOS_DECON_LCD_S6E3HA2K)	+= s6e3ha2k_mipi_lcd.o s6e3ha2k_lcd_ctrl.o
obj-$(CONFIG_EXYNOS_DECON_LCD_S6E3HF4)	+= s6e3hf4_mipi_lcd.o s6e3hf4_lcd_ctrl.o
obj-$(CONFIG_EXYNOS_DECON_LCD_EMUL_DISP) += emul_disp_mipi_lcd.o emul_disp_lcd_ctrl.o
obj-$(CONFIG_EXYNOS_DECON_LCD_S6E3FA7)	+= s6e3fa7_mipi_lcd.o s6e3fa7_lcd_ctrl.o

obj-y += ../decon_board.o ../decon_notify.o ../decon_abd.o dsim_panel.o
obj-$(CONFIG_DISPLAY_USE_INFO) += dpui.o
obj-$(CONFIG_EXYNOS_DECON_MDNIE) += mdnie.o
obj-$(CONFIG_SUPPORT_POC_FLASH) += poc.o

obj-$(and $(CONFIG_EXYNOS_DECON_MDNIE),$(CONFIG_SMCDSD_LCD_DEBUG),$(subst undefined,y,$(origin CONFIG_SAMSUNG_PRODUCT_SHIP))) += dd_mdnie.o
obj-$(and $(CONFIG_SMCDSD_LCD_DEBUG),$(subst undefined,y,$(origin CONFIG_SAMSUNG_PRODUCT_SHIP))) += dd_lcd.o dd_backlight.o dd_dpu.o dd_param.o

obj-$(CONFIG_EXYNOS_DECON_LCD_HX83102D_A10EU)	+= hx83102d_a10eu_lcd_ctrl.o
obj-$(CONFIG_EXYNOS_DECON_LCD_S6E3FA7_JACKPOT2)	+= s6e3fa7_jackpot_lcd_ctrl.o dynamic_aid.o
obj-$(CONFIG_EXYNOS_DECON_LCD_S6E3FA7_JACKPOT)	+= s6e3fa7_jackpot_lcd_ctrl.o dynamic_aid.o
