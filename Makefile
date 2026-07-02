CC ?= cc
HOST_CC ?= cc
CSTD ?= $(shell printf 'int main(void){return 0;}\n' | $(CC) -std=c23 -x c -c -o /dev/null - >/dev/null 2>&1 && printf 'c23' || printf 'c2x')
HOST_CSTD ?= $(shell printf 'int main(void){return 0;}\n' | $(HOST_CC) -std=c23 -x c -c -o /dev/null - >/dev/null 2>&1 && printf 'c23' || printf 'c2x')
TARGET ?= unix
VERSION ?= 0.1.0
WIN32_CC ?= x86_64-w64-mingw32-gcc
WIN32_CC_PATH ?= $(shell command -v "$(WIN32_CC)" 2>/dev/null || printf '%s' "$(WIN32_CC)")
WIN32_CXX ?= $(if $(filter %-gcc,$(WIN32_CC)),$(patsubst %-gcc,%-g++,$(WIN32_CC)),x86_64-w64-mingw32-g++)
WIN32_CXX_PATH ?= $(shell command -v "$(WIN32_CXX)" 2>/dev/null || printf '%s' "$(WIN32_CXX)")
WIN32_WINDRES ?= $(if $(filter %-gcc,$(WIN32_CC)),$(patsubst %-gcc,%-windres,$(WIN32_CC)),windres)
WIN32_OBJDUMP ?= $(if $(filter %-gcc,$(WIN32_CC)),$(patsubst %-gcc,%-objdump,$(WIN32_CC)),objdump)
WINE ?= wine
WINEPREFIX_DIR ?= $(abspath build/wineprefix)
CMAKE ?= cmake
CMAKE_GENERATOR ?= Unix Makefiles
CMAKE_MAKE_PROGRAM ?= $(shell command -v make 2>/dev/null || command -v gmake 2>/dev/null || printf make)
SDL2_SUBMODULE ?= vendor/SDL
SDL2_WIN32_BUILD_DIR ?= build/sdl2-win32-build
SDL2_WIN32_PREFIX ?= $(abspath build/sdl2-win32)
VERSION_PARTS := $(subst ., ,$(VERSION))
VERSION_MAJOR := $(or $(word 1,$(VERSION_PARTS)),0)
VERSION_MINOR := $(or $(word 2,$(VERSION_PARTS)),0)
VERSION_PATCH := $(or $(word 3,$(VERSION_PARTS)),0)
VERSION_COMMA := $(VERSION_MAJOR),$(VERSION_MINOR),$(VERSION_PATCH),0
BUILD_DIR := build/$(TARGET)
BIN := nightshift
SRC := src/main.c src/game.c src/audio.c
TEST_BIN := build/test_game
WIN32_MODEL_BIN := build/test_game-win32.exe
ICON_TOOL := build/make_ico
ZIP_TOOL := build/make_zip
BMP_CHECKER := build/check_bmp
VERSION_HEADER := $(BUILD_DIR)/nightshift_version.h
METAINFO := $(BUILD_DIR)/nightshift.metainfo.xml
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
DOCDIR ?= $(PREFIX)/share/doc/$(BIN)
DATADIR ?= $(PREFIX)/share
APPDIR ?= $(DATADIR)/applications
ICONDIR ?= $(DATADIR)/icons/hicolor/scalable/apps
METAINFODIR ?= $(DATADIR)/metainfo
DIST_DIR := dist
PACKAGE_NAME := $(BIN)-$(VERSION)-$(TARGET)
WIN32_INSTALLER_NAME := $(BIN)-$(VERSION)-win32-setup.exe
WIN32_INSTALLER := $(DIST_DIR)/$(WIN32_INSTALLER_NAME)
PKG_CONFIG ?= pkg-config
WIN32_PKG_CONFIG ?=
SDL_PREFIX ?=
SDL_DLL ?=
WINDRES ?= $(if $(filter %-gcc,$(CC)),$(patsubst %-gcc,%-windres,$(CC)),windres)

WARNINGS := -Wall -Wextra -Wpedantic -Wconversion -Wshadow
DEPFLAGS := -MMD -MP
CPPFLAGS ?=
override CPPFLAGS += -include $(VERSION_HEADER)
CFLAGS ?= -O2 -g
LDFLAGS ?=

ifeq ($(TARGET),win32)
	EXE := $(BIN).exe
	RES_OBJ := $(BUILD_DIR)/nightshift.res.o
	PACKAGE_DEPS := $(ZIP_TOOL)
ifneq ($(strip $(SDL_PREFIX)),)
	SDL_CFLAGS ?= -I$(SDL_PREFIX)/include/SDL2
	SDL_LIBS ?= -L$(SDL_PREFIX)/lib -lmingw32 -lSDL2main -lSDL2
else ifneq ($(strip $(WIN32_PKG_CONFIG)),)
	SDL_CFLAGS ?= $(shell $(WIN32_PKG_CONFIG) --cflags sdl2 2>/dev/null)
	SDL_LIBS ?= $(shell $(WIN32_PKG_CONFIG) --libs sdl2 2>/dev/null || printf -- "-lmingw32 -lSDL2main -lSDL2")
else
	SDL_CFLAGS ?=
	SDL_LIBS ?= -lmingw32 -lSDL2main -lSDL2
endif
	PLATFORM_LIBS := -luser32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 -lshell32 -lversion -luuid
ifneq ($(strip $(SDL_PREFIX)),)
	PACKAGE_SDL_DLL ?= $(SDL_PREFIX)/bin/SDL2.dll
else ifneq ($(strip $(SDL_DLL)),)
	PACKAGE_SDL_DLL ?= $(SDL_DLL)
else
	PACKAGE_SDL_DLL ?= $(shell cc_path="$$(command -v "$(CC)" 2>/dev/null)"; if [ -n "$$cc_path" ] && [ -f "$$(dirname "$$cc_path")/SDL2.dll" ]; then printf '%s\n' "$$(dirname "$$cc_path")/SDL2.dll"; fi)
endif
else
	EXE := $(BIN)
	RES_OBJ :=
	PACKAGE_DEPS :=
	SDL_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags sdl2)
	SDL_LIBS ?= $(shell $(PKG_CONFIG) --libs sdl2)
	PLATFORM_LIBS := -lm
	PACKAGE_SDL_DLL :=
endif

C_OBJ := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRC))
OBJ := $(C_OBJ) $(RES_OBJ)
DEP := $(C_OBJ:.o=.d)

.PHONY: all clean run smoke render-test audio-test screenshot-test input-test settings-test cli-test simulate help version test sdl2-win32 sdl2-win32-clean win32-vendored-package-check win32-vendored-wine-package-run-check win32-vendored-release-check win32-model-check win32-model-run-check win32-resource-check win32-dry-run win32-package-layout-check win32-wine-package-run-check win32-sdl-probe win32-sdl-probe-check win32-sdl-release-check docs-check ci-check version-check verify release-check clean-release-check metadata-check install install-check uninstall uninstall-check package package-check package-installer package-installer-check package-run-check check-sdl FORCE

all: $(EXE)

$(EXE): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(SDL_LIBS) $(PLATFORM_LIBS)

$(BUILD_DIR)/%.o: src/%.c $(VERSION_HEADER) | check-sdl $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(SDL_CFLAGS) $(CFLAGS) $(DEPFLAGS) -std=$(CSTD) $(WARNINGS) -c -o $@ $<

FORCE:

$(VERSION_HEADER): FORCE | $(BUILD_DIR)
	printf '%s\n' '#ifndef NIGHTSHIFT_VERSION' '#define NIGHTSHIFT_VERSION "$(VERSION)"' '#endif' > $@.tmp
	if ! cmp -s $@.tmp $@; then mv $@.tmp $@; else rm $@.tmp; fi

