#---------------------------------------------------------------------------------
# Screen Reader — 3GX plugin build
#
# Produces ScreenReader.3gx. Unlike XYORASAccess this plugin does not read any
# game: it hooks a host process's menu-item text-draw function and speaks
# whatever string is passed to it. It reuses ../plugin/source/speech wholesale
# rather than carrying its own synthesis or audio.
#
# Requires DEVKITARM. Run `source scripts/env.sh` first, or use
# scripts/build-screenreader.sh which does it for you.
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

PLGNAME  := ScreenReader
PLGINFO  := $(PLGNAME).plgInfo

BUILD    := build
INCLUDES := include \
            ../plugin/include \
            ../plugin/source/speech
SOURCES  := source \
            ../plugin/source/speech

# speech/ also needs platform.cpp and diagnostics.cpp, but those sit next to
# XYORASAccess's own main.cpp. Globbing that directory would pull in a second
# main(); instead it goes on VPATH only and the two files are named here.
SHARED_VPATH := ../plugin/source

#---------------------------------------------------------------------------------
# Dependency locations — identical to ../plugin/Makefile
#---------------------------------------------------------------------------------

CTRPFLIB   ?= $(DEVKITPRO)/libctrpf
CWAVLIB    ?= $(DEVKITPRO)/libcwav
NCSNDLIB   ?= $(DEVKITPRO)/libncsnd

ESPEAK_DIR    ?= $(TOPDIR)/../third_party/espeak-ng
ESPEAK_BUILD  ?= $(ESPEAK_DIR)/build-3ds
ESPEAK_INC    := $(ESPEAK_DIR)/src/include
ESPEAK_LIBDIR := $(ESPEAK_BUILD)/src/libespeak-ng
ESPEAK_UCDDIR := $(ESPEAK_BUILD)/src/ucd-tools
ESPEAK_SPDIR  := $(ESPEAK_BUILD)/src/speechPlayer

#---------------------------------------------------------------------------------
# Code generation — MUST match ../plugin/Makefile and cmake/3DSToolchain.cmake
#---------------------------------------------------------------------------------

ARCH     := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS   := $(ARCH) -Os -mword-relocations \
            -fomit-frame-pointer -ffunction-sections -fdata-sections \
            -fno-strict-aliasing -Wall -Wextra -Wno-unused-parameter

CFLAGS   += $(INCLUDE) -D__3DS__ -DARM11 -D_3DS $(SPEECH_DEFS)

CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11

ASFLAGS  := $(ARCH)

LDFLAGS  := -T $(TOPDIR)/3gx.ld $(ARCH) -Os \
            -Wl,-Map,$(PLGNAME).map,--gc-sections,--strip-discarded,--strip-debug

# Order matters: dependents before dependencies, ctru last.
LIBS     := -lctrpf $(SPEECH_LIBS) -lctru

LIBDIRS  := $(TOPDIR) $(CTRPFLIB) $(CWAVLIB) $(NCSNDLIB) $(CTRULIB) $(PORTLIBS)

#---------------------------------------------------------------------------------
# No need to edit past this point
#---------------------------------------------------------------------------------

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT   := $(TOPDIR)/$(PLGNAME)
export TOPDIR   := $(TOPDIR)
export VPATH    := $(foreach dir,$(SOURCES),$(TOPDIR)/$(dir)) \
                   $(TOPDIR)/$(SHARED_VPATH)
export DEPSDIR  := $(TOPDIR)/$(BUILD)

CFILES          := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES        := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp))) \
                   $(SHARED_CPP)
SFILES          := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

export LD       := $(CXX)
export OFILES   := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)

export INCLUDE  := $(foreach dir,$(INCLUDES),-I $(TOPDIR)/$(dir)) \
                   $(foreach dir,$(LIBDIRS),-I $(dir)/include) \
                   -I $(ESPEAK_INC) \
                   -I $(TOPDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L $(dir)/lib) \
                   -L $(ESPEAK_LIBDIR) -L $(ESPEAK_UCDDIR) -L $(ESPEAK_SPDIR)

.PHONY: all clean re $(BUILD)

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(TOPDIR)/Makefile

clean:
	@echo "cleaning $(PLGNAME)"
	@rm -fr $(BUILD) $(OUTPUT).3gx $(OUTPUT).elf $(OUTPUT).map

re: clean all

else

DEPENDS := $(OFILES:.o=.d)

all: $(OUTPUT).3gx

$(OUTPUT).elf : $(OFILES)

# 3gxtool turns the linked ELF plus the plgInfo metadata into a .3gx.
# -s embeds the symbol table, which makes crash addresses resolvable.
GXTOOL := $(shell command -v 3gxtool 2>/dev/null || echo "$(DEVKITPRO)/tools/bin/3gxtool.exe")

# The plgInfo is a real input: without it listed here, editing it leaves the
# old values baked into the .3gx and the change looks like it did nothing.
%.3gx: %.elf $(TOPDIR)/$(PLGINFO)
	@echo "creating $(notdir $@)"
	@$(GXTOOL) -s $< $(TOPDIR)/$(PLGINFO) $@

-include $(DEPENDS)

endif
