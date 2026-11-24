PROJ_ROOT := $(shell pwd)
ATH10K    := $(HOME)/Desktop/git/Ath10kMac
BUILD_DIR := $(PROJ_ROOT)/build
OUT_KEXT  := $(BUILD_DIR)/out/QcaBtMac.kext
OUT_BIN   := $(OUT_KEXT)/Contents/MacOS/QcaBtMac

SDK    = $(shell xcrun --show-sdk-path)
MKSDK := $(ATH10K)/MacKernelSDK
ARCH  := -arch x86_64
MINOS := -mmacosx-version-min=11.0

KFLAGS = -fstack-usage -Wframe-larger-than=4096 -Werror=frame-larger-than -O2 -fno-stack-check -fno-stack-protector -mkernel $(ARCH) $(MINOS) \
         -isysroot $(SDK) \
         -I$(MKSDK)/Headers \
         -I$(SDK)/System/Library/Frameworks/Kernel.framework/Headers

CXXFLAGS = $(KFLAGS) -fapple-kext -fno-exceptions -fno-rtti -std=c++17 \
           -DKERNEL -Wno-deprecated-declarations -Wno-nullability-completeness

LDFLAGS = $(ARCH) -static -nostdlib -Xlinker -kext \
          $(MKSDK)/Library/x86_64/libkmod.a \
          $(MKSDK)/Library/universal/libkmodc++.a \
          -F$(SDK)/System/Library/Frameworks

OBJS := $(BUILD_DIR)/QcaBtUSBDevice.o $(BUILD_DIR)/kmod_info.o

.PHONY: all kext prepare-firmware clean
all: kext

prepare-firmware:
	python3 scripts/prepare-bt-firmware.py

src/QcaBtPayload.hpp: prepare-firmware

kext: $(OUT_BIN)
	python3 $(ATH10K)/scripts/check-kext-symbols.py "$(OUT_BIN)"

$(BUILD_DIR):
	@mkdir -p $@

$(BUILD_DIR)/QcaBtUSBDevice.o: src/QcaBtUSBDevice.cpp src/QcaBtUSBDevice.hpp src/QcaBtPayloadProfile.hpp src/QcaBtPayload.hpp | $(BUILD_DIR)
	xcrun clang++ $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/kmod_info.o: src/kmod_info.c | $(BUILD_DIR)
	xcrun clang $(KFLAGS) -c $< -o $@

$(OUT_BIN): $(OBJS) QcaBtMac.kext/Contents/Info.plist
	@mkdir -p "$(OUT_KEXT)/Contents/MacOS"
	@cp QcaBtMac.kext/Contents/Info.plist "$(OUT_KEXT)/Contents/Info.plist"
	xcrun clang++ $(LDFLAGS) -o $@ $(OBJS)

clean:
	rm -rf $(BUILD_DIR)
