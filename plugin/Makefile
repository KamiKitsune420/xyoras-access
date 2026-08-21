#---------------------------------------------------------------------------------
# XYORAS Access — 3GX plugin build
#
# Produces XYORASAccess.3gx from the sources in source/, linked against
# CTRPluginFramework, libcwav/libncsnd (audio), eSpeak NG (speech), and libctru.
#
# Requires DEVKITARM. Run `source scripts/env.sh` first, or use
# scripts/build-plugin.sh which does it for you.
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "DEVKITARM is not set. Run: source scripts/env.sh")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
# Project
#---------------------------------------------------------------------------------

PLGNAME  := XYORASAccess
PLGINFO  := $(PLGNAME).plgInfo

BUILD    := build
INCLUDES := include
SOURCES  := source \
            source/game \
            source/speech \
            source/features \
            source/data
DATA     := data

#---------------------------------------------------------------------------------
# Dependency locations
#---------------------------------------------------------------------------------

CTRPFLIB   ?= $(DEVKITPRO)/libctrpf
CWAVLIB    ?= $(DEVKITPRO)/libcwav
NCSNDLIB   ?= $(DEVKITPRO)/libncsnd

# eSpeak NG is built out of tree by scripts/build-espeak-3ds.sh.
ESPEAK_DIR    ?= $(TOPDIR)/../third_party/espeak-ng
ESPEAK_BUILD  ?= $(ESPEAK_DIR)/build-3ds
ESPEAK_INC    := $(ESPEAK_DIR)/src/include
# eSpeak builds as THREE static libraries, not one: the synthesiser plus its
# Unicode character-database helper (ucd) and the speechPlayer backend. All
# three must reach the link line, or libespeak-ng is left full of undefined
# ucd_* references.
ESPEAK_LIBDIR := $(ESPEAK_BUILD)/src/libespeak-ng
ESPEAK_UCDDIR := $(ESPEAK_BUILD)/src/ucd-tools
ESPEAK_SPDIR  := $(ESPEAK_BUILD)/src/speechPlayer

#---------------------------------------------------------------------------------
# Code generation
#
# ARMv6k hard-float is the 3DS ARM11. -mtp=soft because a plugin does not own
# the hardware thread pointer. These MUST match cmake/3DSToolchain.cmake.
#---------------------------------------------------------------------------------

ARCH     := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS   := $(ARCH) -Os -mword-relocations \
            -fomit-frame-pointer -ffunction-sections -fdata-sections \
            -fno-strict-aliasing -Wall -Wextra -Wno-unused-parameter

CFLAGS   += $(INCLUDE) -D__3DS__ -DARM11 -D_3DS

# CTRPF is built without RTTI or exceptions; we must match or the link fails.
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11

ASFLAGS  := $(ARCH)

LDFLAGS  := -T $(TOPDIR)/3gx.ld $(ARCH) -Os \
            -Wl,-Map,$(PLGNAME).map,--gc-sections,--strip-discarded,--strip-debug

# Order matters: ctrpf first, ctru last.
# Order matters: a static library only resolves symbols for objects listed
# BEFORE it, so dependents come first. espeak-ng depends on ucd and
# speechPlayer; everything eventually falls through to ctru.
LIBS     := -lctrpf -lcwav -lncsnd -lespeak-ng -lucd -lspeechPlayer -lctru

LIBDIRS  := $(TOPDIR) $(CTRPFLIB) $(CWAVLIB) $(NCSNDLIB) $(CTRULIB) $(PORTLIBS)

#---------------------------------------------------------------------------------
# No need to edit past this point
#---------------------------------------------------------------------------------

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT   := $(TOPDIR)/$(PLGNAME)
export TOPDIR   := $(TOPDIR)
export VPATH    := $(foreach dir,$(SOURCES),$(TOPDIR)/$(dir)) \
                   $(foreach dir,$(DATA),$(TOPDIR)/$(dir))
export DEPSDIR  := $(TOPDIR)/$(BUILD)

CFILES          := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES        := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES          := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES        := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.bin)))

export LD       := $(CXX)
export OFILES_BIN := $(addsuffix .o,$(BINFILES))
export OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES     := $(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN := $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE  := $(foreach dir,$(INCLUDES),-I $(TOPDIR)/$(dir)) \
                   $(foreach dir,$(LIBDIRS),-I $(dir)/include) \
                   -I $(ESPEAK_INC) \
                   -I $(TOPDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L $(dir)/lib) \
                   -L $(ESPEAK_LIBDIR) -L $(ESPEAK_UCDDIR) -L $(ESPEAK_SPDIR)

.PHONY: all clean re check $(BUILD)

# $(BUILD) must be phony: it is also a directory name, and once that directory
# exists make would otherwise consider the target up to date and never recurse.

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(TOPDIR)/Makefile

clean:
	@echo "cleaning $(PLGNAME)"
	@rm -fr $(BUILD) $(OUTPUT).3gx $(OUTPUT).elf $(OUTPUT).map

re: clean all

# Verify dependencies are where the build expects them.
check:
	@echo "checking dependencies"
	@for f in "$(CTRPFLIB)/lib/libctrpf.a" \
	          "$(CWAVLIB)/lib/libcwav.a" \
	          "$(NCSNDLIB)/lib/libncsnd.a" \
	          "$(ESPEAK_LIBDIR)/libespeak-ng.a" \
	          "$(CTRULIB)/lib/libctru.a"; do \
	    if [ -f "$$f" ]; then echo "  ok   $$f"; else echo "  MISS $$f"; fi; \
	done

else

DEPENDS := $(OFILES:.o=.d)

all: $(OUTPUT).3gx

$(OUTPUT).3gx : $(OUTPUT).elf
$(OUTPUT).elf : $(OFILES)

$(OFILES_SRC) : $(HFILES_BIN)

%.bin.o %_bin.h : %.bin
	@echo "$(notdir $<)"
	@$(bin2o)

# 3gxtool turns the linked ELF plus the plgInfo metadata into a .3gx.
# -s embeds the symbol table, which makes crash addresses resolvable.
GXTOOL := $(shell command -v 3gxtool 2>/dev/null || echo "$(DEVKITPRO)/tools/bin/3gxtool.exe")

# The plgInfo is an input, not just documentation: it carries the memory size
# and the metadata Rosalina shows. Without it listed here, editing it leaves
# the old values baked into the .3gx and the change looks like it did nothing.
%.3gx: %.elf $(TOPDIR)/$(PLGINFO)
	@echo "creating $(notdir $@)"
	@$(GXTOOL) -s $< $(TOPDIR)/$(PLGINFO) $@

-include $(DEPENDS)

endif
