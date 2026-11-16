# fatfs.mk - FatFs R0.16 (ChaN) with the sdcard.h disk layer.
#
# In a project Makefile, before including w80x.mk:
#
#     SRCS := main.c
#     include ../lib/fatfs/fatfs.mk
#     include ../lib/sdcard/sdcard_spi.mk     # one sdcard backend
#     include ../../w80x/w80x.mk
#
# ff.c, ffunicode.c and ffconf.h are the R0.16 release; ffconf.h has the
# W80x SDK settings listed at its top. diskio_sdcard.c is the glue.

FATFS_DIR := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))

SRCS += $(FATFS_DIR)/ff.c $(FATFS_DIR)/ffunicode.c $(FATFS_DIR)/diskio_sdcard.c
override CFLAGS += -I$(FATFS_DIR)

# ChaN's code trips some -Wextra warnings; keep them out of the build log.
BUILD ?= build
$(BUILD)/ff.o $(BUILD)/ffunicode.o: override CFLAGS += -Wno-unused-parameter -Wno-sign-compare
