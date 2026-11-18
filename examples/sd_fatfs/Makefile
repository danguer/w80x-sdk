TARGET := sd_fatfs
SRCS   := main.c
include ../lib/fatfs/fatfs.mk
# SD card bus: spi (default, LSPI) or sdio (SDIO host, 4-bit):
#   make SDCARD_BUS=sdio
SDCARD_BUS ?= spi
include ../lib/sdcard/sdcard_$(SDCARD_BUS).mk
include ../../w80x/w80x.mk
