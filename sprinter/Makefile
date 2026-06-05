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

# Keep code + data + stack all inside WIN1 (0x4000-0x7FFF), the single page DSS
# guarantees for a program loaded at 0x4100. WIN2 (0x8000+) is NOT the program's
# memory unless explicitly allocated (Dss.GetMem + SetWin2), so data/stack must
# not live there. (See sprinter_ai_doc .../04_dss/05_dss_exe.md.)
CODE_LOC  ?= 0x4100
DATA_LOC  ?= 0x6000
STACK     ?= 0x7FFF

SRCDIR  = src
BUILD   = build
DISTR   = distr

SDCC_TARGET = -mz80
SDCC_OPT    = --max-allocs-per-node 5000 --opt-code-speed
CFLAGS      = $(SDCC_TARGET) $(SDCC_OPT) -I$(SDK)/include -Iinclude
CRT0    = $(SDK)/build/crt0.rel
SPRLIB  = $(SDK)/build/sprinter.lib

# --- Apps: <NAME>_OBJS lists the .rel objects for each EXE -----------------
APPS = SPECTALK NETDIAG NETIRC
SPECTALK_OBJS = $(BUILD)/main.rel    $(BUILD)/term.rel $(BUILD)/net_esp.rel \
                $(BUILD)/netcfg.rel  $(BUILD)/isauart.rel $(BUILD)/uart.rel
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