$(BUILD_DIR)/nightshift.manifest: packaging/nightshift.manifest.in FORCE | $(BUILD_DIR)
	sed -e 's/@VERSION@/$(VERSION)/g' $< > $@.tmp
	if ! cmp -s $@.tmp $@; then mv $@.tmp $@; else rm $@.tmp; fi

$(METAINFO): packaging/nightshift.metainfo.xml FORCE | $(BUILD_DIR)
	sed -E 's/<release version="[^"]*"/<release version="$(VERSION)"/' $< > $@.tmp
	if ! cmp -s $@.tmp $@; then mv $@.tmp $@; else rm $@.tmp; fi

$(ICON_TOOL): tools/make_ico.c | build
	$(HOST_CC) -std=$(HOST_CSTD) $(WARNINGS) -o $@ $<

$(ZIP_TOOL): tools/make_zip.c | build
	$(HOST_CC) -std=$(HOST_CSTD) $(WARNINGS) -o $@ $<

$(BMP_CHECKER): | build
	printf '%s\n' '#!/bin/sh' 'set -eu' 'test "$$#" -eq 1' 'path="$$1"' 'test -s "$$path"' 'signature=$$(od -An -tx1 -N2 "$$path" | tr -d " \n")' 'test "$$signature" = "424d"' 'width=$$(od -An -tu4 -j18 -N4 "$$path" | tr -d " \n")' 'height=$$(od -An -tu4 -j22 -N4 "$$path" | tr -d " \n")' 'test "$$width" = "960"' 'test "$$height" = "540"' > $@
	chmod +x $@

$(BUILD_DIR)/nightshift.ico: $(ICON_TOOL) | $(BUILD_DIR)
	$(ICON_TOOL) $@

$(BUILD_DIR)/nightshift.rc: packaging/nightshift.rc.in $(BUILD_DIR)/nightshift.manifest $(BUILD_DIR)/nightshift.ico FORCE | $(BUILD_DIR)
	sed -e 's/@VERSION@/$(VERSION)/g' -e 's/@VERSION_COMMA@/$(VERSION_COMMA)/g' -e 's|@MANIFEST@|$(BUILD_DIR)/nightshift.manifest|g' -e 's|@ICON@|$(BUILD_DIR)/nightshift.ico|g' $< > $@.tmp
	if ! cmp -s $@.tmp $@; then mv $@.tmp $@; else rm $@.tmp; fi

$(BUILD_DIR)/nightshift.res.o: $(BUILD_DIR)/nightshift.rc | $(BUILD_DIR)
	$(WINDRES) -O coff -o $@ $<

$(BUILD_DIR)/nightshift-installer.rc: packaging/nightshift_installer.rc.in $(BUILD_DIR)/nightshift.manifest $(BUILD_DIR)/nightshift.ico package FORCE | $(BUILD_DIR)
	sed -e 's/@VERSION@/$(VERSION)/g' -e 's/@VERSION_COMMA@/$(VERSION_COMMA)/g' -e 's|@MANIFEST@|$(BUILD_DIR)/nightshift.manifest|g' -e 's|@ICON@|$(BUILD_DIR)/nightshift.ico|g' -e 's|@PACKAGE_DIR@|$(DIST_DIR)/$(PACKAGE_NAME)|g' $< > $@.tmp
	if ! cmp -s $@.tmp $@; then mv $@.tmp $@; else rm $@.tmp; fi

$(BUILD_DIR)/nightshift-installer.res.o: $(BUILD_DIR)/nightshift-installer.rc package FORCE | $(BUILD_DIR)
	$(WINDRES) -O coff -o $@ $<

$(WIN32_INSTALLER): tools/nightshift_installer.c $(BUILD_DIR)/nightshift-installer.res.o FORCE | $(DIST_DIR)
	$(CC) -DNIGHTSHIFT_VERSION=\"$(VERSION)\" $(CFLAGS) -std=$(CSTD) $(WARNINGS) -o $@ tools/nightshift_installer.c $(BUILD_DIR)/nightshift-installer.res.o -mwindows -static -static-libgcc -lshell32 -lole32 -luuid

$(BUILD_DIR):
	mkdir -p $@

build:
	mkdir -p $@

$(DIST_DIR):
	mkdir -p $@

run: $(EXE)
	./$(EXE)

smoke: $(EXE)
	timeout 2s env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./$(EXE) --fast-night --save=/tmp/nightshift-smoke.save --settings=/tmp/nightshift-smoke.cfg; test $$? -eq 124
	timeout 2s env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./$(EXE) --fast-night --reset-save --save=/tmp/nightshift-reset-smoke.save --settings=/tmp/nightshift-reset-smoke.cfg; test $$? -eq 124

render-test: $(EXE)
	env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./$(EXE) --render-test --save=/tmp/nightshift-render.save --settings=/tmp/nightshift-render.cfg

audio-test: $(EXE)
	env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./$(EXE) --audio-test --save=/tmp/nightshift-audio.save --settings=/tmp/nightshift-audio.cfg

screenshot-test: $(EXE) $(BMP_CHECKER)
	env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./$(EXE) --screenshot=/tmp/nightshift-screenshot.bmp --save=/tmp/nightshift-shot.save --settings=/tmp/nightshift-shot.cfg
	$(BMP_CHECKER) /tmp/nightshift-screenshot.bmp
	env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./$(EXE) --custom-night=20,20,20,20 --screenshot-scene=title --screenshot=/tmp/nightshift-custom-title.bmp --save=/tmp/nightshift-custom-shot.save --settings=/tmp/nightshift-custom-shot.cfg
	$(BMP_CHECKER) /tmp/nightshift-custom-title.bmp
	set -e; for scene in title title-cleared extras office camera win loss-rust loss-volt loss-skitr loss-echo blackout; do \
		env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./$(EXE) --screenshot-scene=$$scene --screenshot=/tmp/nightshift-$$scene.bmp --save=/tmp/nightshift-$$scene.save --settings=/tmp/nightshift-$$scene.cfg; \
		$(BMP_CHECKER) /tmp/nightshift-$$scene.bmp; \
	done

input-test: $(EXE)
	./$(EXE) --input-test

settings-test: $(EXE)
	./$(EXE) --settings=/tmp/nightshift-settings-test.cfg --settings-test
	grep -q "^custom_ai_configured=1$$" /tmp/nightshift-settings-test.cfg
	grep -q "^custom_ai_1=4$$" /tmp/nightshift-settings-test.cfg
	grep -q "^custom_ai_2=8$$" /tmp/nightshift-settings-test.cfg
	grep -q "^custom_ai_3=12$$" /tmp/nightshift-settings-test.cfg
	grep -q "^custom_ai_4=16$$" /tmp/nightshift-settings-test.cfg
	rm -f /tmp/nightshift-settings-test.cfg /tmp/nightshift-settings-test.cfg.tmp

