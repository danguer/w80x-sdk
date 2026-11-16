# sdcard_spi.mk - SD card in SPI mode (sdcard.h API on the LSPI).
#
#     include ../lib/sdcard/sdcard_spi.mk
#
# Pins and clock are set with -D options, see the top of sdcard_spi.c.

SDCARD_DIR := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))

SRCS += $(SDCARD_DIR)/sdcard.c $(SDCARD_DIR)/sdcard_spi.c
override CFLAGS += -I$(SDCARD_DIR)
