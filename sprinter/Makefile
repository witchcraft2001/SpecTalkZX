# =========================================================================
#  SprinTalk — Sprinter DSS IRC client (based on SpecTalk ZX)
#  Build with the SDCC 4.5 Sprinter SDK. Produces one .EXE per app.
# =========================================================================
#  Targets:
#    make            Build all .EXE files (SprinTalk -> SPTALK.EXE)
#    make deploy      Build + write a FAT12 floppy (distr/sptalk.img) with the
#                     EXEs and plain-text docs (SPTALK.TXT, HOWTO.TXT,
#                     HOWTO_RU.TXT, converted from README.md / HOWTO*.md)
#    make clean       Remove build artifacts
#
#  Verify in MAME:
#    ./run.sh         (boots MAME with distr/sptalk.img mounted as flop2)
#    then at the DSS prompt switch to the floppy drive and run SPTALK / NETDIAG
# =========================================================================

SDK     ?= /Users/dmitry/dev/zx/sprinter/sdcc45-sprinter-sdk
SDCC    ?= sdcc
PYTHON  ?= python3

# `all` (SPTALK.EXE) is the default goal even though the win0 build rules
# below (needed before the `all:` target itself appears) would otherwise make
# Make pick whichever target happens to be defined first in the file.
.DEFAULT_GOAL := all

SRCDIR  = src
# Per-backend object directory. NET_BACKEND lands in CFLAGS (-DNET_BACKEND_UNET),
# so every shared .c -- main, term, irc, hist, cfg, recode -- compiles
# differently per backend. With one shared build/ a `make NET_BACKEND=unet`
# after a plain `make` relinked stale ESP-compiled objects and only rebuilt the
# ones whose sources happened to be newer, producing a binary that reported
# itself as the ESP build.
BUILD   = build/$(NET_BACKEND)
DISTR   = distr

# NET_BACKEND selects the transport SPTALK.EXE is built against:
#   esp  (default) — the native ESP8266/ESP-AT driver (net_esp.c/uart.c/
#                     isauart.c), Wi-Fi only, no extra memory-layout needs.
#   unet            — UNET.DLL (net_unet.c over unetld.c/unet.c/unetcall.s):
#                     one binary, any UNETxxxx.DLL card chosen at runtime via
#                     the NET env var. Needs the extra _HIGH linker area (the
#                     DLL call dispatcher — see unetcall.s / unetcore.s) since
#                     the DLL is swapped into WIN1, which under the win0 layout
#                     holds the upper half of SPTALK's own code.
NET_BACKEND ?= esp

ifeq ($(NET_BACKEND),unet)
SPTALK_NET_OBJS = $(BUILD)/net_unet.rel $(BUILD)/unetldcore.rel $(BUILD)/unetcore.rel $(BUILD)/unetcall.rel
NET_CFLAGS      = -DNET_BACKEND_UNET
else ifeq ($(NET_BACKEND),esp)
SPTALK_NET_OBJS = $(BUILD)/net_esp.rel $(BUILD)/uart.rel $(BUILD)/isauart.rel
NET_CFLAGS      =
else
$(error NET_BACKEND must be 'esp' or 'unet', got '$(NET_BACKEND)')
endif

SDCC_TARGET = -mz80
SDCC_OPT    = --max-allocs-per-node 5000 --opt-code-speed
CFLAGS      = $(SDCC_TARGET) $(SDCC_OPT) $(NET_CFLAGS) -I$(SDK)/include -Iinclude
SDASZ80 ?= sdasz80
SDASZ_FLAGS ?= -plosgff
SDLDZ80 ?= sdldz80
SPRLIB  = $(SDK)/build/sprinter.lib

# SDCC's own z80 runtime library, linked LAST so it only supplies helpers the
# one-shot `sdcc ... -o` frontend always pulls in silently (e.g. __divuchar/
# __moduchar/__modsint for plain C division and modulo) but that sprinter.lib
# itself doesn't provide. Our hand-written .lk scripts (SPTALK.EXE, NETDLL.EXE
# — needed for the extra _HIGH linker area none of --code-loc/--data-loc can
# place) replicate that, matching win0.mk's own discovery of the same gap.
# sprinter.lib is listed first so its DSS-backed printf/puts/memcpy win over
# any same-named z80.lib module.
SDCC_Z80LIB := $(shell for d in $$($(SDCC) -mz80 --print-search-dirs 2>/dev/null | sed -n '/libdir:/,/libpath:/p' | grep '^/'); do if [ -f "$$d/z80.lib" ]; then echo "$$d/z80.lib"; break; fi; done)