cli-test: $(EXE)
	if ./$(EXE) --unknown-option >/tmp/nightshift-cli.err 2>&1; then cat /tmp/nightshift-cli.err; exit 1; fi
	grep -q "unknown option" /tmp/nightshift-cli.err
	if ./$(EXE) --scale=9 >/tmp/nightshift-cli.err 2>&1; then cat /tmp/nightshift-cli.err; exit 1; fi
	grep -q "invalid --scale value" /tmp/nightshift-cli.err
	if ./$(EXE) --night-seconds=abc >/tmp/nightshift-cli.err 2>&1; then cat /tmp/nightshift-cli.err; exit 1; fi
	grep -q "invalid --night-seconds value" /tmp/nightshift-cli.err
	if ./$(EXE) --night-seconds=inf >/tmp/nightshift-cli.err 2>&1; then cat /tmp/nightshift-cli.err; exit 1; fi
	grep -q "invalid --night-seconds value" /tmp/nightshift-cli.err
	if ./$(EXE) --night=7 >/tmp/nightshift-cli.err 2>&1; then cat /tmp/nightshift-cli.err; exit 1; fi
	grep -q "invalid --night value" /tmp/nightshift-cli.err
	if ./$(EXE) --screenshot= >/tmp/nightshift-cli.err 2>&1; then cat /tmp/nightshift-cli.err; exit 1; fi
	grep -q "invalid --screenshot value" /tmp/nightshift-cli.err
	if ./$(EXE) --screenshot-scene=bad >/tmp/nightshift-cli.err 2>&1; then cat /tmp/nightshift-cli.err; exit 1; fi
	grep -q "invalid --screenshot-scene value" /tmp/nightshift-cli.err
	if ./$(EXE) --custom-night=21,0,0 >/tmp/nightshift-cli.err 2>&1; then cat /tmp/nightshift-cli.err; exit 1; fi
	grep -q "invalid --custom-night value" /tmp/nightshift-cli.err
	if ./$(EXE) --custom-night=1,1,1 >/tmp/nightshift-cli.err 2>&1; then cat /tmp/nightshift-cli.err; exit 1; fi
	grep -q "invalid --custom-night value" /tmp/nightshift-cli.err
	if ./$(EXE) --custom-night=1,1,1,1,1 >/tmp/nightshift-cli.err 2>&1; then cat /tmp/nightshift-cli.err; exit 1; fi
	grep -q "invalid --custom-night value" /tmp/nightshift-cli.err
	if ./$(EXE) --custom-night=1,1,1,1x >/tmp/nightshift-cli.err 2>&1; then cat /tmp/nightshift-cli.err; exit 1; fi
	grep -q "invalid --custom-night value" /tmp/nightshift-cli.err
	if ./$(EXE) --night=2 --custom-night=1,1,1,1 >/tmp/nightshift-cli.err 2>&1; then cat /tmp/nightshift-cli.err; exit 1; fi
	grep -q "invalid option combination" /tmp/nightshift-cli.err
	if ./$(EXE) --save= >/tmp/nightshift-cli.err 2>&1; then cat /tmp/nightshift-cli.err; exit 1; fi
	grep -q "invalid --save value" /tmp/nightshift-cli.err
	if ./$(EXE) --settings= >/tmp/nightshift-cli.err 2>&1; then cat /tmp/nightshift-cli.err; exit 1; fi
	grep -q "invalid --settings value" /tmp/nightshift-cli.err
	if ./$(EXE) --settings-test >/tmp/nightshift-cli.err 2>&1; then cat /tmp/nightshift-cli.err; exit 1; fi
	grep -q "missing_settings_path" /tmp/nightshift-cli.err
	if ./$(EXE) --simulate=bad >/tmp/nightshift-cli.err 2>&1; then cat /tmp/nightshift-cli.err; exit 1; fi
	grep -q "invalid --simulate value" /tmp/nightshift-cli.err

simulate: $(EXE)
	./$(EXE) --fast-night --simulate=defended
	set -e; for night in 1 2 3 4 5 6; do ./$(EXE) --fast-night --night=$$night --simulate=defended; done
	./$(EXE) --night-seconds=120 --simulate=idle
	./$(EXE) --fast-night --custom-night=0,0,0,0 --simulate=defended
	./$(EXE) --fast-night --custom-night=20,20,20,20 --simulate=defended

help: $(EXE)
	./$(EXE) --help

version: $(EXE)
	./$(EXE) --version

test: $(TEST_BIN)
	./$(TEST_BIN)

sdl2-win32:
	@if [ ! -f "$(SDL2_SUBMODULE)/CMakeLists.txt" ]; then \
		printf '%s\n' 'SDL2 submodule is missing. Run: git submodule update --init vendor/SDL'; \
		exit 2; \
	fi
	$(CMAKE) -S "$(SDL2_SUBMODULE)" -B "$(SDL2_WIN32_BUILD_DIR)" -G "$(CMAKE_GENERATOR)" \
		-DCMAKE_TOOLCHAIN_FILE="$(abspath $(SDL2_SUBMODULE))/build-scripts/cmake-toolchain-mingw64-x86_64.cmake" \
		-DCMAKE_C_COMPILER="$(WIN32_CC_PATH)" \
		-DCMAKE_CXX_COMPILER="$(WIN32_CXX_PATH)" \
		-DCMAKE_MAKE_PROGRAM="$(CMAKE_MAKE_PROGRAM)" \
		-DCMAKE_INSTALL_PREFIX="$(SDL2_WIN32_PREFIX)" \
		-DCMAKE_BUILD_TYPE=Release \
		-DSDL_SHARED=ON \
		-DSDL_STATIC=OFF \
		-DSDL_TEST=OFF \
		-DSDL_TESTS=OFF \
		-DSDL_INSTALL_TESTS=OFF
	$(CMAKE) --build "$(SDL2_WIN32_BUILD_DIR)" --target install
	test -s "$(SDL2_WIN32_PREFIX)/bin/SDL2.dll"
	test -s "$(SDL2_WIN32_PREFIX)/lib/libSDL2.dll.a"
	test -s "$(SDL2_WIN32_PREFIX)/lib/libSDL2main.a"
	printf '%s\n' "sdl2_win32=pass prefix=$(SDL2_WIN32_PREFIX)"

sdl2-win32-clean:
	rm -rf "$(SDL2_WIN32_BUILD_DIR)" "$(SDL2_WIN32_PREFIX)"

win32-vendored-package-check: sdl2-win32
	$(MAKE) TARGET=win32 CC="$(WIN32_CC)" SDL_PREFIX="$(SDL2_WIN32_PREFIX)" package-check package-installer-check

win32-vendored-wine-package-run-check: sdl2-win32
	$(MAKE) TARGET=win32 CC="$(WIN32_CC)" SDL_PREFIX="$(SDL2_WIN32_PREFIX)" win32-wine-package-run-check

win32-vendored-release-check: sdl2-win32
	$(MAKE) TARGET=win32 CC="$(WIN32_CC)" SDL_PREFIX="$(SDL2_WIN32_PREFIX)" win32-sdl-probe-check package-check package-installer-check win32-wine-package-run-check

win32-model-check: $(VERSION_HEADER) | build
	$(WIN32_CC) $(CPPFLAGS) $(CFLAGS) -std=$(CSTD) $(WARNINGS) -o $(WIN32_MODEL_BIN) tests/test_game.c src/game.c -lm

win32-model-run-check: win32-model-check
	@if command -v "$(WINE)" >/dev/null 2>&1 && WINEPREFIX="$(WINEPREFIX_DIR)" "$(WINE)" --version >/dev/null 2>&1; then \
		WINEPREFIX="$(WINEPREFIX_DIR)" WINEDLLOVERRIDES=mscoree,mshtml= "$(WINE)" "$(WIN32_MODEL_BIN)"; \
	else \
		printf '%s\n' "win32_model_run_check=skip reason=wine_unavailable"; \
	fi

win32-resource-check:
	$(MAKE) TARGET=win32 CC="$(WIN32_CC)" WINDRES="$(WIN32_WINDRES)" build/win32/nightshift.res.o
	$(WIN32_OBJDUMP) -x build/win32/nightshift.res.o > /tmp/nightshift-win32-resource.txt
	grep -q "Entry: ID: 0x000003" /tmp/nightshift-win32-resource.txt
	grep -q "Entry: ID: 0x00000e" /tmp/nightshift-win32-resource.txt
	grep -q "Entry: ID: 0x000010" /tmp/nightshift-win32-resource.txt
	grep -q "Entry: ID: 0x000018" /tmp/nightshift-win32-resource.txt
	strings -el build/win32/nightshift.res.o | grep -q "Night Shift SDL fangame"
	strings -el build/win32/nightshift.res.o | grep -q "nightshift.exe"
	strings build/win32/nightshift.res.o | grep -q 'requestedExecutionLevel level="asInvoker"'
	strings build/win32/nightshift.res.o | grep -q "PerMonitorV2, PerMonitor"
	strings build/win32/nightshift.res.o | grep -q "longPathAware"

