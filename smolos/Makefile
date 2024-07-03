RYFS := ../ryfs/ryfs.py
FOX32ASM := ../../fox32asm/target/release/fox32asm
OKAMERON := ../okameron/okameron.lua
GFX2INC := ../../tools/gfx2inc/target/release/gfx2inc

IMAGE_SIZE := 16777216
ROM_IMAGE_SIZE := 65536
BOOTLOADER := bootloader/bootloader.bin

all: fox32os.img romdisk.img

base_image:
	mkdir -p base_image

base_image/kernel.fxf: kernel/main.asm $(wildcard kernel/*.asm kernel/*/*.asm)
	$(FOX32ASM) $< $@

base_image/sh.fxf: applications/sh/main.asm $(wildcard applications/sh/*.asm applications/sh/*/*.asm)
	$(FOX32ASM) $< $@

base_image/ted.fxf: applications/ted/TEd.okm $(wildcard applications/ted/*.okm)
	lua $(OKAMERON) -arch=fox32 -startup=applications/ted/start.asm $< \
		applications/ted/OS.okm \
		> applications/ted/ted.asm
	$(FOX32ASM) applications/ted/ted.asm $@
	rm applications/ted/ted.asm

bootloader/bootloader.bin: bootloader/main.asm $(wildcard bootloader/*.asm)
	$(FOX32ASM) $< $@

base_image/startup.bat: base_image/startup.bat.default
	cp $< $@

FILES = \
	base_image/startup.bat \
	base_image/kernel.fxf \
	base_image/sh.fxf \
	base_image/ted.fxf

ROM_FILES = \
	base_image/startup.bat \
	base_image/kernel.fxf \
	base_image/sh.fxf \
	base_image/ted.fxf

fox32os.img: $(BOOTLOADER) $(FILES)
	$(RYFS) -s $(IMAGE_SIZE) -l fox32os -b $(BOOTLOADER) create $@.tmp
	for file in $(FILES); do $(RYFS) add $@.tmp $$file; done
	mv $@.tmp $@

romdisk.img: $(BOOTLOADER) $(ROM_FILES)
	$(RYFS) -s $(ROM_IMAGE_SIZE) -l romdisk -b $(BOOTLOADER) create $@.tmp
	for file in $(ROM_FILES); do $(RYFS) add $@.tmp $$file; done
	mv $@.tmp $@

clean:
	rm -f $(FILES)
