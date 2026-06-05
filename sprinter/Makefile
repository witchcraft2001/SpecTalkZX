# =========================================================================
#  SpecTalk ZX — Sprinter DSS port
#  Build with the SDCC 4.5 Sprinter SDK. Produces one .EXE per app.
# =========================================================================
#  Targets:
#    make            Build all .EXE files
#    make deploy      Build + write a FAT12 floppy (distr/spectalk.img) with the
#                     EXEs and a sample NET.CFG
#    make clean       Remove build artifacts
#
#  Verify in MAME:
#    ./run.sh         (boots MAME with distr/spectalk.img mounted as flop2)
#    then at the DSS prompt switch to the floppy drive and run SPECTALK / NETDIAG
# =========================================================================

SDK     ?= /Users/dmitry/dev/zx/sprinter/sdcc45-sprinter-sdk
SDCC    ?= sdcc
PYTHON  ?= python3

# Use the SDK's crt0_page2: it DSS-allocates a 16K page, maps it to WIN2, and
# moves the stack to 0xBFFF before touching data. That gives the process a real
# 32K split — code in WIN1 (0x4200..0x7FFF), data+stack in the owned WIN2
# (0x8000..0xBFFF) — so data at 0x8000 is safe (the earlier corruption was from
# using WIN2 WITHOUT allocating it). crt0_page2 also zeroes _DATA at startup.
CRT0_NAME ?= crt0_page2
CODE_LOC  ?= 0x4200
DATA_LOC  ?= 0x8000
STACK     ?= 0x40FF      # EXE-header bootstrap SP; crt0 switches to 0xBFFF

SRCDIR  = src
BUILD   = build
DISTR   = distr

SDCC_TARGET = -mz80
SDCC_OPT    = --max-allocs-per-node 5000 --opt-code-speed
CFLAGS      = $(SDCC_TARGET) $(SDCC_OPT) -I$(SDK)/include -Iinclude
CRT0    = $(SDK)/build/$(CRT0_NAME).rel
SPRLIB  = $(SDK)/build/sprinter.lib

# --- Apps: <NAME>_OBJS lists the .rel objects for each EXE -----------------
APPS = SPECTALK NETDIAG NETIRC
SPECTALK_OBJS = $(BUILD)/main.rel    $(BUILD)/term.rel  $(BUILD)/irc.rel \
                $(BUILD)/net_esp.rel $(BUILD)/netcfg.rel $(BUILD)/isauart.rel $(BUILD)/uart.rel
NETDIAG_OBJS  = $(BUILD)/netdiag.rel $(BUILD)/netcfg.rel $(BUILD)/isauart.rel
NETIRC_OBJS   = $(BUILD)/netirc.rel  $(BUILD)/netcfg.rel $(BUILD)/isauart.rel $(BUILD)/uart.rel

EXES = $(addsuffix .EXE,$(APPS))
DEPLOY_IMG ?= $(DISTR)/spectalk.img

.PHONY: all deploy clean
all: $(EXES)

$(BUILD)/%.rel: $(SRCDIR)/%.c
	@mkdir -p $(@D)
	$(SDCC) $(CFLAGS) -c -o $@ $<

# Link + convert to DSS EXE. $@ = SPECTALK.EXE; objects from <NAME>_OBJS.
%.EXE:
	$(SDCC) $(SDCC_TARGET) --no-std-crt0 --code-loc $(CODE_LOC) --data-loc $(DATA_LOC) $(SDCC_OPT) \
		$(CRT0) $($(basename $@)_OBJS) -l$(SPRLIB) -o $(BUILD)/$(basename $@).ihx
	$(PYTHON) $(SDK)/tools/ihx2exe.py $(BUILD)/$(basename $@).ihx $@ \
		--load $(CODE_LOC) --entry $(CODE_LOC) --stack $(STACK)
	@echo "Built: $@"

SPECTALK.EXE: $(CRT0) $(SPECTALK_OBJS)
NETDIAG.EXE:  $(CRT0) $(NETDIAG_OBJS)
NETIRC.EXE:   $(CRT0) $(NETIRC_OBJS)

# FAT12 floppy with all EXEs + sample NET.CFG, into distr/. Requires mtools.
deploy: $(EXES)
	@mkdir -p $(DISTR)
	dd if=/dev/zero of=$(DEPLOY_IMG) bs=512 count=2880 2>/dev/null
	MTOOLS_NO_VFAT=1 mformat -f 1440 -v SPECTALK -i $(DEPLOY_IMG) ::
	@for e in $(EXES); do mcopy -i $(DEPLOY_IMG) $$e ::$$e; done
	mcopy -i $(DEPLOY_IMG) $(DISTR)/NET.CFG ::NET.CFG
	@echo "Deployed -> $(DEPLOY_IMG)"
	@mdir -i $(DEPLOY_IMG) ::

clean:
	rm -rf $(BUILD) $(EXES) *.ihx *.map *.noi *.lk *.sym