win32-dry-run:
	tmp=$$(mktemp -d /tmp/nightshift-win32-dry.XXXXXX); \
	trap 'rm -rf "$$tmp"' EXIT; \
	printf '%s' fake > "$$tmp/SDL2.dll"; \
	$(MAKE) -n WIN32_CC="$(WIN32_CC)" SDL_CFLAGS="-I$$tmp" SDL_DLL="$$tmp/SDL2.dll" win32-sdl-release-check > "$$tmp/dry-run.log"; \
	grep -q "nightshift.exe" "$$tmp/dry-run.log"; \
	grep -q "SDL2.dll" "$$tmp/dry-run.log"; \
	grep -q ".zip" "$$tmp/dry-run.log"; \
	grep -q "package-installer-check" "$$tmp/dry-run.log"; \
	grep -q "unzip -q" "$$tmp/dry-run.log"; \
	grep -q -- "--settings-test" "$$tmp/dry-run.log"; \
	grep -q "package-run-check" "$$tmp/dry-run.log"; \
	grep -q "LICENSE" "$$tmp/dry-run.log"; \
	grep -q "nightshift.ico" "$$tmp/dry-run.log"; \
	grep -q "nightshift.manifest" "$$tmp/dry-run.log"; \
	grep -q "nightshift.res.o" "$$tmp/dry-run.log"; \
	grep -q -- "-o nightshift.exe" "$$tmp/dry-run.log"; \
	grep -q -- "-lmingw32 -lSDL2main -lSDL2" "$$tmp/dry-run.log"; \
	grep -q -- "-luser32" "$$tmp/dry-run.log"; \
	grep -q -- "-lgdi32" "$$tmp/dry-run.log"; \
	grep -q -- "-lwinmm" "$$tmp/dry-run.log"; \
	grep -q -- "-lshell32" "$$tmp/dry-run.log"; \
	grep -q -- "-lversion" "$$tmp/dry-run.log"; \
	grep -q 'cp "nightshift.exe"' "$$tmp/dry-run.log"; \
	grep -q 'cp README.md' "$$tmp/dry-run.log"; \
	grep -q 'cp HOW_TO_PLAY.md' "$$tmp/dry-run.log"; \
	grep -q 'cp packaging/WINDOWS.txt' "$$tmp/dry-run.log"; \
	grep -q "invalid option combination" "$$tmp/dry-run.log"; \
	printf '%s\n' "win32_dry_run=pass"

win32-package-layout-check: $(ZIP_TOOL)
	tmp=$$(mktemp -d /tmp/nightshift-win32-package.XXXXXX); \
	trap 'rm -rf "$$tmp"' EXIT; \
	pkg="$(BIN)-$(VERSION)-win32"; \
	mkdir -p "$$tmp/dist/$$pkg"; \
	printf '%s\n' "fake win32 executable" > "$$tmp/dist/$$pkg/$(BIN).exe"; \
	printf '%s\n' "fake sdl runtime" > "$$tmp/dist/$$pkg/SDL2.dll"; \
	cp README.md "$$tmp/dist/$$pkg/README.md"; \
	cp HOW_TO_PLAY.md "$$tmp/dist/$$pkg/HOW_TO_PLAY.md"; \
	cp packaging/WINDOWS.txt "$$tmp/dist/$$pkg/WINDOWS.txt"; \
	cp LICENSE "$$tmp/dist/$$pkg/LICENSE"; \
	printf '%s\n' "Name: Night Shift" "Version: $(VERSION)" "Target: win32" "Executable: $(BIN).exe" "Runtime: SDL2" "Docs: README.md HOW_TO_PLAY.md WINDOWS.txt LICENSE" "Package: $$pkg" > "$$tmp/dist/$$pkg/PACKAGE.txt"; \
	chmod +x "$$tmp/dist/$$pkg/$(BIN).exe"; \
	$(ZIP_TOOL) "$$tmp/dist/$$pkg.zip" "$$tmp/dist/$$pkg" "$(BIN).exe" "SDL2.dll" "README.md" "HOW_TO_PLAY.md" "WINDOWS.txt" "LICENSE" "PACKAGE.txt"; \
	cd "$$tmp/dist" && sha256sum "$$pkg.zip" > "$$pkg.zip.sha256"; \
	cd "$$tmp/dist" && sha256sum -c "$$pkg.zip.sha256"; \
	unzip -Z1 "$$tmp/dist/$$pkg.zip" > "$$tmp/package-zip.lst"; \
	unzip -t "$$tmp/dist/$$pkg.zip" >/dev/null; \
	grep -q "^$(BIN).exe$$" "$$tmp/package-zip.lst"; \
	grep -q "^SDL2.dll$$" "$$tmp/package-zip.lst"; \
	grep -q "^README.md$$" "$$tmp/package-zip.lst"; \
	grep -q "^HOW_TO_PLAY.md$$" "$$tmp/package-zip.lst"; \
	grep -q "^WINDOWS.txt$$" "$$tmp/package-zip.lst"; \
	grep -q "^LICENSE$$" "$$tmp/package-zip.lst"; \
	grep -q "^PACKAGE.txt$$" "$$tmp/package-zip.lst"; \
	if grep -Eq '(^|/)(build|dist)/|\.tmp$$|\.save$$|\.cfg$$|\.bmp$$|\.o$$|\.res$$|\.res\.o$$' "$$tmp/package-zip.lst"; then \
		printf '%s\n' 'win32 zip package contains generated, local state, or temporary files'; \
		exit 1; \
	fi; \
	mkdir -p "$$tmp/zip-extract"; \
	unzip -q "$$tmp/dist/$$pkg.zip" -d "$$tmp/zip-extract"; \
	test -x "$$tmp/zip-extract/$(BIN).exe"; \
	test -s "$$tmp/zip-extract/SDL2.dll"; \
	test -s "$$tmp/zip-extract/WINDOWS.txt"; \
	grep -q "SDL2.dll" "$$tmp/zip-extract/WINDOWS.txt"; \
	grep -q "^Target: win32$$" "$$tmp/zip-extract/PACKAGE.txt"; \
	grep -q "^Executable: $(BIN).exe$$" "$$tmp/zip-extract/PACKAGE.txt"; \
	printf '%s\n' "win32_package_layout=pass"

win32-wine-package-run-check: package
ifeq ($(TARGET),win32)
	@if command -v "$(WINE)" >/dev/null 2>&1 && WINEPREFIX="$(WINEPREFIX_DIR)" "$(WINE)" --version >/dev/null 2>&1; then \
		set -e; tmp=$$(mktemp -d /tmp/nightshift-win32-wine.XXXXXX); \
		trap 'rm -rf "$$tmp"' EXIT; \
		mkdir -p "$$tmp/zip"; \
		unzip -q "$(DIST_DIR)/$(PACKAGE_NAME).zip" -d "$$tmp/zip"; \
		test -s "$$tmp/zip/SDL2.dll"; \
		WINEPREFIX="$(WINEPREFIX_DIR)" WINEDLLOVERRIDES=mscoree,mshtml= "$(WINE)" "$$tmp/zip/$(EXE)" --version | tr -d '\r' | grep -q "^Night Shift $(VERSION)$$"; \
		WINEPREFIX="$(WINEPREFIX_DIR)" WINEDLLOVERRIDES=mscoree,mshtml= "$(WINE)" "$$tmp/zip/$(EXE)" --help | tr -d '\r' | grep -q "^Usage:"; \
		SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy WINEPREFIX="$(WINEPREFIX_DIR)" WINEDLLOVERRIDES=mscoree,mshtml= "$(WINE)" "$$tmp/zip/$(EXE)" --render-test --save=Z:/tmp/nightshift-wine-render.save --settings=Z:/tmp/nightshift-wine-render.cfg | tr -d '\r' | grep -q "^render_test=pass"; \
		SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy WINEPREFIX="$(WINEPREFIX_DIR)" WINEDLLOVERRIDES=mscoree,mshtml= "$(WINE)" "$$tmp/zip/$(EXE)" --audio-test --save=Z:/tmp/nightshift-wine-audio.save --settings=Z:/tmp/nightshift-wine-audio.cfg | tr -d '\r' | grep -q "^audio_test=pass"; \
		printf '%s\n' "win32_wine_package_run_check=pass"; \
	else \
		printf '%s\n' "win32_wine_package_run_check=skip reason=wine_unavailable"; \
	fi