# --- win0 layout (WIN0+WIN1+WIN2, ~47K): two-stage PRELOAD build -----------
# Port plan "SprinTalk: переход на трёхоконную раскладку WIN0+WIN1+WIN2".
# Code+rodata may span WIN0 (0x0180..0x3FFF) + WIN1 (0x4000..0x7FFF); the RST
# trampolines (_WINRT), the UNET call dispatcher (_HIGH, unet backend only)
# and _DATA/stack live in the private WIN2 page. See tools/check_win0_layout.py
# for the exact invariants and lib/win0/*.s (SDK) for the runtime mechanics.
WIN0DIR      = $(SDK)/lib/win0
WIN0_CODE    = 0x0180
WIN0_WINRT   = 0x8000
WIN0_HIGH    = 0x8100
WIN0_DATA    = 0x8400
WIN0_STACK_TOP = 0xBF00

# SDK win0 runtime, compiled straight from the SDK sources (not vendored --
# see the port plan's "link SDK files directly" decision).
WIN0_RT_OBJS = $(BUILD)/crt0_win0.rel $(BUILD)/win0_rt.rel $(BUILD)/win0_dss.rel $(BUILD)/dss_raw.rel

$(BUILD)/crt0_win0.rel: $(WIN0DIR)/crt0_win0.s
	@mkdir -p $(@D)
	$(SDASZ80) $(SDASZ_FLAGS) $@ $<
$(BUILD)/win0_rt.rel: $(WIN0DIR)/win0_rt.s
	@mkdir -p $(@D)
	$(SDASZ80) $(SDASZ_FLAGS) $@ $<
$(BUILD)/win0_dss.rel: $(WIN0DIR)/win0_dss.c
	@mkdir -p $(@D)
	$(SDCC) $(SDCC_TARGET) $(SDCC_OPT) -I$(SDK)/include -I$(WIN0DIR) -c -o $@ $<
$(BUILD)/dss_raw.rel: $(WIN0DIR)/dss_raw.c
	@mkdir -p $(@D)
	$(SDCC) $(SDCC_TARGET) $(SDCC_OPT) -I$(SDK)/include -I$(WIN0DIR) -c -o $@ $<
$(BUILD)/loader.rel: $(WIN0DIR)/loader.c
	@mkdir -p $(@D)
	$(SDCC) $(SDCC_TARGET) $(SDCC_OPT) -I$(SDK)/include -c -o $@ $<

# Zeroes _DATA on the win0 runtime's behalf (crt0_win0.s does not -- see the
# port plan's win0 migration notes). Must link immediately after crt0_win0.rel.
$(BUILD)/gsinit_zero_data.rel: gsinit_zero_data.s
	@mkdir -p $(@D)
	$(SDASZ80) $(SDASZ_FLAGS) $@ $<

# Stage-1 PRELOAD loader (@0x8100): independent of the payload, plain SDK
# crt0 (single WIN2 page, same shape as crt0_page2's own diagnostics).
$(BUILD)/loader.ihx: $(SDK)/build/crt0.rel $(SPRLIB) $(BUILD)/loader.rel
	@mkdir -p $(BUILD)
	printf '%s\n' '-mjx' > $(BUILD)/loader.lk
	printf '%s\n' '-i $(BUILD)/loader.ihx' >> $(BUILD)/loader.lk
	printf '%s\n' '-b _CODE = 0x8100' >> $(BUILD)/loader.lk
	printf '%s\n' '-l $(abspath $(SPRLIB))' >> $(BUILD)/loader.lk
	printf '%s\n' '-l $(abspath $(SDCC_Z80LIB))' >> $(BUILD)/loader.lk
	printf '%s\n' '$(abspath $(SDK)/build/crt0.rel)' >> $(BUILD)/loader.lk
	printf '%s\n' '$(abspath $(BUILD)/loader.rel)' >> $(BUILD)/loader.lk
	printf '%s\n' '-e' >> $(BUILD)/loader.lk
	$(SDLDZ80) -nf $(BUILD)/loader.lk

