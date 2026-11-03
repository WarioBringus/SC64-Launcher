PROJECT_NAME = N64FlashcartMenu

.DEFAULT_GOAL := all

SOURCE_DIR = src
ASSETS_DIR = assets
FILESYSTEM_DIR = filesystem
BUILD_DIR = build
OUTPUT_DIR = output

# uncomment the following line to support preview API's in libDragon.
# Launcher: on for audio_get_queued_buffers (a deeper audio queue while a game loads).
LIBDRAGON_PREVIEW = 2

MENU_VERSION ?= "Preview release"
BUILD_TIMESTAMP = "$(shell TZ='UTC' date "+%Y-%m-%d %H:%M:%S %:z")"
# Launcher build: commits since upstream V0.3.6 and the commit (* = built with uncommitted changes).
LAUNCHER_BUILD = "build $(shell git rev-list --count V0.3.6..HEAD 2>/dev/null || echo '?') ($(shell git rev-parse --short=8 HEAD 2>/dev/null)$(shell git diff --quiet HEAD -- src 2>/dev/null || echo '*'))"

include $(N64_INST)/include/n64.mk

N64_ROM_SAVETYPE = none
N64_ROM_RTC = 1
N64_ROM_REGIONFREE = 1
N64_ROM_REGION = E
N64_ROM_EXPANSIONPAK = recommended

N64_CFLAGS += -iquote $(SOURCE_DIR) -iquote $(ASSETS_DIR) -I $(SOURCE_DIR)/libs -isystem $(SOURCE_DIR)/libs/miniz -flto=auto $(FLAGS)

SRCS = \
	main.c \
	boot/boot.c \
	boot/cheats.c \
	boot/cic.c \
	boot/reboot.S \
	flashcart/64drive/64drive_ll.c \
	flashcart/64drive/64drive.c \
	flashcart/flashcart_utils.c \
	flashcart/ed64/ed64_proseries.c \
	flashcart/ed64/ed64_vseries.c \
	flashcart/ed64/ed64_vseries_ll.c \
	flashcart/ed64/ed64_xseries.c \
	flashcart/ed64/ed64_xseries_ll.c \
	flashcart/ed64/ed64_pseudo_state.c \
	flashcart/flashcart.c \
	flashcart/sc64/sc64_ll.c \
	flashcart/sc64/sc64.c \
	libs/libspng/spng/spng.c \
	libs/miniz/miniz_tdef.c \
	libs/miniz/miniz_tinfl.c \
	libs/miniz/miniz_zip.c \
	libs/miniz/miniz.c \
	menu/ini_parser.c \
	menu/actions.c \
	menu/audio_player.c \
	menu/bookkeeping.c \
	menu/cart_load.c \
	menu/datel_codes.c \
	menu/disk_info.c \
	menu/fonts.c \
	menu/hdmi.c \
	menu/library.c \
	menu/id3_parser.c \
	menu/jpeg_decoder.c \
	menu/menu.c \
	menu/path.c \
	menu/png_decoder.c \
	menu/rom_info.c \
	menu/settings.c \
	menu/sound.c \
	menu/theme.c \
	menu/zip_entry_count.c \
	menu/ui_components/background.c \
	menu/ui_components/boxart.c \
	menu/ui_components/common.c \
	menu/ui_components/context_menu.c \
	menu/ui_components/covers.c \
	menu/ui_components/file_info.c \
	menu/ui_components/file_list.c \
	menu/ui_components/launcher.c \
	menu/ui_components/tabs.c \
	menu/usb_comm.c \
	menu/views/about.c \
	menu/views/attract.c \
	menu/views/browser.c \
	menu/views/credits.c \
	menu/views/datel_code_editor.c \
	menu/views/error.c \
	menu/views/extract_file.c \
	menu/views/fault.c \
	menu/views/file_info.c \
	menu/views/history_favorites.c \
	menu/views/home.c \
	menu/views/library_view.c \
	menu/views/image_viewer.c \
	menu/views/text_viewer.c \
	menu/views/load_disk.c \
	menu/views/load_emulator.c \
	menu/views/load_rom.c \
	menu/views/music_player.c \
	menu/views/startup.c \
	menu/views/system_info.c \
	menu/views/settings_editor.c \
	menu/views/rtc.c \
	menu/views/flashcart_info.c \
	menu/views/cpakfs_manager.c \
	menu/views/cpak_dump_info.c \
	menu/views/cpak_note_dump_info.c \
	utils/cpakfs_utils.c \
	utils/fs.c \
	utils/utf_converter.c \