else
	@printf '%s\n' "win32_wine_package_run_check=skip target=$(TARGET)"
endif

win32-sdl-probe:
	$(MAKE) TARGET=win32 CC="$(WIN32_CC)" WIN32_PKG_CONFIG="$(WIN32_PKG_CONFIG)" SDL_PREFIX="$(SDL_PREFIX)" SDL_DLL="$(SDL_DLL)" win32-sdl-probe-check

win32-sdl-probe-check: check-sdl
	tmp=$$(mktemp -d /tmp/nightshift-win32-sdl.XXXXXX); \
	trap 'rm -rf "$$tmp"' EXIT; \
	printf '%s\n' '#include <SDL.h>' 'int main(int argc, char **argv) { (void)argc; (void)argv; if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) return 2; SDL_Quit(); return 0; }' > "$$tmp/probe.c"; \
	if ! $(CC) $(SDL_CFLAGS) $(CFLAGS) -std=$(CSTD) -o "$$tmp/probe.exe" "$$tmp/probe.c" $(SDL_LIBS) $(PLATFORM_LIBS); then \
		printf '%s\n' '#include <SDL2/SDL.h>' 'int main(int argc, char **argv) { (void)argc; (void)argv; if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) return 2; SDL_Quit(); return 0; }' > "$$tmp/probe.c"; \
		$(CC) $(SDL_CFLAGS) $(CFLAGS) -std=$(CSTD) -o "$$tmp/probe.exe" "$$tmp/probe.c" $(SDL_LIBS) $(PLATFORM_LIBS); \
	fi; \
	test -s "$$tmp/probe.exe"; \
	if [ -z "$(PACKAGE_SDL_DLL)" ] || [ ! -f "$(PACKAGE_SDL_DLL)" ]; then \
		printf '%s\n' 'Win32 SDL2.dll was not found for packaging.'; \
		printf '%s\n' 'Pass SDL_PREFIX=/path/to/mingw-sdl2 or SDL_DLL=/path/to/SDL2.dll.'; \
		exit 2; \
	fi; \
	printf '%s\n' "win32_sdl_probe=pass cc=$(CC) dll=$(PACKAGE_SDL_DLL)"

win32-sdl-release-check:
	$(MAKE) TARGET=win32 CC="$(WIN32_CC)" WIN32_PKG_CONFIG="$(WIN32_PKG_CONFIG)" SDL_PREFIX="$(SDL_PREFIX)" SDL_DLL="$(SDL_DLL)" win32-sdl-probe-check verify package-check package-installer-check package-run-check

verify: all test simulate help version input-test settings-test cli-test render-test audio-test screenshot-test smoke metadata-check install-check uninstall-check

release-check: version-check verify package-check package-run-check win32-model-check win32-model-run-check win32-resource-check win32-dry-run win32-package-layout-check docs-check ci-check

clean-release-check:
	$(MAKE) clean
	$(MAKE) release-check

docs-check:
	test "$$(awk '/^```/{n++} END{print n % 2}' README.md)" = "0"
	test "$$(awk '/^```/{n++} END{print n % 2}' HOW_TO_PLAY.md)" = "0"
	grep -q "make clean-release-check" README.md
	grep -q "make settings-test" README.md
	grep -q "win32-sdl-release-check" README.md
	grep -q "make win32-model-run-check" README.md
	grep -q "make win32-resource-check" README.md
	grep -q "git submodule update --init vendor/SDL" README.md
	grep -q "make sdl2-win32" README.md
	grep -q "release-2.32.8" README.md
	grep -q "win32-wine-package-run-check" README.md
	grep -q "win32-vendored-release-check" README.md
	grep -q "make package-run-check" README.md
	grep -q "make win32-sdl-probe" README.md
	grep -q "make TARGET=win32 CC=x86_64-w64-mingw32-gcc" README.md
	grep -q "HOW_TO_PLAY.md" README.md
	grep -q "Custom Night" HOW_TO_PLAY.md
	grep -q "## Threats" HOW_TO_PLAY.md
	grep -q "## Power And Blackout" HOW_TO_PLAY.md
	grep -q "SDL2.dll" HOW_TO_PLAY.md
	grep -q "nightshift.save" HOW_TO_PLAY.md
	grep -q "WINDOWS.txt" README.md
	grep -q "win32.zip" README.md
	grep -q "win32-setup.exe" README.md
	grep -Eq "no Win32 .*tar.gz.* is generated" README.md
	grep -q "Browse button" README.md
	grep -q "/S /D=C" README.md
	grep -q "make TARGET=win32 CC=x86_64-w64-mingw32-gcc package-installer" README.md
	grep -q "native Windows PowerShell" README.md
	test -s tools/make_zip.c
	test -s tools/nightshift_installer.c
	test -s packaging/WINDOWS.txt
	test -s packaging/nightshift_installer.rc.in
	grep -q "SDL2.dll" packaging/WINDOWS.txt
	grep -q "nightshift.exe" packaging/WINDOWS.txt

ci-check:
	grep -q "make release-check" .github/workflows/ci.yml
	grep -q "submodules: true" .github/workflows/ci.yml
	grep -q "make libsdl2-dev" .github/workflows/ci.yml
	grep -q "g++-mingw-w64-x86-64" .github/workflows/ci.yml
	grep -q "cmake unzip" .github/workflows/ci.yml
	grep -q "make win32-vendored-package-check" .github/workflows/ci.yml
	grep -q "make WIN32_CC=gcc WIN32_PKG_CONFIG=pkg-config win32-sdl-release-check" .github/workflows/ci.yml
	grep -q "actions/upload-artifact@v4" .github/workflows/ci.yml
	grep -q "nightshift-unix" .github/workflows/ci.yml
	grep -q "nightshift-win32" .github/workflows/ci.yml
	grep -q "dist/nightshift-\\*-win32.zip" .github/workflows/ci.yml
	grep -q "dist/nightshift-\\*-win32-setup.exe" .github/workflows/ci.yml
	grep -q "Verify Win32 artifacts" .github/workflows/ci.yml
	grep -q "sha256sum -c nightshift-\\*-win32.zip.sha256" .github/workflows/ci.yml
	grep -q "sha256sum -c nightshift-\\*-win32-setup.exe.sha256" .github/workflows/ci.yml
	grep -q "unzip -Z1 nightshift-\\*-win32.zip" .github/workflows/ci.yml
	@if grep -Eq "win32.*tar[.]gz" .github/workflows/ci.yml; then printf '%s\n' 'CI should not publish Win32 tarballs'; exit 1; fi
	grep -q "Native Win32 package run" .github/workflows/ci.yml
	grep -q "Start-Process" .github/workflows/ci.yml
	grep -q "Expand-Archive" .github/workflows/ci.yml
	grep -q "Push-Location" .github/workflows/ci.yml
	grep -q "version check failed.*exit=.*output=" .github/workflows/ci.yml
	grep -q "versionLines.*notcontains" .github/workflows/ci.yml
	grep -q "(?m)^Usage:" .github/workflows/ci.yml
	grep -q "Night Shift 0.1.0" .github/workflows/ci.yml

version-check:
	@if ! printf '%s\n' "$(VERSION)" | grep -Eq '^[0-9]+[.][0-9]+[.][0-9]+$$'; then \
		printf '%s\n' 'VERSION must be numeric major.minor.patch, for example 0.1.0'; \
		exit 1; \
	fi

