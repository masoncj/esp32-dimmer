PROJECT_NAME := esp-dimmer

WEBPACK := ./node_modules/.bin/webpack

include $(IDF_PATH)/make/project.mk

flash-romfs: build/main/romfs.img
	$(IDF_PATH)/components/esptool_py/esptool/esptool.py \
		--chip esp32 \
		--port $(ESPPORT) \
		write_flash 0x310000 build/main/romfs.img

./node_modules/.bin/webpack: 
	node install

webpack: ${WEBPACK}
	${WEBPACK} --config webpack.config.js

web-fs/main.js: webpack
	${WEBPACK} 

build/main/romfs.img: web-fs/main.js
	genromfs -f build/main/romfs.img -d web-ifs
