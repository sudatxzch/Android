 # Copyright (C) 2010 Spreadtrum . All Rights Reserved. 


#
# SPRD Build System
#
BUILD_SPRD_PLATFORM = sc8810

BUILD_KERNE_MACHINE  = CONFIG_MACH_SP8810

BUILD_KERNEL_VERSION = kernel

BUILD_UBOOT_VERSION  = u-boot

#
# Customize for customer driver(configured according to your need)
#
3RDPARTY_BLUETOOTH = BC6888

3RDPARTY_CAMERA = ov2655:ov5640:gc0309:gt2005:nt99250:hi253:hi704:sid130b  #:hi253:hi704


3RDPARTY_LCD = ili9481:ili9486:r61581b_CS:hx8357_CM    #ili9486l_YS

3RDPARTY_GSENSOR = bma250

#3RDPARTY_MSENSOR = akm8975

3RDPARTY_LSENSOR = apds990x

3RDPARTY_GPS  = gsd4t

3RDPARTY_TP   = FT5206 #FT5206/PIXCIR

3RDPARTY_WIFI = UNIFI6030

3RDPARTY_FM = rda5802

#3RDPARTY_ATV = nmi601

# Not support multi language
3RDPARTY_APP = app8810
# Support multi language
#3RDPARTY_APP = app8810:app8810multilanguage

3RDPARTY_ANIM = poweranim


#
# SPRD COMMON MODULE(dedicated!!!.should not change)
#
3RDPARTY_TOOLS = iperf-2.0.4:wireless_tools.29:tools-binary:testjar

3RDPARTY_HEADSET = headset-soc

3RDPARTY_AUDIO = snd_dummy_alsa_audio

3RDPARTY_MEDIASERVER = mediaserver_listener

3RDPARTY_FIREWALL= yeezone


3RDPARTY_UBOOT = uboot

#3RDPARTY_CMMB = IF238

3RDPARTY_CMCC = CMCC

3RDPARTY_GPU = mali



#
# SPRD KERNEL FEATUR CONTROL(configured according to your need)
#
#CONFIG_INPUT_TOUCHSCREEN = yes
CONFIG_CHUANQI_T100 = yes
CONFIG_EXTERNAL_LCD_BACKLIGHT = yes

#
# SPRD APP FEATUR CONTROL(configured according to your need)
#
#SPRD_APP_USE_TASKMANAGER = no
CONFIG_RGK_DEFAULT = yes

#
# RGK configuration
#
RGK_CAMERA_CONFIG = 2M  #5M  3M  2M VGA
RGK_FRONT_CAMERA_CONFIG = VGA   #2M VGA

RGK_INTERNAL_VERSION = T100_BASE_W1217_V0.7.4_$(shell date +%Y%m%d)  #zhucaihua add internal version number
RGK_INTERNAL_PROJECT = T100_BASE_BOM