# --- Apps -------------------------------------------------------------------
# Only SprinTalk ships. NETDIAG/NETIRC were early bring-up/debug tools — still
# buildable on demand (`make diag`) but not part of the release or test image.
APPS = SPTALK
SPTALK_OBJS   = $(BUILD)/main.rel    $(BUILD)/term.rel  $(BUILD)/irc.rel  $(BUILD)/hist.rel \
                $(BUILD)/cfg.rel     $(BUILD)/recode.rel $(SPTALK_NET_OBJS)
NETDIAG_OBJS  = $(BUILD)/netdiag.rel $(BUILD)/netcfg.rel $(BUILD)/isauart.rel
NETIRC_OBJS   = $(BUILD)/netirc.rel  $(BUILD)/netcfg.rel $(BUILD)/isauart.rel $(BUILD)/uart.rel
DIAG_EXES     = NETDIAG.EXE NETIRC.EXE

# UNET DLL loader diagnostic (port plan Stage 1): win0 layout testbed. Proves
# the DLL window-swap mechanics (unetcore.s's _wrt_p1 fix, _HIGH placement,
# _DATA zeroing, the P0:0x0100 app-dir path) in isolation, before SPTALK.EXE
# itself moves to win0.
NETDLL_OBJS   = $(BUILD)/netdll.rel $(BUILD)/unetldcore.rel $(BUILD)/unetcore.rel $(BUILD)/unetcall.rel
NETDLL_EXE    = NETDLL.EXE

EXES = $(addsuffix .EXE,$(APPS))
DEPLOY_IMG ?= $(DISTR)/sptalk.img

# NET_BACKEND=unet ships beside SPTALK.EXE: unetldcore.s resolves a UNETxxxx.DLL
# next to the EXE (P0:0x0100 app directory, see the win0 layout notes), and the
# NET env var picks which one at runtime -- ship all three so any NET tag works.
# Override UNET_DLL_DIR if the unet_libs checkout lives elsewhere; deploy/
# distrib stop with a readable message rather than an mcopy/zip error if the
# DLLs are missing.
UNET_DLL_DIR ?= /Users/dmitry/dev/zx/sprinter/sources/unet_libs/unet_libs_core/dll
UNET_DLLS = UNETESP.DLL UNETRTL.DLL UNET509B.DLL

# $(call check-dlls) -- shell fragment, only expanded for NET_BACKEND=unet.
define check-dlls
for dll in $(UNET_DLLS); do \
        test -f $(UNET_DLL_DIR)/$$dll || { \
                echo "ERROR: $(UNET_DLL_DIR)/$$dll not found."; \
                echo "       NET_BACKEND=unet ships the UNET DLLs beside SPTALK.EXE;"; \
                echo "       set UNET_DLL_DIR=<path to unet_libs_core/dll>."; \
                exit 1; }; \
done
endef

# Markdown docs -> plain text (DOS 8.3 names) for the package / test image
DOC_README = README.md
DOC_HOWTO  = HOWTO.md
DOC_HOWTO_RU = HOWTO_RU.md
TXT_README = $(BUILD)/SPTALK.TXT
TXT_HOWTO  = $(BUILD)/HOWTO.TXT
TXT_HOWTO_RU = $(BUILD)/HOWTO_RU.TXT

# Release zip
DIST_VER ?= 0.1
# Names the archive after the transport that is actually in the binary, so a
# `make distrib NET_BACKEND=unet` can't be mistaken for the ESP build. Override
# for a narrower label (the plan's `make distrib DIST_TAG=rtl`, say).
DIST_TAG ?= $(NET_BACKEND)
DIST_ZIP  = $(DISTR)/sptalk-$(DIST_VER)-$(DIST_TAG).zip

.PHONY: all deploy distrib dist distr diag netdll clean
all: $(EXES)

# Optional: build the early-bring-up diagnostics by hand.
diag: $(DIAG_EXES)

