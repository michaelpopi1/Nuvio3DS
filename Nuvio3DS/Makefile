#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
#---------------------------------------------------------------------------------
TARGET		:= Nuvio3DS
BUILD		:= build
SOURCES		:= source source/net source/stremio source/player source/ui
DATA		:= data
INCLUDES	:= source
ROMFS		:= romfs

APP_TITLE	:= Nuvio3DS
APP_DESCRIPTION	:= Native Nuvio & Stremio Addon Client for New 3DS XL
APP_AUTHOR	:= Nuvio3DS Contributors
APP_UNIQUE_ID	:= 0x4E5633

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:= -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS	:= -g -Wall -O2 -mword-relocations \
		   -ffunction-sections -fdata-sections \
		   $(ARCH)

CFLAGS	+= -I$(TOPDIR)/source $(INCLUDE) -D__3DS__ -DNUVIO3DS_VERSION=\"1.0.0\"

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17

ASFLAGS	:= -g $(ARCH)
LDFLAGS	= -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

# Link order matters for static ARM libraries:
# curl -> mbedtls/x509/crypto -> z -> citro2d -> citro3d -> ctru -> m
LIBS	:= -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -lcitro2d -lcitro3d -lctru -lm

#---------------------------------------------------------------------------------
# list of directories containing libraries
#---------------------------------------------------------------------------------
LIBDIRS	:= $(PORTLIBS) $(CTRULIB)

#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT	:= $(CURDIR)/$(TARGET)
export TOPDIR	:= $(CURDIR)

export VPATH	:= $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
		   $(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:= $(CURDIR)/$(BUILD)

CFILES		:= $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:= $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:= $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
PICAFILES	:= $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.v.pica)))
BINFILES	:= $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

ifeq ($(strip $(CPPFILES)),)
	export LD := $(CC)
else
	export LD := $(CXX)
endif

export OFILES_SOURCES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES_BIN := $(addsuffix .o,$(BINFILES)) $(addsuffix .o,$(PICAFILES))
export OFILES := $(OFILES_BIN) $(OFILES_SOURCES)

export HFILES := $(addsuffix .h,$(subst .,_,$(BINFILES))) \
		 $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.h))) \
		 $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.hpp)))

export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
		  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
		  -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export _3DSXDEPS := $(OUTPUT).smdh

ifeq ($(strip $(ROMFS)),)
else
ifneq ($(wildcard $(CURDIR)/$(ROMFS)),)
	export _3DSXFLAGS += --romfs=$(CURDIR)/$(ROMFS)
endif
endif

.PHONY: $(BUILD) clean all 3dsx cia

all: $(BUILD)

3dsx: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

cia: all
	@if command -v makerom >/dev/null 2>&1; then \
		makerom -f cia -o $(OUTPUT).cia -elf $(OUTPUT).elf -rsf $(CURDIR)/meta/nuvio3ds.rsf -icon $(OUTPUT).smdh -target p; \
		echo "Built $(OUTPUT).cia"; \
	else \
		echo "makerom not found in PATH; place Project_CTR makerom in devkitPro/tools/bin or /usr/local/bin."; \
		exit 1; \
	fi

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(OUTPUT).3dsx $(OUTPUT).smdh $(OUTPUT).elf $(OUTPUT).cia

else

DEPENDS := $(OFILES:.o=.d)

$(OUTPUT).3dsx: $(OUTPUT).elf $(_3DSXDEPS)

$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif
