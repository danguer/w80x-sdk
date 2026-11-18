# sdcard_sdio.mk - SD card on the SDIO host (sdcard.h API, SD bus mode).
#
#     include ../lib/sdcard/sdcard_sdio.mk
#
# Pins, bus width and clock are set with -D options, see the top of
# sdcard_sdio.c.

SDCARD_DIR := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))

SRCS += $(SDCARD_DIR)/sdcard.c $(SDCARD_DIR)/sdcard_sdio.c
override CFLAGS += -I$(SDCARD_DIR)
