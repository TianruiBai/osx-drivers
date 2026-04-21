include common/kext_info.mk

NATIVE_TIGER	?=	0

BUILD_PKG	:=	build_pkg
KEXTS		:=	WiiAudio WiiEXI WiiGraphics WiiPlatform WiiStorage WiiUSB
MKEXT_NAME	:=	Wii.mkext
ARCHIVE_ZIP	:= 	Wiintosh-osx-drivers-$(KEXT_VERSION).zip

ifeq ($(NATIVE_TIGER),1)
MKEXT_TOOL	:=	kextcache -z -m $(MKEXT_NAME) $(BUILD_PKG)/Kexts
ZIP_FLAGS	:=	-qry
else
MKEXT_TOOL	:=	./make-mkext.py $(BUILD_PKG)/Kexts $(MKEXT_NAME)
ZIP_FLAGS	:=	-qry -FS
endif

.PHONY: all clean

all:
	$(foreach kext,$(KEXTS),$(MAKE) -C $(kext) all &&) true

	rm -rf $(BUILD_PKG)
	$(foreach kext,$(KEXTS),mkdir -p $(BUILD_PKG)/Kexts/$(kext).kext &&) true
	$(foreach kext,$(KEXTS),cp -r $(kext)/build_kext/$(kext).kext $(BUILD_PKG)/Kexts &&) true

	$(MKEXT_TOOL)
	cp $(MKEXT_NAME) $(BUILD_PKG)
	cd $(BUILD_PKG); zip $(ZIP_FLAGS) ../$(ARCHIVE_ZIP) *

clean:
	rm -rf $(BUILD_PKG)
	rm -rf $(MKEXT_NAME)
	rm -rf $(ARCHIVE_ZIP)
	$(foreach kext,$(KEXTS),$(MAKE) -C $(kext) clean &&) true