# Optional: build the UNET loader diagnostic by hand.
netdll: $(NETDLL_EXE)

$(BUILD)/%.rel: $(SRCDIR)/%.c
	@mkdir -p $(@D)
	$(SDCC) $(CFLAGS) -c -o $@ $<

# main.c carries the APP_BUILD (__DATE__/__TIME__) banner stamp; rebuild it every
# run so the stamp reflects the actual build, not the last time main.c changed.
.PHONY: FORCE
FORCE:
$(BUILD)/main.rel: FORCE

# Other hand-written asm modules (currently just the UNET call trampoline).
$(BUILD)/%.rel: $(SRCDIR)/%.s
	@mkdir -p $(@D)
	$(SDASZ80) $(SDASZ_FLAGS) $@ $<

# Link + convert to DSS EXE. $@ = SPECTALK.EXE; objects from <NAME>_OBJS.
# Only the small crt0_page2 diagnostics (NETDIAG.EXE/NETIRC.EXE) use this
# generic rule; SPTALK.EXE/NETDLL.EXE have their own win0 recipes.
%.EXE:
	$(SDCC) $(SDCC_TARGET) --no-std-crt0 --code-loc $(CODE_LOC) --data-loc $(DATA_LOC) $(SDCC_OPT) \
		$(CRT0) $($(basename $@)_OBJS) -l$(SPRLIB) -o $(BUILD)/$(basename $@).ihx
	$(PYTHON) $(SDK)/tools/ihx2exe.py $(BUILD)/$(basename $@).ihx $@ \
		--load $(CODE_LOC) --entry $(CODE_LOC) --stack $(STACK)
	@echo "Built: $@"

# SPTALK: win0 layout (WIN0+WIN1+WIN2). Same two-link + win0_exe.py pack as
# NETDLL.EXE (see that recipe and the win0 build machinery above); _HIGH only
# for NET_BACKEND=unet (unetcore.s's _unet_call, which must survive the WIN1
# DLL swap -- the esp backend has nothing that needs to live there).
SPTALK.EXE: $(WIN0_RT_OBJS) $(BUILD)/gsinit_zero_data.rel $(SPTALK_OBJS) $(BUILD)/loader.ihx
	@mkdir -p $(BUILD)
	printf '%s\n' '-mjx' > $(BUILD)/SPTALK_payload.lk
	printf '%s\n' '-i $(BUILD)/SPTALK_payload.ihx' >> $(BUILD)/SPTALK_payload.lk
	printf '%s\n' '-b _CODE = $(WIN0_CODE)' >> $(BUILD)/SPTALK_payload.lk
	printf '%s\n' '-b _WINRT = $(WIN0_WINRT)' >> $(BUILD)/SPTALK_payload.lk
	if [ "$(NET_BACKEND)" = "unet" ]; then \
		printf '%s\n' '-b _HIGH = $(WIN0_HIGH)' >> $(BUILD)/SPTALK_payload.lk; \
	fi
	printf '%s\n' '-b _DATA = $(WIN0_DATA)' >> $(BUILD)/SPTALK_payload.lk
	printf '%s\n' '-l $(abspath $(SPRLIB))' >> $(BUILD)/SPTALK_payload.lk
	printf '%s\n' '-l $(abspath $(SDCC_Z80LIB))' >> $(BUILD)/SPTALK_payload.lk
	printf '%s\n' '$(abspath $(BUILD)/crt0_win0.rel)' >> $(BUILD)/SPTALK_payload.lk
	printf '%s\n' '$(abspath $(BUILD)/gsinit_zero_data.rel)' >> $(BUILD)/SPTALK_payload.lk
	printf '%s\n' '$(abspath $(BUILD)/win0_rt.rel)' >> $(BUILD)/SPTALK_payload.lk
	printf '%s\n' '$(abspath $(BUILD)/win0_dss.rel)' >> $(BUILD)/SPTALK_payload.lk
	printf '%s\n' '$(abspath $(BUILD)/dss_raw.rel)' >> $(BUILD)/SPTALK_payload.lk
	for rel in $(SPTALK_OBJS); do printf '%s\n' "$(abspath $$rel)" >> $(BUILD)/SPTALK_payload.lk; done
	printf '%s\n' '-e' >> $(BUILD)/SPTALK_payload.lk
	$(SDLDZ80) -nf $(BUILD)/SPTALK_payload.lk
	$(PYTHON) tools/check_win0_layout.py $(BUILD)/SPTALK_payload.ihx $(BUILD)/SPTALK_payload.noi
	$(PYTHON) $(SDK)/tools/win0_exe.py $(BUILD)/loader.ihx $(BUILD)/SPTALK_payload.ihx SPTALK.EXE
	@echo "Built: SPTALK.EXE (win0, NET_BACKEND=$(NET_BACKEND))"

