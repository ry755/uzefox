###############################################################################
# Makefile for the project UzeFox
###############################################################################

## General Flags
GAME    = uzefox
INFO    = gameinfo.properties
MCU     = atmega644
TARGET  = $(GAME).elf
CC      = avr-gcc
OUTDIR  = bin
OBJDIR  = obj
DEPDIR  = dep
DIRS    = $(OUTDIR) $(OBJDIR) $(DEPDIR)

## Kernel settings
UZEBIN_DIR = kernel-tools/bin
KERNEL_DIR = kernel-tools/kernel
KERNEL_OPTIONS = -DVIDEO_MODE=41 -DINTRO_LOGO=0 -DM40_IBM_ASCII=1 -DSOUND_MIXER=MIXER_TYPE_INLINE


## Options common to compile, link and assembly rules
COMMON = -mmcu=$(MCU)

## Compile options common for all C compilation units.
CFLAGS  = $(COMMON)
CFLAGS += -Wall -gdwarf-2 -std=gnu99 -DF_CPU=28636360UL -O2 -fsigned-char
CFLAGS += -ffunction-sections -fno-toplevel-reorder -fno-tree-switch-conversion
CFLAGS += -MD -MP -MT $(*F).o -MF $(DEPDIR)/$(@F).d
CFLAGS += $(KERNEL_OPTIONS)

## Assembly specific flags
ASMFLAGS  = $(COMMON)
ASMFLAGS += $(CFLAGS)
ASMFLAGS += -x assembler-with-cpp -Wa,-gdwarf2

## Linker flags
LDFLAGS  = $(COMMON)
LDFLAGS += -Wl,-Map=$(OUTDIR)/$(GAME).map
LDFLAGS += -Wl,-gc-sections

## Intel Hex file production flags
HEX_FLASH_FLAGS = -R .eeprom


## Objects that must be built in order to link
OBJECTS  = $(OBJDIR)/uzeboxVideoEngineCore.o
OBJECTS += $(OBJDIR)/uzeboxCore.o
OBJECTS += $(OBJDIR)/uzeboxSoundEngine.o
OBJECTS += $(OBJDIR)/uzeboxSoundEngineCore.o
OBJECTS += $(OBJDIR)/uzeboxVideoEngine.o
OBJECTS += $(OBJDIR)/bootlib.o
OBJECTS += $(OBJDIR)/spiram.o
OBJECTS += $(OBJDIR)/keyboard.o

OBJECTS += $(OBJDIR)/$(GAME).o
OBJECTS += $(OBJDIR)/bus.o
OBJECTS += $(OBJDIR)/cpu.o
OBJECTS += $(OBJDIR)/disk.o
OBJECTS += $(OBJDIR)/serial.o

## Include Directories
INCLUDES = -I. -I"$(KERNEL_DIR)"

## Build
all: $(UZEBIN_DIR)/packrom $(OUTDIR)/$(TARGET) $(OUTDIR)/$(GAME).hex $(OUTDIR)/$(GAME).lss $(OUTDIR)/$(GAME).uze size

$(UZEBIN_DIR)/packrom:
	mkdir -p $(UZEBIN_DIR)
	cd $(UZEBIN_DIR)/.. && $(MAKE) tools

## Directories
$(OBJDIR):
	mkdir $(OBJDIR)

$(OUTDIR):
	mkdir $(OUTDIR)

$(DEPDIR):
	mkdir $(DEPDIR)

## Compile Kernel files
$(OBJDIR)/uzeboxVideoEngineCore.o: $(KERNEL_DIR)/uzeboxVideoEngineCore.s $(DIRS)
	$(CC) $(INCLUDES) $(ASMFLAGS) -c $< -o $@

$(OBJDIR)/uzeboxSoundEngineCore.o: $(KERNEL_DIR)/uzeboxSoundEngineCore.s $(DIRS)
	$(CC) $(INCLUDES) $(ASMFLAGS) -c $< -o $@

$(OBJDIR)/uzeboxCore.o: $(KERNEL_DIR)/uzeboxCore.c $(DIRS)
	$(CC) $(INCLUDES) $(CFLAGS) -c $< -o $@

$(OBJDIR)/uzeboxSoundEngine.o: $(KERNEL_DIR)/uzeboxSoundEngine.c $(DIRS)
	$(CC) $(INCLUDES) $(CFLAGS) -c $< -o $@

$(OBJDIR)/uzeboxVideoEngine.o: $(KERNEL_DIR)/uzeboxVideoEngine.c $(DIRS)
	$(CC) $(INCLUDES) $(CFLAGS) -c $< -o $@

$(OBJDIR)/spiram.o: $(KERNEL_DIR)/spiram.s $(DIRS)
	$(CC) $(INCLUDES) $(ASMFLAGS) -c $< -o $@

$(OBJDIR)/bootlib.o: $(KERNEL_DIR)/bootlib.s $(DIRS)
	$(CC) $(INCLUDES) $(ASMFLAGS) -c $< -o $@

$(OBJDIR)/keyboard.o: $(KERNEL_DIR)/keyboard.c $(DIRS)
	$(CC) $(INCLUDES) $(CFLAGS) -c $< -o $@

## Compile game sources
$(OBJDIR)/$(GAME).o: main.c $(DIRS)
	$(CC) $(INCLUDES) $(CFLAGS) -c $< -o $@
$(OBJDIR)/bus.o: bus.c $(DIRS)
	$(CC) $(INCLUDES) $(CFLAGS) -c $< -o $@
$(OBJDIR)/cpu.o: cpu.c $(DIRS)
	$(CC) $(INCLUDES) $(CFLAGS) -c $< -o $@
$(OBJDIR)/disk.o: disk.c $(DIRS)
	$(CC) $(INCLUDES) $(CFLAGS) -c $< -o $@
$(OBJDIR)/serial.o: serial.c $(DIRS)
	$(CC) $(INCLUDES) $(CFLAGS) -c $< -o $@

## Link
$(OUTDIR)/$(TARGET): $(OBJECTS) $(DIRS)
	$(CC) $(LDFLAGS) $(OBJECTS) $(LIBDIRS) $(LIBS) -o $(OUTDIR)/$(TARGET)

$(OUTDIR)/%.hex: $(OUTDIR)/$(TARGET)
	avr-objcopy -O ihex $(HEX_FLASH_FLAGS) $< $@

$(OUTDIR)/%.lss: $(OUTDIR)/$(TARGET)
	avr-objdump -h -S $< > $@

$(OUTDIR)/%.uze: $(OUTDIR)/$(TARGET)
	$(UZEBIN_DIR)/packrom $(OUTDIR)/$(GAME).hex $@ $(INFO)

UNAME := $(shell sh -c 'uname -s 2>/dev/null || echo not')
AVRSIZEFLAGS := -A $(OUTDIR)/${TARGET}
ifneq (,$(findstring MINGW,$(UNAME)))
AVRSIZEFLAGS := -C --mcu=${MCU} $(OUTDIR)/${TARGET}
endif

size: $(OUTDIR)/${TARGET}
	@echo
	@avr-size ${AVRSIZEFLAGS}

## Clean target
.PHONY: clean
clean:
	-rm -rf $(DIRS)