FONTS = \
	Firple-Bold.ttf \
	Inter.ttf

SOUNDS_WAV = \
	cursorsound.wav \
	back.wav \
	bgm.wav \
	enter.wav \
	error.wav \
	settings.wav \
	tick.wav \
	swish.wav \
	pop.wav

SOUNDS_XM ?=

OBJS = $(addprefix $(BUILD_DIR)/, $(addsuffix .o,$(basename $(SRCS))))
MINIZ_OBJS = $(filter $(BUILD_DIR)/libs/miniz/%.o,$(OBJS))
SPNG_OBJS = $(filter $(BUILD_DIR)/libs/libspng/%.o,$(OBJS))
DEPS = $(OBJS:.o=.d)

FILESYSTEM = \
	$(addprefix $(FILESYSTEM_DIR)/menu/fonts/, $(notdir $(FONTS:%.ttf=%.font64))) \
	$(FILESYSTEM_DIR)/menu/fonts/InterTitle.font64 \
	$(FILESYSTEM_DIR)/menu/fonts/InterSmall.font64 \
	$(FILESYSTEM_DIR)/menu/fonts/InterWide.font64 \
	$(FILESYSTEM_DIR)/menu/fonts/InterTitleWide.font64 \
	$(FILESYSTEM_DIR)/menu/fonts/InterSmallWide.font64 \
	$(addprefix $(FILESYSTEM_DIR)/menu/sounds/, $(notdir $(SOUNDS_WAV:%.wav=%.wav64))) \
	$(addprefix $(FILESYSTEM_DIR)/menu/sounds/, $(notdir $(SOUNDS_XM:%.xm=%.xm64))) \
	$(addprefix $(FILESYSTEM_DIR)/menu/sprites/, $(notdir $(IMAGES:%.png=%.sprite)))

$(MINIZ_OBJS): N64_CFLAGS+=-Wno-unused-function -fcompare-debug-second
$(SPNG_OBJS): N64_CFLAGS+=-DSPNG_USE_MINIZ -fcompare-debug-second
$(FILESYSTEM_DIR)/menu/fonts/Firple-Bold.font64: MKFONT_FLAGS+=--compress 1 --outline 1 --size 15 --charset $(ASSETS_DIR)/fonts/charset.txt --ellipsis 2026,1
$(FILESYSTEM_DIR)/menu/sounds/%.wav64: AUDIOCONV_FLAGS=--wav-compress 1

$(@info $(shell mkdir -p ./$(FILESYSTEM_DIR) &> /dev/null))

# Inter (SIL OFL, see assets/fonts/Inter-OFL.txt) is the launcher's typeface: body text and larger titles.
$(FILESYSTEM_DIR)/menu/fonts/Inter.font64: MKFONT_FLAGS+=--compress 1 --size 15 --var-axis wght=600 --charset $(ASSETS_DIR)/fonts/charset.txt --ellipsis 2026,1

$(FILESYSTEM_DIR)/menu/fonts/InterTitle.font64: $(ASSETS_DIR)/fonts/Inter.ttf
	@echo " [FONT] $@"
	@mkdir -p $(dir $@) $(BUILD_DIR)/fonts
	@cp "$<" $(BUILD_DIR)/fonts/InterTitle.ttf
	@$(N64_MKFONT) --compress 1 --size 22 --var-axis wght=700 --charset $(ASSETS_DIR)/fonts/charset.txt --ellipsis 2026,1 -o $(dir $@) $(BUILD_DIR)/fonts/InterTitle.ttf