$(TEST_BIN): tests/test_game.c src/game.c src/game.h $(VERSION_HEADER) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=$(CSTD) $(WARNINGS) -o $@ tests/test_game.c src/game.c -lm

metadata-check: $(BUILD_DIR)/nightshift.rc $(METAINFO)
	grep -q "^Type=Application$$" packaging/nightshift.desktop
	grep -q "^Name=Night Shift$$" packaging/nightshift.desktop
	grep -q "^Comment=Survive a haunted security shift$$" packaging/nightshift.desktop
	grep -q "^Exec=$(BIN)$$" packaging/nightshift.desktop
	grep -q "^Icon=$(BIN)$$" packaging/nightshift.desktop
	grep -q "^Categories=Game;$$" packaging/nightshift.desktop
	grep -q "<id>$(BIN).desktop</id>" "$(METAINFO)"
	grep -q "<metadata_license>CC0-1.0</metadata_license>" "$(METAINFO)"
	grep -q "<project_license>LicenseRef-Proprietary</project_license>" "$(METAINFO)"
	grep -q "<summary>Survive a haunted security shift</summary>" "$(METAINFO)"
	grep -q "<launchable type=\"desktop-id\">$(BIN).desktop</launchable>" "$(METAINFO)"
	grep -q "<release version=\"$(VERSION)\"" "$(METAINFO)"
	grep -q "FILEVERSION $(VERSION_COMMA)" "$(BUILD_DIR)/nightshift.rc"
	grep -q 'VALUE "FileVersion", "$(VERSION)"' "$(BUILD_DIR)/nightshift.rc"
	grep -q 'VALUE "OriginalFilename", "$(BIN).exe"' "$(BUILD_DIR)/nightshift.rc"
	grep -q '1 ICON "$(BUILD_DIR)/nightshift.ico"' "$(BUILD_DIR)/nightshift.rc"
	grep -q '1 24 "$(BUILD_DIR)/nightshift.manifest"' "$(BUILD_DIR)/nightshift.rc"
	test -s "$(BUILD_DIR)/nightshift.ico"
	grep -q 'requestedExecutionLevel level="asInvoker"' "$(BUILD_DIR)/nightshift.manifest"
	grep -q '<dpiAwareness xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">PerMonitorV2, PerMonitor</dpiAwareness>' "$(BUILD_DIR)/nightshift.manifest"
	grep -q '<longPathAware xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">true</longPathAware>' "$(BUILD_DIR)/nightshift.manifest"
	@if grep -q '@VERSION@\|@MANIFEST@\|@ICON@' "$(BUILD_DIR)/nightshift.rc" "$(BUILD_DIR)/nightshift.manifest"; then printf '%s\n' 'unexpanded placeholder in generated Win32 resources'; exit 1; fi

install: $(EXE) $(METAINFO)
	mkdir -p "$(DESTDIR)$(BINDIR)" "$(DESTDIR)$(DOCDIR)"
	cp "$(EXE)" "$(DESTDIR)$(BINDIR)/$(EXE)"
	cp README.md "$(DESTDIR)$(DOCDIR)/README.md"
	cp HOW_TO_PLAY.md "$(DESTDIR)$(DOCDIR)/HOW_TO_PLAY.md"
	cp LICENSE "$(DESTDIR)$(DOCDIR)/LICENSE"
ifeq ($(TARGET),unix)
	mkdir -p "$(DESTDIR)$(APPDIR)" "$(DESTDIR)$(ICONDIR)" "$(DESTDIR)$(METAINFODIR)"
	cp packaging/nightshift.desktop "$(DESTDIR)$(APPDIR)/nightshift.desktop"
	cp packaging/nightshift.svg "$(DESTDIR)$(ICONDIR)/nightshift.svg"
	cp "$(METAINFO)" "$(DESTDIR)$(METAINFODIR)/nightshift.metainfo.xml"
endif
ifeq ($(TARGET),win32)
	cp packaging/WINDOWS.txt "$(DESTDIR)$(DOCDIR)/WINDOWS.txt"
	if [ -n "$(PACKAGE_SDL_DLL)" ] && [ -f "$(PACKAGE_SDL_DLL)" ]; then cp "$(PACKAGE_SDL_DLL)" "$(DESTDIR)$(BINDIR)/SDL2.dll"; else printf '%s\n' 'warning: SDL2.dll was not found for install; set SDL_PREFIX or SDL_DLL to install it beside nightshift.exe.'; fi
endif

install-check: $(EXE)
	tmp=$$(mktemp -d /tmp/nightshift-install.XXXXXX); \
	trap 'rm -rf "$$tmp"' EXIT; \
	$(MAKE) install DESTDIR="$$tmp/root" PREFIX=/usr; \
	test -x "$$tmp/root/usr/bin/$(EXE)"; \
	test -f "$$tmp/root/usr/share/doc/$(BIN)/README.md"; \
	test -f "$$tmp/root/usr/share/doc/$(BIN)/HOW_TO_PLAY.md"; \
	test -f "$$tmp/root/usr/share/doc/$(BIN)/LICENSE"; \
	if [ "$(TARGET)" = "win32" ]; then \
		test -f "$$tmp/root/usr/share/doc/$(BIN)/WINDOWS.txt"; \
	fi; \
	if [ "$(TARGET)" = "win32" ] && [ -n "$(PACKAGE_SDL_DLL)" ] && [ -f "$(PACKAGE_SDL_DLL)" ]; then \
		test -f "$$tmp/root/usr/bin/SDL2.dll"; \
	fi; \
	if [ "$(TARGET)" = "unix" ]; then \
		test -f "$$tmp/root/usr/share/applications/nightshift.desktop"; \
		test -f "$$tmp/root/usr/share/icons/hicolor/scalable/apps/nightshift.svg"; \
		test -f "$$tmp/root/usr/share/metainfo/nightshift.metainfo.xml"; \
	fi

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/$(EXE)"
ifeq ($(TARGET),win32)
	rm -f "$(DESTDIR)$(BINDIR)/SDL2.dll"
	rm -f "$(DESTDIR)$(DOCDIR)/WINDOWS.txt"
endif
	rm -f "$(DESTDIR)$(DOCDIR)/README.md"
	rm -f "$(DESTDIR)$(DOCDIR)/HOW_TO_PLAY.md"
	rm -f "$(DESTDIR)$(DOCDIR)/LICENSE"
	rmdir "$(DESTDIR)$(DOCDIR)" 2>/dev/null || true
ifeq ($(TARGET),unix)
	rm -f "$(DESTDIR)$(APPDIR)/nightshift.desktop"
	rm -f "$(DESTDIR)$(ICONDIR)/nightshift.svg"
	rm -f "$(DESTDIR)$(METAINFODIR)/nightshift.metainfo.xml"
endif

uninstall-check: $(EXE)
	tmp=$$(mktemp -d /tmp/nightshift-uninstall.XXXXXX); \
	trap 'rm -rf "$$tmp"' EXIT; \
	$(MAKE) install DESTDIR="$$tmp/root" PREFIX=/usr; \
	$(MAKE) uninstall DESTDIR="$$tmp/root" PREFIX=/usr; \
	test ! -e "$$tmp/root/usr/bin/$(EXE)"; \
	if [ "$(TARGET)" = "win32" ]; then \
		test ! -e "$$tmp/root/usr/bin/SDL2.dll"; \
	fi; \
	test ! -e "$$tmp/root/usr/share/doc/$(BIN)/README.md"; \
	test ! -e "$$tmp/root/usr/share/doc/$(BIN)/HOW_TO_PLAY.md"; \
	test ! -e "$$tmp/root/usr/share/doc/$(BIN)/LICENSE"; \
	if [ "$(TARGET)" = "win32" ]; then \
		test ! -e "$$tmp/root/usr/share/doc/$(BIN)/WINDOWS.txt"; \
	fi; \
	if [ "$(TARGET)" = "unix" ]; then \
		test ! -e "$$tmp/root/usr/share/applications/nightshift.desktop"; \
		test ! -e "$$tmp/root/usr/share/icons/hicolor/scalable/apps/nightshift.svg"; \
		test ! -e "$$tmp/root/usr/share/metainfo/nightshift.metainfo.xml"; \
	fi

