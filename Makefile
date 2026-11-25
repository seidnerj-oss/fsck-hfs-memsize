PRODUCT   := FsckMemsize
BUNDLE_ID := com.seidnerj.FsckMemsize
VERSION   := 1.0.0

LILU_REPO   := https://github.com/acidanthera/Lilu.git
LILU_REV    := dc01cb583295ceded4f42a6310a15e3abac1c025
SDK_REPO    := https://github.com/acidanthera/MacKernelSDK.git
SDK_REV     := 3f750085caa17ec3a7880f11c11bf4f48cd6a164

VENDOR := vendor
LILU   := $(VENDOR)/Lilu
SDK    := $(VENDOR)/MacKernelSDK
BUILD  := build

DEVELOPER_DIR ?= $(shell xcode-select -p)
export DEVELOPER_DIR
CXX      := $(shell xcrun --find clang++)
CC       := $(shell xcrun --find clang)
CC_KEXT  := $(shell dirname $$(xcrun --find clang))/../lib/clang/$(shell $(CC) -dumpversion | cut -d. -f1)/lib/darwin/libclang_rt.cc_kext.a

TARGET := -arch x86_64 -mmacosx-version-min=10.6
DEFS   := -DPRODUCT_NAME=$(PRODUCT) -DMODULE_VERSION=$(VERSION) -DBUNDLE_ID=$(BUNDLE_ID) -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT
INCS   := -nostdinc -I$(LILU)/Lilu -I$(LILU) -I$(SDK)/Headers
COMMON := $(TARGET) $(DEFS) $(INCS) -mkernel -fno-builtin -fno-common -fno-stack-protector -fno-asynchronous-unwind-tables \
          -fvisibility=hidden -Wall -Wextra -Wno-unused-parameter -Wno-stdlibcxx-not-found -Werror -Os
CXXFLAGS := $(COMMON) -std=c++17 -fapple-kext -fno-rtti -fno-exceptions -fno-threadsafe-statics
CFLAGS   := $(COMMON) -std=c11
LDFLAGS  := $(TARGET) -Xlinker -kext -nostdlib -L$(SDK)/Library/x86_64 -lkmod $(CC_KEXT)

SOURCES := src/kern_start.cpp $(LILU)/Lilu/Library/plugin_start.cpp

.PHONY: all release debug vendor clean

all: release debug

release: $(BUILD)/Release/$(PRODUCT).kext
debug: $(BUILD)/Debug/$(PRODUCT).kext

vendor: $(LILU)/.git $(SDK)/.git

$(LILU)/.git:
	git clone $(LILU_REPO) $(LILU) && git -C $(LILU) checkout --detach $(LILU_REV)

$(SDK)/.git:
	git clone $(SDK_REPO) $(SDK) && git -C $(SDK) checkout --detach $(SDK_REV)

$(BUILD)/%/$(PRODUCT).kext: $(SOURCES) src/kmod_info.c src/Info.plist.in Makefile | vendor
	rm -rf $@ $(BUILD)/$*/obj && mkdir -p $@/Contents/MacOS $(BUILD)/$*/obj
	$(CXX) $(CXXFLAGS) $(if $(filter Debug,$*),-DDEBUG=1) -c src/kern_start.cpp -o $(BUILD)/$*/obj/kern_start.o
	$(CXX) $(CXXFLAGS) $(if $(filter Debug,$*),-DDEBUG=1) -c $(LILU)/Lilu/Library/plugin_start.cpp -o $(BUILD)/$*/obj/plugin_start.o
	$(CC) $(CFLAGS) -c src/kmod_info.c -o $(BUILD)/$*/obj/kmod_info.o
	$(CXX) $(LDFLAGS) $(BUILD)/$*/obj/*.o -o $@/Contents/MacOS/$(PRODUCT)
	sed -e 's/@PRODUCT@/$(PRODUCT)/g' -e 's/@BUNDLE_ID@/$(BUNDLE_ID)/g' -e 's/@VERSION@/$(VERSION)/g' src/Info.plist.in > $@/Contents/Info.plist
	plutil -lint $@/Contents/Info.plist

clean:
	rm -rf $(BUILD)

.PHONY: test
test: $(BUILD)/core_test
	$(BUILD)/core_test

$(BUILD)/core_test: tests/core_test.cpp src/fsck_memsize.hpp
	mkdir -p $(BUILD)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -isysroot $(shell xcrun --sdk macosx --show-sdk-path) tests/core_test.cpp -o $@