# Diagnostics: small single-page programs on the SDK's alloc-WIN2 crt0_page2.
# These are the ONLY users of the generic %.EXE rule (and so of CODE_LOC/
# DATA_LOC/STACK) -- SPTALK.EXE and NETDLL.EXE have their own win0 recipes.
NETDIAG.EXE NETIRC.EXE: CRT0 = $(SDK)/build/crt0_page2.rel
NETDIAG.EXE NETIRC.EXE: CODE_LOC = 0x4200
NETDIAG.EXE NETIRC.EXE: DATA_LOC = 0x8000
NETDIAG.EXE NETIRC.EXE: STACK = 0x40FF
NETDIAG.EXE:  $(SDK)/build/crt0_page2.rel $(NETDIAG_OBJS)
NETIRC.EXE:   $(SDK)/build/crt0_page2.rel $(NETIRC_OBJS)

# NETDLL: win0 layout (WIN0+WIN1+WIN2). Two links -- the stage-1 loader
# ($(BUILD)/loader.ihx, shared machinery above) and this payload -- packed
# together by win0_exe.py into one PRELOAD .EXE. gsinit_zero_data.rel is
# linked immediately after crt0_win0.rel (see that file's header); unetcore.s's
# _unet_call (the only thing that must survive the WIN1 DLL swap) lands in
# _HIGH, right after _WINRT.
NETDLL.EXE: $(WIN0_RT_OBJS) $(BUILD)/gsinit_zero_data.rel $(NETDLL_OBJS) $(BUILD)/loader.ihx
	@mkdir -p $(BUILD)
	printf '%s\n' '-mjx' > $(BUILD)/NETDLL_payload.lk
	printf '%s\n' '-i $(BUILD)/NETDLL_payload.ihx' >> $(BUILD)/NETDLL_payload.lk
	printf '%s\n' '-b _CODE = $(WIN0_CODE)' >> $(BUILD)/NETDLL_payload.lk
	printf '%s\n' '-b _WINRT = $(WIN0_WINRT)' >> $(BUILD)/NETDLL_payload.lk
	printf '%s\n' '-b _HIGH = $(WIN0_HIGH)' >> $(BUILD)/NETDLL_payload.lk
	printf '%s\n' '-b _DATA = $(WIN0_DATA)' >> $(BUILD)/NETDLL_payload.lk
	printf '%s\n' '-l $(abspath $(SPRLIB))' >> $(BUILD)/NETDLL_payload.lk
	printf '%s\n' '-l $(abspath $(SDCC_Z80LIB))' >> $(BUILD)/NETDLL_payload.lk
	printf '%s\n' '$(abspath $(BUILD)/crt0_win0.rel)' >> $(BUILD)/NETDLL_payload.lk
	printf '%s\n' '$(abspath $(BUILD)/gsinit_zero_data.rel)' >> $(BUILD)/NETDLL_payload.lk
	printf '%s\n' '$(abspath $(BUILD)/win0_rt.rel)' >> $(BUILD)/NETDLL_payload.lk
	printf '%s\n' '$(abspath $(BUILD)/win0_dss.rel)' >> $(BUILD)/NETDLL_payload.lk
	printf '%s\n' '$(abspath $(BUILD)/dss_raw.rel)' >> $(BUILD)/NETDLL_payload.lk
	for rel in $(NETDLL_OBJS); do printf '%s\n' "$(abspath $$rel)" >> $(BUILD)/NETDLL_payload.lk; done
	printf '%s\n' '-e' >> $(BUILD)/NETDLL_payload.lk
	$(SDLDZ80) -nf $(BUILD)/NETDLL_payload.lk
	$(PYTHON) tools/check_win0_layout.py $(BUILD)/NETDLL_payload.ihx $(BUILD)/NETDLL_payload.noi
	$(PYTHON) $(SDK)/tools/win0_exe.py $(BUILD)/loader.ihx $(BUILD)/NETDLL_payload.ihx NETDLL.EXE
	@echo "Built: NETDLL.EXE (win0)"