$(FILESYSTEM_DIR)/menu/fonts/InterSmall.font64: $(ASSETS_DIR)/fonts/Inter.ttf
	@echo " [FONT] $@"
	@mkdir -p $(dir $@) $(BUILD_DIR)/fonts
	@cp "$<" $(BUILD_DIR)/fonts/InterSmall.ttf
	@$(N64_MKFONT) --compress 1 --size 12 --var-axis wght=600 --charset $(ASSETS_DIR)/fonts/charset.txt --ellipsis 2026,1 -o $(dir $@) $(BUILD_DIR)/fonts/InterSmall.ttf

# 16:9 variants: glyphs squeezed for a display that is stretched to 16:9 (--display ...,16:9).
INTER_WIDE_FLAGS = --compress 1 --display 640x480,16:9 --charset $(ASSETS_DIR)/fonts/charset.txt --ellipsis 2026,1

$(FILESYSTEM_DIR)/menu/fonts/InterWide.font64: $(ASSETS_DIR)/fonts/Inter.ttf
	@echo " [FONT] $@"
	@mkdir -p $(dir $@) $(BUILD_DIR)/fonts
	@cp "$<" $(BUILD_DIR)/fonts/InterWide.ttf
	@$(N64_MKFONT) $(INTER_WIDE_FLAGS) --size 15 --var-axis wght=600 -o $(dir $@) $(BUILD_DIR)/fonts/InterWide.ttf

$(FILESYSTEM_DIR)/menu/fonts/InterTitleWide.font64: $(ASSETS_DIR)/fonts/Inter.ttf
	@echo " [FONT] $@"
	@mkdir -p $(dir $@) $(BUILD_DIR)/fonts
	@cp "$<" $(BUILD_DIR)/fonts/InterTitleWide.ttf
	@$(N64_MKFONT) $(INTER_WIDE_FLAGS) --size 22 --var-axis wght=700 -o $(dir $@) $(BUILD_DIR)/fonts/InterTitleWide.ttf

$(FILESYSTEM_DIR)/menu/fonts/InterSmallWide.font64: $(ASSETS_DIR)/fonts/Inter.ttf
	@echo " [FONT] $@"
	@mkdir -p $(dir $@) $(BUILD_DIR)/fonts
	@cp "$<" $(BUILD_DIR)/fonts/InterSmallWide.ttf
	@$(N64_MKFONT) $(INTER_WIDE_FLAGS) --size 12 --var-axis wght=600 -o $(dir $@) $(BUILD_DIR)/fonts/InterSmallWide.ttf

$(FILESYSTEM_DIR)/menu/fonts/%.font64: $(ASSETS_DIR)/fonts/%.ttf
	@echo " [FONT] $@"
	@mkdir -p $(dir $@)
	@$(N64_MKFONT) $(MKFONT_FLAGS) -o $(dir $@) "$<"

$(FILESYSTEM_DIR)/menu/sounds/%.wav64: $(ASSETS_DIR)/sounds/%.wav
	@echo " [AUDIO WAV] $@"
	@mkdir -p $(dir $@)
	@$(N64_AUDIOCONV) $(AUDIOCONV_FLAGS) -o $(dir $@) "$<"

$(FILESYSTEM_DIR)/menu/sounds/%.xm64: $(ASSETS_DIR)/sounds/%.xm
	@echo " [AUDIO XM] $@"
	@mkdir -p $(dir $@)
	@$(N64_AUDIOCONV) $(AUDIOCONV_FLAGS) -o $(dir $@) "$<"

