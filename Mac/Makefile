# KO SimpleFill — macOS build
# SDK は AE_SDK_ROOT で指定（このリポジトリは sdk を同梱していない）

REPO_ROOT   = $(dir $(abspath $(lastword $(MAKEFILE_LIST))))..
AE_SDK_ROOT ?= $(REPO_ROOT)/sdk

DIST      = $(REPO_ROOT)/dist
BUILD_DIR = $(REPO_ROOT)/Mac/build

SDK_HDRS      = $(AE_SDK_ROOT)/Headers
SDK_UTIL      = $(AE_SDK_ROOT)/Util
SDK_RESOURCES = $(AE_SDK_ROOT)/Resources

CXXFLAGS = \
	-std=c++14 \
	-arch x86_64 -arch arm64 \
	-mmacosx-version-min=10.15 \
	-O2 \
	-I$(REPO_ROOT) \
	-I$(SDK_HDRS) \
	-I$(SDK_HDRS)/SP \
	-I$(SDK_RESOURCES) \
	-I$(SDK_UTIL) \
	-fvisibility=hidden \
	-fvisibility-inlines-hidden

LDFLAGS = \
	-arch x86_64 -arch arm64 \
	-bundle \
	-mmacosx-version-min=10.15 \
	-Wl,-exported_symbol,_EffectMain \
	-Wl,-exported_symbol,_PluginDataEntryFunction2

REZ_FLAGS = \
	-d AE_OS_MAC \
	-I $(SDK_HDRS) \
	-I $(SDK_HDRS)/SP \
	-I $(SDK_RESOURCES) \
	-I $(REPO_ROOT)

.PHONY: all clean

BUNDLE    = $(DIST)/KO_SimpleFill.plugin
MACOS     = $(BUNDLE)/Contents/MacOS
RESOURCES = $(BUNDLE)/Contents/Resources
BINARY    = $(MACOS)/KO_SimpleFill
RSRC      = $(BUILD_DIR)/KO_SimpleFill.rsrc
OBJS      = $(BUILD_DIR)/KO_SimpleFill.o \
            $(BUILD_DIR)/AEGP_SuiteHandler.o \
            $(BUILD_DIR)/MissingSuiteError.o

# plist はルールではなくレシピ内で無条件にコピーする。ルールにするとタイムスタンプ判定が
# 効き、KO BandFill では古い plist を何ヶ月も出荷し続けた。PkgInfo は type と creator の
# 8バイトで、Adobe のサンプルバンドルには必ず入っている。
all: $(BINARY) $(RSRC) | $(RESOURCES) $(BUNDLE)/Contents
	cp $(REPO_ROOT)/Mac/Info.plist $(BUNDLE)/Contents/Info.plist
	printf 'eFKTFXTC' > $(BUNDLE)/Contents/PkgInfo
	cp $(RSRC) $(RESOURCES)/KO_SimpleFill.rsrc

$(BUILD_DIR)/KO_SimpleFill.o: $(REPO_ROOT)/KO_SimpleFill.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/AEGP_SuiteHandler.o: $(SDK_UTIL)/AEGP_SuiteHandler.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/MissingSuiteError.o: $(SDK_UTIL)/MissingSuiteError.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# -useDF はリソースマップをデータフォークに書く。付けないとリソースフォークに書かれ、
# コピーや zip で中身だけが消えて「エフェクトの無いバンドル」になる。MediaCore は
# この .rsrc を読むので、空だとロードされない。検査までが対になっている。
$(RSRC): $(REPO_ROOT)/KO_SimpleFillPiPL.r | $(BUILD_DIR)
	xcrun Rez -useDF $(REZ_FLAGS) $< -o $@
	@test -s $@ || { echo "*** PiPL resource is EMPTY: $@" >&2; exit 1; }
	@ls -l $@

$(BINARY): $(OBJS) | $(MACOS)
	$(CXX) $(LDFLAGS) $(OBJS) -o $@

$(BUILD_DIR) $(MACOS) $(BUNDLE)/Contents $(RESOURCES):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)
	rm -rf $(BUNDLE)
