PROJECT_NAME := esp-dimmer

# Third Party Components (included via git submodules):
DUK_PATH := $(abspath components/duktape-esp32)
MKSPIFFS_PATH := $(abspath tools/Lua-RTOS-ESP32/components/mkspiffs/src)
MKESPFS_PATH := $(abspath tools/libesphttpd/espfs/mkespfsimage)

EXTRA_COMPONENT_DIRS := ${DUK_PATH}/components

SRCDIRS += main

include $(IDF_PATH)/make/project.mk

$(MKSPIFFS_PATH)/mkspiffs:
	CC=clang $(MAKE) -C $(MKSPIFFS_PATH)

$(MKESPFS_PATH)/mkespfsimage:
	CC=clang $(MAKE) -C $(MKESPFS_PATH)

images: $(MKSPIFFS_PATH)/mkspiffs $(MKESPFS_PATH)/mkespfsimage
	cd filesystem; find . -print | $(MKESPFS_PATH)/mkespfsimage -c 0 > ../build/espfs.img
	$(MKSPIFFS_PATH)/mkspiffs -c filesystem -b 65536 -p 256 -s 524288 build/spiffs.img

venv: venv/bin/activate
venv/bin/activate: requirements.txt
	test -d venv || virtualenv venv
	venv/bin/pip install -Ur requirements.txt
	touch venv/bin/activate

duktape_configure: venv
	source venv/bin/activate;\
	$(MAKE) -C $(DUK_PATH) duktape_configure

flash: flash-images

flash-images: images
	$(ESPTOOLPY_WRITE_FLASH) --compress 0x360000 build/espfs.img 0x180000 build/spiffs.img

clean: local-clean

local-clean:
	- rm -f $(MKSPIFFS_PATH)/mkspiffs
	- rm -f $(MKESPFS_PATH)/mkespfsimage
