#
# WiiPlatform kernel extension - Wii and Wii U platform support
#
KEXT_NAME		:= WiiPlatform
SOURCES			:= src src/Interrupts src/IPC src/PE

ifeq ($(NATIVE_TIGER),1)
PLIST_FILE		:= Info.tiger.plist
EXCLUDED_CXXFILES	+= src/PE/WiiCPU.cpp
endif

include ../common/kext.mk