package: $(EXE) $(METAINFO) $(PACKAGE_DEPS)
	rm -rf "$(DIST_DIR)/$(PACKAGE_NAME)" "$(DIST_DIR)/$(PACKAGE_NAME).tar.gz" "$(DIST_DIR)/$(PACKAGE_NAME).tar.gz.sha256" "$(DIST_DIR)/$(PACKAGE_NAME).zip" "$(DIST_DIR)/$(PACKAGE_NAME).zip.sha256"
	mkdir -p "$(DIST_DIR)/$(PACKAGE_NAME)"
	cp "$(EXE)" README.md HOW_TO_PLAY.md LICENSE "$(DIST_DIR)/$(PACKAGE_NAME)/"
	printf '%s\n' \
		"Name: Night Shift" \
		"Version: $(VERSION)" \
		"Target: $(TARGET)" \
		"Executable: $(EXE)" \
		"Runtime: SDL2" \
		"Docs: README.md HOW_TO_PLAY.md LICENSE" \
		"Package: $(PACKAGE_NAME)" > "$(DIST_DIR)/$(PACKAGE_NAME)/PACKAGE.txt"
ifeq ($(TARGET),unix)
	mkdir -p "$(DIST_DIR)/$(PACKAGE_NAME)/share/applications" "$(DIST_DIR)/$(PACKAGE_NAME)/share/icons/hicolor/scalable/apps" "$(DIST_DIR)/$(PACKAGE_NAME)/share/metainfo"
	cp packaging/nightshift.desktop "$(DIST_DIR)/$(PACKAGE_NAME)/share/applications/"
	cp packaging/nightshift.svg "$(DIST_DIR)/$(PACKAGE_NAME)/share/icons/hicolor/scalable/apps/"
	cp "$(METAINFO)" "$(DIST_DIR)/$(PACKAGE_NAME)/share/metainfo/nightshift.metainfo.xml"
endif
ifeq ($(TARGET),win32)
	cp packaging/WINDOWS.txt "$(DIST_DIR)/$(PACKAGE_NAME)/WINDOWS.txt"
	if [ -n "$(PACKAGE_SDL_DLL)" ] && [ -f "$(PACKAGE_SDL_DLL)" ]; then cp "$(PACKAGE_SDL_DLL)" "$(DIST_DIR)/$(PACKAGE_NAME)/"; else printf '%s\n' 'error: SDL2.dll was not found for Win32 package; set SDL_PREFIX or SDL_DLL to include it.'; exit 2; fi
endif
ifeq ($(TARGET),unix)
	tar -czf "$(DIST_DIR)/$(PACKAGE_NAME).tar.gz" -C "$(DIST_DIR)" "$(PACKAGE_NAME)"
	cd "$(DIST_DIR)" && sha256sum "$(PACKAGE_NAME).tar.gz" > "$(PACKAGE_NAME).tar.gz.sha256"
endif
ifeq ($(TARGET),win32)
	$(ZIP_TOOL) "$(DIST_DIR)/$(PACKAGE_NAME).zip" "$(DIST_DIR)/$(PACKAGE_NAME)" "$(EXE)" "SDL2.dll" "README.md" "HOW_TO_PLAY.md" "WINDOWS.txt" "LICENSE" "PACKAGE.txt"
	cd "$(DIST_DIR)" && sha256sum "$(PACKAGE_NAME).zip" > "$(PACKAGE_NAME).zip.sha256"
endif

ifeq ($(TARGET),win32)
package-installer: package $(WIN32_INSTALLER)
	cd "$(DIST_DIR)" && sha256sum "$(WIN32_INSTALLER_NAME)" > "$(WIN32_INSTALLER_NAME).sha256"
else
package-installer:
	@printf '%s\n' "package-installer is only available with TARGET=win32"
	@exit 2
endif

ifeq ($(TARGET),win32)
package-installer-check: package-installer
	cd "$(DIST_DIR)" && sha256sum -c "$(WIN32_INSTALLER_NAME).sha256"
	test -s "$(WIN32_INSTALLER)"
	grep -q '101 RCDATA "dist/$(PACKAGE_NAME)/nightshift.exe"' "$(BUILD_DIR)/nightshift-installer.rc"
	grep -q '102 RCDATA "dist/$(PACKAGE_NAME)/SDL2.dll"' "$(BUILD_DIR)/nightshift-installer.rc"
	grep -q '103 RCDATA "dist/$(PACKAGE_NAME)/README.md"' "$(BUILD_DIR)/nightshift-installer.rc"
	grep -q '104 RCDATA "dist/$(PACKAGE_NAME)/HOW_TO_PLAY.md"' "$(BUILD_DIR)/nightshift-installer.rc"
	grep -q '105 RCDATA "dist/$(PACKAGE_NAME)/WINDOWS.txt"' "$(BUILD_DIR)/nightshift-installer.rc"
	grep -q '106 RCDATA "dist/$(PACKAGE_NAME)/LICENSE"' "$(BUILD_DIR)/nightshift-installer.rc"
	grep -q '107 RCDATA "dist/$(PACKAGE_NAME)/PACKAGE.txt"' "$(BUILD_DIR)/nightshift-installer.rc"
	strings "$(WIN32_INSTALLER)" | grep -q "Night Shift Setup"
else
package-installer-check:
	@printf '%s\n' "package-installer-check is only available with TARGET=win32"
	@exit 2
endif

package-check: package
ifeq ($(TARGET),unix)
	cd "$(DIST_DIR)" && sha256sum -c "$(PACKAGE_NAME).tar.gz.sha256"
	tar -tzf "$(DIST_DIR)/$(PACKAGE_NAME).tar.gz" > /tmp/nightshift-package.lst
	grep -q "^$(PACKAGE_NAME)/$(EXE)$$" /tmp/nightshift-package.lst
	grep -q "^$(PACKAGE_NAME)/README.md$$" /tmp/nightshift-package.lst
	grep -q "^$(PACKAGE_NAME)/HOW_TO_PLAY.md$$" /tmp/nightshift-package.lst
	grep -q "^$(PACKAGE_NAME)/LICENSE$$" /tmp/nightshift-package.lst
	grep -q "^$(PACKAGE_NAME)/PACKAGE.txt$$" /tmp/nightshift-package.lst
	@if grep -Eq '(^|/)(build|dist)/|\.tmp$$|\.save$$|\.cfg$$|\.bmp$$|\.o$$|\.res$$|\.res\.o$$' /tmp/nightshift-package.lst; then \
		printf '%s\n' 'package contains generated, local state, or temporary files'; \
		exit 1; \
	fi
	tmp=$$(mktemp -d /tmp/nightshift-package.XXXXXX); \
	trap 'rm -rf "$$tmp"' EXIT; \
	tar -xzf "$(DIST_DIR)/$(PACKAGE_NAME).tar.gz" -C "$$tmp"; \
	test -x "$$tmp/$(PACKAGE_NAME)/$(EXE)"; \
	test -s "$$tmp/$(PACKAGE_NAME)/README.md"; \
	test -s "$$tmp/$(PACKAGE_NAME)/HOW_TO_PLAY.md"; \
	test -s "$$tmp/$(PACKAGE_NAME)/LICENSE"; \
	grep -q "^Version: $(VERSION)$$" "$$tmp/$(PACKAGE_NAME)/PACKAGE.txt"; \
	grep -q "^Target: $(TARGET)$$" "$$tmp/$(PACKAGE_NAME)/PACKAGE.txt"; \
	grep -q "^Executable: $(EXE)$$" "$$tmp/$(PACKAGE_NAME)/PACKAGE.txt"; \
	grep -q "^Runtime: SDL2$$" "$$tmp/$(PACKAGE_NAME)/PACKAGE.txt"
	grep -q "^$(PACKAGE_NAME)/share/applications/nightshift.desktop$$" /tmp/nightshift-package.lst
	grep -q "^$(PACKAGE_NAME)/share/icons/hicolor/scalable/apps/nightshift.svg$$" /tmp/nightshift-package.lst
	grep -q "^$(PACKAGE_NAME)/share/metainfo/nightshift.metainfo.xml$$" /tmp/nightshift-package.lst
	tmp=$$(mktemp -d /tmp/nightshift-package.XXXXXX); \
	trap 'rm -rf "$$tmp"' EXIT; \
	tar -xzf "$(DIST_DIR)/$(PACKAGE_NAME).tar.gz" -C "$$tmp"; \
	test -s "$$tmp/$(PACKAGE_NAME)/share/applications/nightshift.desktop"; \
	test -s "$$tmp/$(PACKAGE_NAME)/share/icons/hicolor/scalable/apps/nightshift.svg"; \
	test -s "$$tmp/$(PACKAGE_NAME)/share/metainfo/nightshift.metainfo.xml"; \
	grep -q "<release version=\"$(VERSION)\"" "$$tmp/$(PACKAGE_NAME)/share/metainfo/nightshift.metainfo.xml"