$(FILESYSTEM_DIR)/menu/sprites/%.sprite: $(ASSETS_DIR)/images/%.png
	@echo " [SPRITE] $@"
	@mkdir -p $(dir $@)
	@$(N64_MKSPRITE) $(MKSPRITE_FLAGS) -o $(dir $@) "$<"

$(BUILD_DIR)/$(PROJECT_NAME).dfs: $(FILESYSTEM)

$(BUILD_DIR)/menu/views/credits.o: .FORCE
$(BUILD_DIR)/menu/views/credits.o: FLAGS+=-DMENU_VERSION=\"$(MENU_VERSION)\" -DBUILD_TIMESTAMP=\"$(BUILD_TIMESTAMP)\" -DLAUNCHER_BUILD=\"$(LAUNCHER_BUILD)\"

$(BUILD_DIR)/$(PROJECT_NAME).elf: $(OBJS)

disassembly: $(BUILD_DIR)/$(PROJECT_NAME).elf
	@$(N64_OBJDUMP) -S $< > $(BUILD_DIR)/$(PROJECT_NAME).lst
.PHONY: disassembly

$(PROJECT_NAME).z64: N64_ROM_TITLE=$(PROJECT_NAME)
$(PROJECT_NAME).z64: N64_ROM_METADATA=metadata/metadata.ini
$(PROJECT_NAME).z64: $(BUILD_DIR)/$(PROJECT_NAME).dfs

$(@info $(shell mkdir -p ./$(OUTPUT_DIR) &> /dev/null))

$(OUTPUT_DIR)/$(PROJECT_NAME).n64: $(PROJECT_NAME).z64
	@mv $< $@

64drive: $(OUTPUT_DIR)/$(PROJECT_NAME).n64
	@cp $< $(OUTPUT_DIR)/menu.bin
.PHONY: 64drive

ed64: $(OUTPUT_DIR)/$(PROJECT_NAME).n64
	@cp $< $(OUTPUT_DIR)/OS64.v64
.PHONY: ed64

ed64-clone: $(OUTPUT_DIR)/$(PROJECT_NAME).n64
	@cp $< $(OUTPUT_DIR)/OS64P.v64
.PHONY: ed64-clone

sc64: $(OUTPUT_DIR)/$(PROJECT_NAME).n64
	@cp $< $(OUTPUT_DIR)/sc64menu.n64
.PHONY: sc64

all: $(OUTPUT_DIR)/$(PROJECT_NAME).n64 64drive ed64 ed64-clone sc64
.PHONY: all

clean:
	@rm -f ./$(FILESYSTEM)
	@find ./$(FILESYSTEM_DIR)/menu/ -type d -empty -delete
	@rm -rf ./$(BUILD_DIR) ./$(OUTPUT_DIR)
.PHONY: clean

format:
	@find ./$(SOURCE_DIR) \
		-path \./$(SOURCE_DIR)/libs -prune \
		-o -iname *.c -print \
		-o -iname *.h -print \
		| xargs clang-format -i

run: $(OUTPUT_DIR)/$(PROJECT_NAME).n64
ifeq ($(OS),Windows_NT)
	./localdeploy.bat
else
	./remotedeploy.sh
endif
.PHONY: run

run-debug: $(OUTPUT_DIR)/$(PROJECT_NAME).n64
ifeq ($(OS),Windows_NT)
	./localdeploy.bat /d
else
	./remotedeploy.sh -d
endif
.PHONY: run-debug

run-debug-reboot: $(OUTPUT_DIR)/$(PROJECT_NAME).n64
ifeq ($(OS),Windows_NT)
	./localdeploy.bat /dr
else
	./remotedeploy.sh -dr
endif
.PHONY: run-debug-reboot

run-debug-upload: $(OUTPUT_DIR)/$(PROJECT_NAME).n64
ifeq ($(OS),Windows_NT)
	./localdeploy.bat /dur
else
	./remotedeploy.sh -dur
endif
.PHONY: run-debug-upload

# test:
#   TODO: run tests

.FORCE:

-include $(DEPS)
