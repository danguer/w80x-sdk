# w80x.mk - shared build rules for W80x projects.
#
# Minimal project Makefile:
#
#     TARGET := blink
#     include ../../w80x/w80x.mk
#
# Optional variables (set before the include, or on the command line):
#     SRCS       extra sources (default: all *.c / *.S in the project dir)
#     PREFIX     toolchain prefix (auto-detected: csky-unknown-elf-, csky-elfabiv2-, csky-elf-)
#     OPT        optimisation flags (default: -Os)
#     PORT       serial port for flashing (default: /dev/ttyUSB0)
#     BAUD       download baud rate (default: 2000000)
#     SECBOOT    path to w800_secboot.img, only needed for `make fls` (BOOT=secboot only)
#     BOOT       secboot (default) or iot: which bootloader's flash layout to target
#
# Targets: all (elf/bin/img), fls, flash, flash-fls, monitor, size, disasm, clean

W80X_DIR  := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
TOOLS_DIR := $(W80X_DIR)/../tools

TARGET   ?= app
BUILD    ?= build

# ---- toolchain --------------------------------------------------------------
PREFIX_CANDIDATES := csky-unknown-elf- csky-elfabiv2- csky-elf- csky-abiv2-elf-
PREFIX ?= $(firstword $(foreach p,$(PREFIX_CANDIDATES),$(if $(shell command -v $(p)gcc 2>/dev/null),$(p))))
ifeq ($(strip $(PREFIX)),)
  ifeq ($(filter clean monitor,$(MAKECMDGOALS)),)
    $(error No C-SKY toolchain found in PATH (tried: $(PREFIX_CANDIDATES)). See README.md, or pass PREFIX=/path/to/bin/csky-xxx-)
  endif
endif

CC      := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
OBJDUMP := $(PREFIX)objdump
SIZE    := $(PREFIX)size
PYTHON  ?= python3

# ---- image layout (must match the bootloader's expectations) ----------------
# BOOT=secboot  stock/factory secboot, also wm_sdk_w80x (header 0x080D0000)
# BOOT=iot      bootloader of the official wm_iot_sdk ("app" partition at 0xF000)
BOOT ?= secboot
ifeq ($(BOOT),iot)
  IMG_HEADER_ADDR ?= 0x0800F000
  IMG_UPD_ADDR    ?= 0x0
else ifneq ($(BOOT),secboot)
  $(error BOOT must be 'secboot' or 'iot')
endif
IMG_HEADER_ADDR ?= 0x080D0000
IMG_RUN_ADDR    ?= $(shell printf '0x%08X' $$(( $(IMG_HEADER_ADDR) + 0x400 )))
IMG_UPD_ADDR    ?= 0x08010000
IMG_VERSION     ?= 1.0.0

# ---- flags --------------------------------------------------------------------
ARCH_FLAGS := -mcpu=ck804ef -mhard-float
OPT        ?= -Os
override CFLAGS  += $(ARCH_FLAGS) $(OPT) -g -std=gnu11 -Wall -Wextra \
              -ffunction-sections -fdata-sections -ffreestanding \
              -I$(W80X_DIR) -I.
override ASFLAGS += $(ARCH_FLAGS) -g -I$(W80X_DIR)
LDSCRIPT   := $(BUILD)/w80x.ld
override LDFLAGS += $(ARCH_FLAGS) -nostartfiles -T$(LDSCRIPT) \
              -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map -Wl,--print-memory-usage
LDLIBS     += -lc -lgcc

# ---- sources ------------------------------------------------------------------
SRCS ?= $(wildcard *.c) $(wildcard *.S)
SDK_SRCS := $(W80X_DIR)/startup.S $(W80X_DIR)/system.c $(W80X_DIR)/w80x_clock.c \
            $(W80X_DIR)/w80x_stdio.c

OBJS := $(addprefix $(BUILD)/,$(notdir $(addsuffix .o,$(basename $(SRCS) $(SDK_SRCS)))))
vpath %.c $(sort $(dir $(SRCS) $(SDK_SRCS)))
vpath %.S $(sort $(dir $(SRCS) $(SDK_SRCS)))

ELF := $(BUILD)/$(TARGET).elf
BIN := $(BUILD)/$(TARGET).bin
IMG := $(BUILD)/$(TARGET).img
FLS := $(BUILD)/$(TARGET).fls

PORT ?= /dev/ttyUSB0
BAUD ?= 2000000

# ---- rules --------------------------------------------------------------------
.PHONY: all fls flash flash-fls monitor size disasm clean
all: $(IMG) size

$(BUILD):
	@mkdir -p $@

# Image layout of the last build. Rewritten only when BOOT/IMG_* change, so
# switching BOOT relinks and rebuilds the image instead of reusing the old one.
LAYOUT := $(BUILD)/layout.txt
LAYOUT_NOW := header=$(IMG_HEADER_ADDR) run=$(IMG_RUN_ADDR) upd=$(IMG_UPD_ADDR) version=$(IMG_VERSION)
ifneq ($(LAYOUT_NOW),$(shell cat $(LAYOUT) 2>/dev/null))
  $(shell mkdir -p $(BUILD) && echo '$(LAYOUT_NOW)' > $(LAYOUT))
endif

$(LDSCRIPT): $(W80X_DIR)/w80x.ld $(MAKEFILE_LIST) $(LAYOUT) | $(BUILD)
	$(CC) -E -P -undef -x c -DIMG_RUN_ADDR=$(IMG_RUN_ADDR) $< -o $@

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/%.o: %.S | $(BUILD)
	$(CC) $(ASFLAGS) -MMD -MP -c $< -o $@

$(ELF): $(OBJS) $(LDSCRIPT)
	$(CC) $(LDFLAGS) $(OBJS) $(LDLIBS) -o $@

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

$(IMG): $(BIN) $(LAYOUT)
	$(PYTHON) $(TOOLS_DIR)/w80x_img.py img $< -o $@ \
	    --header $(IMG_HEADER_ADDR) --run $(IMG_RUN_ADDR) --upd $(IMG_UPD_ADDR) --version $(IMG_VERSION)

fls: $(FLS)
$(FLS): $(IMG)
	@test "$(BOOT)" = secboot || { echo "make fls is for BOOT=secboot only. With BOOT=iot, install the wm_iot_sdk"; \
	    echo "bootloader once by flashing a wm_iot_sdk .fls, then use 'make BOOT=iot flash' (.img only)"; exit 1; }
	@test -n "$(SECBOOT)" || { echo "Set SECBOOT=/path/to/w800_secboot.img (from the WinnerMicro SDK)"; exit 1; }
	$(PYTHON) $(TOOLS_DIR)/w80x_img.py fls $(SECBOOT) $< -o $@

flash: $(IMG)
	$(PYTHON) $(TOOLS_DIR)/w80x_flash.py -p $(PORT) -b $(BAUD) $<

flash-fls: $(FLS)
	$(PYTHON) $(TOOLS_DIR)/w80x_flash.py -p $(PORT) -b $(BAUD) $<

monitor:
	$(PYTHON) $(TOOLS_DIR)/w80x_flash.py -p $(PORT) --monitor-only

size: $(ELF)
	@$(SIZE) $<

disasm: $(ELF)
	$(OBJDUMP) -d -S $< > $(BUILD)/$(TARGET).lst

clean:
	rm -rf $(BUILD)

-include $(OBJS:.o=.d)