endif
ifeq ($(TARGET),win32)
	cd "$(DIST_DIR)" && sha256sum -c "$(PACKAGE_NAME).zip.sha256"
	unzip -t "$(DIST_DIR)/$(PACKAGE_NAME).zip" >/dev/null
	unzip -Z1 "$(DIST_DIR)/$(PACKAGE_NAME).zip" > /tmp/nightshift-package-zip.lst
	grep -q "^$(EXE)$$" /tmp/nightshift-package-zip.lst
	grep -q "^SDL2.dll$$" /tmp/nightshift-package-zip.lst
	grep -q "^README.md$$" /tmp/nightshift-package-zip.lst
	grep -q "^HOW_TO_PLAY.md$$" /tmp/nightshift-package-zip.lst
	grep -q "^WINDOWS.txt$$" /tmp/nightshift-package-zip.lst
	grep -q "^LICENSE$$" /tmp/nightshift-package-zip.lst
	grep -q "^PACKAGE.txt$$" /tmp/nightshift-package-zip.lst
	@if grep -Eq '(^|/)(build|dist)/|\.tmp$$|\.save$$|\.cfg$$|\.bmp$$|\.o$$|\.res$$|\.res\.o$$' /tmp/nightshift-package-zip.lst; then \
		printf '%s\n' 'zip package contains generated, local state, or temporary files'; \
		exit 1; \
	fi
	tmp=$$(mktemp -d /tmp/nightshift-package-zip.XXXXXX); \
	trap 'rm -rf "$$tmp"' EXIT; \
	unzip -q "$(DIST_DIR)/$(PACKAGE_NAME).zip" -d "$$tmp"; \
	test -x "$$tmp/$(EXE)"; \
	test -s "$$tmp/SDL2.dll"; \
	test -s "$$tmp/README.md"; \
	test -s "$$tmp/HOW_TO_PLAY.md"; \
	test -s "$$tmp/LICENSE"; \
	test -s "$$tmp/WINDOWS.txt"; \
	grep -q "^Version: $(VERSION)$$" "$$tmp/PACKAGE.txt"; \
	grep -q "^Target: $(TARGET)$$" "$$tmp/PACKAGE.txt"; \
	grep -q "^Executable: $(EXE)$$" "$$tmp/PACKAGE.txt"; \
	grep -q "^Runtime: SDL2$$" "$$tmp/PACKAGE.txt"; \
	grep -q "SDL2.dll" "$$tmp/WINDOWS.txt"; \
	grep -q "nightshift.exe" "$$tmp/WINDOWS.txt"
endif

package-run-check: package
ifeq ($(TARGET),unix)
	set -e; tmp=$$(mktemp -d /tmp/nightshift-package-run.XXXXXX); \
	trap 'rm -rf "$$tmp"' EXIT; \
	tar -xzf "$(DIST_DIR)/$(PACKAGE_NAME).tar.gz" -C "$$tmp"; \
	"$$tmp/$(PACKAGE_NAME)/$(EXE)" --version | grep -q "^Night Shift $(VERSION)$$"; \
	"$$tmp/$(PACKAGE_NAME)/$(EXE)" --help | grep -q "^Usage:"
else ifeq ($(TARGET),win32)
	case "$$(uname -s)" in \
		MINGW*|MSYS*|CYGWIN*) \
			set -e; tmp=$$(mktemp -d /tmp/nightshift-package-run.XXXXXX); \
			trap 'rm -rf "$$tmp"' EXIT; \
			unzip -q "$(DIST_DIR)/$(PACKAGE_NAME).zip" -d "$$tmp/zip"; \
			"$$tmp/zip/$(EXE)" --version | grep -q "^Night Shift $(VERSION)$$"; \
			"$$tmp/zip/$(EXE)" --help | grep -q "^Usage:"; \
			;; \
		*) \
			printf '%s\n' "package_run_check=skip target=$(TARGET) host=$$(uname -s)"; \
			;; \
	esac
else
	printf '%s\n' "package_run_check=skip target=$(TARGET)"
endif

check-sdl:
ifeq ($(TARGET),win32)
	@if [ -z "$(strip $(SDL_PREFIX)$(WIN32_PKG_CONFIG)$(SDL_CFLAGS))" ]; then \
		printf '%s\n' 'Win32 SDL2 headers/libs were not found.'; \
		printf '%s\n' 'Pass SDL_PREFIX=/path/to/mingw-sdl2, WIN32_PKG_CONFIG=x86_64-w64-mingw32-pkg-config, or explicit SDL_CFLAGS/SDL_LIBS plus SDL_DLL for packaging.'; \
		exit 2; \
	fi
	@if ! printf '%s\n' '#include <SDL2/SDL.h>' 'int main(int argc, char **argv){(void)argc;(void)argv;return 0;}' | $(CC) $(SDL_CFLAGS) -std=$(CSTD) -x c -c -o /dev/null - >/dev/null 2>&1 && \
	    ! printf '%s\n' '#include <SDL.h>' 'int main(int argc, char **argv){(void)argc;(void)argv;return 0;}' | $(CC) $(SDL_CFLAGS) -std=$(CSTD) -x c -c -o /dev/null - >/dev/null 2>&1; then \
		printf '%s\n' 'Win32 SDL2 headers/libs were not found.'; \
		printf '%s\n' 'Pass SDL_PREFIX=/path/to/mingw-sdl2, WIN32_PKG_CONFIG=x86_64-w64-mingw32-pkg-config, or explicit SDL_CFLAGS/SDL_LIBS plus SDL_DLL for packaging.'; \
		exit 2; \
	fi
else
	@if ! printf '%s\n' '#include <SDL2/SDL.h>' 'int main(int argc, char **argv){(void)argc;(void)argv;return 0;}' | $(CC) $(SDL_CFLAGS) -std=$(CSTD) -x c -c -o /dev/null - >/dev/null 2>&1 && \
	    ! printf '%s\n' '#include <SDL.h>' 'int main(int argc, char **argv){(void)argc;(void)argv;return 0;}' | $(CC) $(SDL_CFLAGS) -std=$(CSTD) -x c -c -o /dev/null - >/dev/null 2>&1; then \
		printf '%s\n' 'SDL2 headers were not found.'; \
		printf '%s\n' 'Install SDL2 development files, make sure pkg-config can find sdl2, or pass SDL_CFLAGS explicitly.'; \
		exit 2; \
	fi
endif

clean:
	rm -rf build dist $(BIN) $(BIN).exe

-include $(DEP)
