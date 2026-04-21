NATIVE_TIGER	?=	0
MACOSX_VERSION_MIN ?=	10.4

#
# Toolchain configuration.
#
ifeq ($(NATIVE_TIGER),1)
AS 			:= 	/usr/bin/as
CC 			:= 	/usr/bin/gcc
CXX 		:= 	/usr/bin/g++
LD 			:= 	/usr/bin/ld
RUN_TOOL	:=
KERNEL_HEADERS	?=	/System/Library/Frameworks/Kernel.framework/Headers
SDK_USR_HEADERS	?=	/Developer/SDKs/MacOSX10.4u.sdk/usr/include
EXTRA_INCLUDES	:=	$(KERNEL_HEADERS) $(SDK_USR_HEADERS)
else
DARLING_SHELL	:= 	sudo darling shell
DARLING_CLT 	:= 	/Library/Developer/DarlingCLT/usr/bin
AS 			:= 	$(DARLING_CLT)/as
CC 			:= 	$(DARLING_CLT)/powerpc-apple-darwin10-gcc-4.2.1
CXX 		:= 	$(DARLING_CLT)/powerpc-apple-darwin10-g++-4.2.1
LD 			:= 	$(DARLING_CLT)/ld_classic
RUN_TOOL	:=	$(DARLING_SHELL)
EXTRA_INCLUDES	:=	../MacPPCKernelSDK/Headers
endif

#
# Toolchain flags.
#
ASFLAGS	=
CFLAGS  = 	-fno-builtin -fno-common -mlong-branch -finline -fno-keep-inline-functions
CFLAGS	+=	-fmessage-length=0  -force_cpusubtype_ALL -static -nostdlib -r
ifeq ($(NATIVE_TIGER),0)
CFLAGS	+=	-nostdinc
endif
CFLAGS	+=	-D__KERNEL__ -DKERNEL -DDEBUG -Wall -mmacosx-version-min=$(MACOSX_VERSION_MIN) $(INCLUDE)
CFLAGS	+=	-DKPI_10_4_0_PPC_COMPAT=1
ifeq ($(NATIVE_TIGER),1)
CFLAGS	+=	-DWII_TIGER_SDK=1 -DWII_TIGER_IOINTERRUPT_API=1
endif
CXXFLAGS	=	$(CFLAGS) -x c++ -fapple-kext -fno-rtti -fno-exceptions -fcheck-new

#
# Folders. These must be relative to the kext Makefiles, Darling can't deal with absolute paths.
#
BUILD			:=	build
BUILD_KEXT	:= build_kext
INCLUDES	:=	$(EXTRA_INCLUDES) ../include $(SOURCES)
PLIST_FILE	?=	Info.plist
EXCLUDED_CFILES	?=
EXCLUDED_CXXFILES	?=

#
# Source and object files.
#
KMOD_CFILE		:= 	$(BUILD)/$(KEXT_NAME)-info.c
CFILES				:=	$(filter-out $(EXCLUDED_CFILES),$(foreach dir,$(SOURCES),$(wildcard $(dir)/*.c))) $(KMOD_CFILE)
CXXFILES			:=	$(filter-out $(EXCLUDED_CXXFILES),$(foreach dir,$(SOURCES),$(wildcard $(dir)/*.cpp)))
OFILES				:=	$(CFILES:%.c=$(BUILD)/%.o) $(CXXFILES:%.cpp=$(BUILD)/%.o)
KEXT_BIN			:=	$(BUILD)/$(KEXT_NAME)
KEXT_PLIST		:=	$(BUILD)/$(KEXT_NAME)-Info.plist
KEXT_BUNDLE		:=	$(BUILD_KEXT)/$(KEXT_NAME).kext

INCLUDE				:=	$(foreach dir,$(INCLUDES),-I$(dir))
DEPENDENCIES 	:= 	$(OFILES:.o=.d)

include ../common/kext_info.mk

.PHONY: $(BUILD) $(KEXT_BUNDLE) clean

all: $(KEXT_BUNDLE)

clean:
	rm -rf $(BUILD)
	rm -rf $(BUILD_KEXT)

# kmod info
$(KMOD_CFILE): ../common/kmod_info.c
	@test -d $(dir $@) || mkdir $(dir $@)
	@cat $< | sed -e s/__BUNDLE__/$(KEXT_BUNDLE_ID)/ \
		-e s/__MODULE__/$(KEXT_NAME)/ -e 's/-info[.]c//' \
		-e s/__VERSION__/$(KEXT_VERSION)/ > $@

# C source
$(BUILD)/%.o: %.c
	@mkdir -p $(@D)
	$(RUN_TOOL) $(CC) $(CFLAGS) -c $< -o $@

# C++ source
$(BUILD)/%.o: %.cpp
	@mkdir -p $(@D)
	$(RUN_TOOL) $(CXX) $(CXXFLAGS) -c $< -o $@

# Kext binary
$(KEXT_BIN): $(OFILES)
	$(RUN_TOOL) $(CC)  $^ $(CFLAGS) -o $@

# Kext plist
$(KEXT_PLIST): $(PLIST_FILE)
	@test -d $(dir $@) || mkdir $(dir $@)
	@cat $< | sed -e s/__BUNDLE__/$(KEXT_BUNDLE_ID)/ \
		-e s/__MODULE__/$(KEXT_NAME)/ \
		-e s/__VERSION__/$(KEXT_VERSION)/ > $@

# Kext bundle
$(KEXT_BUNDLE): $(KEXT_BIN) $(KEXT_PLIST)
	@test -d $(KEXT_BUNDLE) || mkdir -p $(KEXT_BUNDLE)
	@test -d $(KEXT_BUNDLE)/Contents || mkdir $(KEXT_BUNDLE)/Contents
	@test -d $(KEXT_BUNDLE)/Contents/MacOS || mkdir $(KEXT_BUNDLE)/Contents/MacOS

	@cp -f $(KEXT_PLIST) $(KEXT_BUNDLE)/Contents/Info.plist
	@cp -f $(KEXT_BIN) $(KEXT_BUNDLE)/Contents/MacOS/$(KEXT_NAME)

-include $(DEPENDENCIES)
