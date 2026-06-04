# =========================================================================
#  SpecTalk ZX — Sprinter DSS port
#  Build with the SDCC 4.5 Sprinter SDK -> SPECTALK.EXE (DSS executable)
# =========================================================================
#  Targets:
#    make            Build SPECTALK.EXE
#    make deploy      Build EXE + write a FAT12 floppy image to the path the
#                     MAME launcher (306_sdcc450.sh) already mounts
#    make clean       Remove build artifacts
#
#  Verify in MAME:
#    cd <mame_release dir> && ./306_sdcc450.sh
#    then at the DSS prompt switch to the floppy drive and run  SPECTALK
# =========================================================================

SDK     ?= /Users/dmitry/dev/zx/sprinter/sdcc45-sprinter-sdk
SDCC    ?= sdcc
PYTHON  ?= python3

APP       ?= SPECTALK
CODE_LOC  ?= 0x4100
STACK     ?= 0xBFFF

SRCDIR  = src
BUILD   = build
# Add new .c files here as the port grows (hal/, net/, irc/ ...).
SRCS    = $(SRCDIR)/main.c

OBJS    = $(patsubst $(SRCDIR)/%.c,$(BUILD)/%.rel,$(SRCS))

SDCC_TARGET = -mz80
SDCC_OPT    = --max-allocs-per-node 5000 --opt-code-speed
CFLAGS      = $(SDCC_TARGET) $(SDCC_OPT) -I$(SDK)/include -Iinclude

CRT0    = $(SDK)/build/crt0.rel
SPRLIB  = $(SDK)/build/sprinter.lib

# Floppy image the launcher mounts as flop2
DEPLOY_IMG ?= $(SDK)/build/sprinter.img

.PHONY: all deploy clean

all: $(APP).EXE

$(BUILD)/%.rel: $(SRCDIR)/%.c
	@mkdir -p $(@D)
	$(SDCC) $(CFLAGS) -c -o $@ $<

$(BUILD)/$(APP).ihx: $(CRT0) $(OBJS)
	$(SDCC) $(SDCC_TARGET) --no-std-crt0 --code-loc $(CODE_LOC) $(SDCC_OPT) \
		$(CRT0) $(OBJS) -l$(SPRLIB) -o $@

$(APP).EXE: $(BUILD)/$(APP).ihx
	$(PYTHON) $(SDK)/tools/ihx2exe.py $< $@ --load $(CODE_LOC) --entry $(CODE_LOC) --stack $(STACK)
	@echo "Built: $@"

# Build a fresh FAT12 floppy containing just SPECTALK.EXE and drop it where
# 306_sdcc450.sh expects it. Requires mtools.
deploy: $(APP).EXE
	dd if=/dev/zero of=$(DEPLOY_IMG) bs=512 count=2880 2>/dev/null
	MTOOLS_NO_VFAT=1 mformat -f 1440 -v SPECTALK -i $(DEPLOY_IMG) ::
	mcopy -i $(DEPLOY_IMG) $(APP).EXE ::$(APP).EXE
	@echo "Deployed $(APP).EXE -> $(DEPLOY_IMG)"
	@mdir -i $(DEPLOY_IMG) ::

clean:
	rm -rf $(BUILD) $(APP).EXE *.ihx *.map *.noi *.lk *.sym