# Markdown -> plain text (CRLF, DOS-friendly) for the floppy.
# SPTALK.TXT / HOWTO_RU.TXT: CP866 (block-drawing logo + Cyrillic). HOWTO.TXT: ASCII.
$(TXT_README): $(DOC_README) tools/md2txt.py
	@mkdir -p $(@D)
	$(PYTHON) tools/md2txt.py $(DOC_README) $@ cp866
$(TXT_HOWTO): $(DOC_HOWTO) tools/md2txt.py
	@mkdir -p $(@D)
	$(PYTHON) tools/md2txt.py $(DOC_HOWTO) $@
$(TXT_HOWTO_RU): $(DOC_HOWTO_RU) tools/md2txt.py
	@mkdir -p $(@D)
	$(PYTHON) tools/md2txt.py $(DOC_HOWTO_RU) $@ cp866

# The unet image also carries NETDLL.EXE: when SPTALK reports a DLL failure,
# that diagnostic re-runs SELECT/LOAD/GETCAPS/ABI/NETSTART/GETINFO on its own
# and prints each stage, which is how you tell a bad NET value from a bad DLL.
ifeq ($(NET_BACKEND),unet)
DEPLOY_EXTRA = $(NETDLL_EXE)
endif

# FAT12 test floppy (for run.sh / MAME): SprinTalk + plain-text docs. mtools req.
deploy: SPTALK.EXE $(DEPLOY_EXTRA) $(TXT_README) $(TXT_HOWTO) $(TXT_HOWTO_RU)
	@mkdir -p $(DISTR)
	dd if=/dev/zero of=$(DEPLOY_IMG) bs=512 count=2880 2>/dev/null
	MTOOLS_NO_VFAT=1 mformat -f 1440 -v SPRINTALK -i $(DEPLOY_IMG) ::
	mcopy -i $(DEPLOY_IMG) SPTALK.EXE ::SPTALK.EXE
	mcopy -i $(DEPLOY_IMG) $(TXT_README) ::SPTALK.TXT
	mcopy -i $(DEPLOY_IMG) $(TXT_HOWTO)  ::HOWTO.TXT
	mcopy -i $(DEPLOY_IMG) $(TXT_HOWTO_RU)  ::HOWTO_RU.TXT
	if [ "$(NET_BACKEND)" = "unet" ]; then \
		$(check-dlls); \
		for dll in $(UNET_DLLS); do mcopy -i $(DEPLOY_IMG) $(UNET_DLL_DIR)/$$dll ::$$dll; done; \
		mcopy -i $(DEPLOY_IMG) $(NETDLL_EXE) ::$(NETDLL_EXE); \
	fi
	@echo "Deployed -> $(DEPLOY_IMG)"
	@mdir -i $(DEPLOY_IMG) ::

# Release archive: SPTALK.EXE + the text docs, flat in a zip. `distr`/`dist`
# are aliases. Requires `zip`.
distrib dist distr: SPTALK.EXE $(TXT_README) $(TXT_HOWTO) $(TXT_HOWTO_RU)
	@mkdir -p $(DISTR)
	rm -f $(DIST_ZIP)
	zip -j $(DIST_ZIP) SPTALK.EXE $(TXT_README) $(TXT_HOWTO) $(TXT_HOWTO_RU)
	if [ "$(NET_BACKEND)" = "unet" ]; then \
		$(check-dlls); \
		zip -j $(DIST_ZIP) $(addprefix $(UNET_DLL_DIR)/,$(UNET_DLLS)); \
	fi
	@echo "Packaged -> $(DIST_ZIP)"
	@unzip -l $(DIST_ZIP)

clean:
	rm -rf build $(EXES) $(DIAG_EXES) $(NETDLL_EXE) *.ihx *.map *.noi *.lk *.sym
