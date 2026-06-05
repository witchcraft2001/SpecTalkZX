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

# Flat 32K layout via our crt0_flat: the EXE image spans WIN1+WIN2 as ONE loaded
# program (code grew past 0x8000, so DSS maps both pages from the image itself).
# Unlike crt0_page2 we must NOT GETMEM/SETWIN2 here — that would swap a fresh
# blank page over the code DSS already loaded into WIN2. crt0_flat keeps the
# header SP (0xBFFF, valid because WIN2 is mapped at entry) and zeroes _DATA.
# Layout: code 0x4200.. (grows up), data+stack high in WIN2:
#   data 0xA800.. (~4.4K),  stack 0xBFFF down (~1.7K),  code ceiling 0xA800.
CRT0_NAME ?= crt0_flat
CODE_LOC  ?= 0x4200
DATA_LOC  ?= 0xA800
STACK     ?= 0xBFFF      # header SP, used directly (WIN2 mapped via the 2-page image)

SRCDIR  = src
BUILD   = build
DISTR   = distr

SDCC_TARGET = -mz80
SDCC_OPT    = --max-allocs-per-node 5000 --opt-code-speed
CFLAGS      = $(SDCC_TARGET) $(SDCC_OPT) -I$(SDK)/include -Iinclude
SDASZ80 ?= sdasz80
SDASZ_FLAGS ?= -plosgff
CRT0    = $(BUILD)/$(CRT0_NAME).rel      # our local crt0 (assembled below)
SPRLIB  = $(SDK)/build/sprinter.lib

# Highest in-image address must clear 0x8000 so DSS maps a 2nd page to WIN2;
# data must start above the code image. WIN2 ends at 0xBFFF.
WIN2_BASE = 0x8000
WIN2_END  = 0xC000

# --- Apps: <NAME>_OBJS lists the .rel objects for each EXE -----------------
APPS = SPECTALK NETDIAG NETIRC
SPECTALK_OBJS = $(BUILD)/main.rel    $(BUILD)/term.rel  $(BUILD)/irc.rel  $(BUILD)/hist.rel \
                $(BUILD)/cfg.rel     $(BUILD)/net_esp.rel $(BUILD)/recode.rel \
                $(BUILD)/isauart.rel $(BUILD)/uart.rel
NETDIAG_OBJS  = $(BUILD)/netdiag.rel $(BUILD)/netcfg.rel $(BUILD)/isauart.rel
NETIRC_OBJS   = $(BUILD)/netirc.rel  $(BUILD)/netcfg.rel $(BUILD)/isauart.rel $(BUILD)/uart.rel

EXES = $(addsuffix .EXE,$(APPS))
DEPLOY_IMG ?= $(DISTR)/spectalk.img

.PHONY: all deploy clean
all: $(EXES)

$(BUILD)/%.rel: $(SRCDIR)/%.c
	@mkdir -p $(@D)
	$(SDCC) $(CFLAGS) -c -o $@ $<

# Local crt0 (flat WIN1+WIN2 layout), assembled from sprinter/crt0_flat.s
$(BUILD)/crt0_flat.rel: crt0_flat.s
	@mkdir -p $(@D)
	$(SDASZ80) $(SDASZ_FLAGS) $@ $<

# Link + convert to DSS EXE. $@ = SPECTALK.EXE; objects from <NAME>_OBJS.
# CRT0/DATA_LOC/STACK are per-app (see overrides below): SPECTALK uses the flat
# WIN1+WIN2 crt0_flat; the small diagnostics use the SDK crt0_page2 (single page).
%.EXE:
	$(SDCC) $(SDCC_TARGET) --no-std-crt0 --code-loc $(CODE_LOC) --data-loc $(DATA_LOC) $(SDCC_OPT) \
		$(CRT0) $($(basename $@)_OBJS) -l$(SPRLIB) -o $(BUILD)/$(basename $@).ihx
	$(PYTHON) $(SDK)/tools/ihx2exe.py $(BUILD)/$(basename $@).ihx $@ \
		--load $(CODE_LOC) --entry $(CODE_LOC) --stack $(STACK)
	@echo "Built: $@"

# SPECTALK: flat WIN1+WIN2 layout (own recipe so we can guard the result).
SPECTALK.EXE: CRT0 = $(BUILD)/crt0_flat.rel
SPECTALK.EXE: DATA_LOC = 0xA800
SPECTALK.EXE: STACK = 0xBFFF
SPECTALK.EXE: $(BUILD)/crt0_flat.rel $(SPECTALK_OBJS)
	$(SDCC) $(SDCC_TARGET) --no-std-crt0 --code-loc $(CODE_LOC) --data-loc $(DATA_LOC) $(SDCC_OPT) \
		$(CRT0) $(SPECTALK_OBJS) -l$(SPRLIB) -o $(BUILD)/SPECTALK.ihx
	$(PYTHON) tools/check_layout.py $(BUILD)/SPECTALK.ihx $(BUILD)/SPECTALK.map \
		--win2-base $(WIN2_BASE) --win2-end $(WIN2_END)
	$(PYTHON) $(SDK)/tools/ihx2exe.py $(BUILD)/SPECTALK.ihx SPECTALK.EXE \
		--load $(CODE_LOC) --entry $(CODE_LOC) --stack $(STACK)
	@echo "Built: SPECTALK.EXE"

# Diagnostics: small single-page programs on the SDK's alloc-WIN2 crt0_page2.
NETDIAG.EXE NETIRC.EXE: CRT0 = $(SDK)/build/crt0_page2.rel
NETDIAG.EXE NETIRC.EXE: DATA_LOC = 0x8000
NETDIAG.EXE NETIRC.EXE: STACK = 0x40FF
NETDIAG.EXE:  $(SDK)/build/crt0_page2.rel $(NETDIAG_OBJS)
NETIRC.EXE:   $(SDK)/build/crt0_page2.rel $(NETIRC_OBJS)

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
