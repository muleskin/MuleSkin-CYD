# MuleSkin-CYD PC emulator.
#
# Builds MuleSkin-CYD's real UI/rendering sources natively against
# the shims in this directory (Arduino.h, TFT_eSPI.h, Preferences.h), so
# layout work can be previewed without flashing hardware.
#
# This lives outside the firmware repo on purpose: it compiles that
# repo's sources but adds nothing to the firmware build.
#
#   make -j8        # build ./muleskinsim and ./muleskinsim-live
#   make shots      # render one PNG per screen into out/
#
# Objects go under build/ with -MMD dependency files beside them, so an
# edit recompiles only the translation units that actually include what
# changed, and `make -j` spreads the rest across every core. The old
# single-shot g++ command recompiled everything on every edit, twice
# (once per binary): 20-30 seconds for a one-line change on a 16-core
# machine, because one compiler process cannot use a second core.
#
# Every object also depends on this Makefile, so a change to CXXFLAGS
# rebuilds everything rather than leaving a binary made of mixed flags
# -- editing a -D here and getting a stale build is exactly the kind of
# thing that sends you chasing a bug that isn't there.
#
# Deliberately excluded from the source list: main.cpp (ESP32 setup/loop
# and hardware init), detection.cpp and sd_log.cpp (WiFi/BLE/SD -- see
# detection_sim.cpp for the stand-in), and the touch calibration code.

SIM_DIR  := .
# Path to the firmware sources. The emulator lives INSIDE the firmware
# checkout now, so this is just the parent directory -- it used to be a
# sibling clone, which is precisely why CI could never build it. Still a
# variable rather than a hard-coded "..", so a second checkout can be
# pointed at:
#   make MuleSkin=/path/to/MuleSkin-CYD
# Nothing is vendored, so the emulator always renders whatever the working
# tree currently has.
MuleSkin ?= ..
SRC      := $(MuleSkin)/src
INC      := $(MuleSkin)/include

# Say so up front if the firmware tree is missing, rather than letting the
# first compile fail on a header it cannot find. `make clean` is exempt:
# it never needed the tree and still should not.
ifeq ($(filter clean,$(MAKECMDGOALS)),)
ifeq ($(wildcard $(INC)),)
$(error MuleSkin-CYD not found at $(MuleSkin) -- set MuleSkin=/path/to/checkout)
endif
endif

CXX      ?= g++
# The sim dir comes first on the include path on purpose: that's what
# makes <TFT_eSPI.h>, <Arduino.h> and <Preferences.h> resolve to the
# shims here rather than to an ESP32 core that isn't installed.
EXTRA_CXXFLAGS ?=
# The version the firmware would report from this tree (extra_script.py does
# the same for a real build), so the splash and banners in renders say it.
FW_VERSION := $(shell git describe --tags --always 2>/dev/null || echo sim)
CXXFLAGS := -std=c++17 -O1 -g -Wall -Wno-unused-parameter $(EXTRA_CXXFLAGS) \
            -I$(SIM_DIR) -I$(INC) -DMULESKIN_MESH=1 -DFIRMWARE_VERSION=\"$(FW_VERSION)\"
# MinGW (Windows) compilers: the POSIX bits its runtime lacks (see win_compat.h).
ifneq ($(findstring mingw,$(shell $(CXX) -dumpmachine)),)
CXXFLAGS += -include $(SIM_DIR)/win_compat.h
endif

UI_SRCS  := $(SRC)/theme.cpp \
            $(SRC)/muleskin.cpp \
            $(SRC)/muleskinmesh.cpp \
            $(SRC)/mesh.cpp \
            $(SRC)/meshmsg.cpp \
            $(SRC)/emote_script.cpp \
            $(SRC)/crowd_bench.cpp \
            $(SRC)/meshwords.cpp \
            $(SRC)/meshtalk.cpp \
            $(SRC)/meshtutor.cpp \
            $(SIM_DIR)/meshcrypto_sim.cpp \
            $(SIM_DIR)/meshsim.cpp \
            $(SRC)/ui_phone.cpp \
            $(SRC)/qwerty.cpp \
            $(SRC)/ui_meshmenu.cpp \
            $(SRC)/ui_meshwarn.cpp \
            $(SRC)/ui_beaconwarn.cpp \
            $(SRC)/security.cpp \
            $(SRC)/ui_security.cpp \
            $(SRC)/ui_squad.cpp \
            $(SRC)/ui_meshphrase.cpp \
            $(SRC)/ui_meshcompose.cpp \
            $(SRC)/settings.cpp \
            $(SRC)/signatures.cpp \
            $(SRC)/remote_id.cpp \
            $(SRC)/clock.cpp \
            $(SRC)/nvs_backup.cpp \
            $(SRC)/radar_blip.cpp \
            $(SRC)/version_cmp.cpp \
            $(SRC)/blackbox.cpp \
            $(SRC)/log_index.cpp \
            $(SRC)/bingo.cpp \
            $(SRC)/ui_bingo.cpp \
            $(SRC)/dex.cpp \
            $(SRC)/regulars.cpp \
            $(SRC)/notices.cpp \
            $(SRC)/ui_dex.cpp \
            $(SRC)/ui_meeting.cpp \
            $(SRC)/ui_timesync.cpp \
            $(SRC)/ui_today.cpp \
            $(SRC)/detection_info.cpp \
            $(SRC)/device_info.cpp \
            $(SRC)/idle_events.cpp \
            $(SRC)/ui_clear.cpp \
            $(SRC)/ui_power.cpp \
            $(SRC)/ui_light.cpp \
            $(SRC)/status_light.cpp \
            $(SRC)/pet.cpp \
            $(SRC)/ui_log.cpp \
            $(SRC)/ui_alert.cpp \
            $(SRC)/ui_settings.cpp \
            $(SRC)/ui_boot.cpp \
            $(SRC)/ui_colorcheck.cpp \
            $(SRC)/ui_diagnostics.cpp \
            $(SRC)/ui_zone.cpp \
            $(SRC)/ui_diary.cpp \
            $(SRC)/ui_hunt.cpp \
            $(SRC)/ui_outfit.cpp \
            $(SRC)/ui_outfit_unlock.cpp \
            $(SRC)/ignore_list.cpp \
            $(SRC)/ui_ignorelist.cpp \
            $(SRC)/ui_detfilter.cpp \
            $(SRC)/ui_watchalert.cpp \
            $(SRC)/ui_update.cpp \
            $(SRC)/ui_nudge.cpp \
            $(SRC)/ui_squadupdate.cpp \
            $(SRC)/ui_invite.cpp \
            $(SRC)/ui_wifipass.cpp \
            $(SRC)/ui_sysprops.cpp \
            $(SRC)/ui_wifinets.cpp \
            $(SRC)/frame_push.cpp \
            $(SRC)/frame_prof.cpp \
            $(SRC)/fast_sprite.cpp \
            $(SIM_DIR)/ota_sim.cpp

# touch_cal.cpp for the touchcal screen, which runs the real calibration flow
# with a scripted finger. Only here: the live and web builds already compile it.
SIM_SRCS := $(SIM_DIR)/main_sim.cpp $(SIM_DIR)/detection_sim.cpp $(SRC)/touch_cal.cpp

# The interactive target additionally compiles the firmware's real
# main.cpp -- its actual setup()/loop(), state machine, gesture handling
# and touch mapping -- plus the two touch modules main.cpp includes.
# detection.cpp/sd_log.cpp stay replaced by detection_sim.cpp (radios).
#
# -include of the board's user setup supplies the TFT_/pin macros
# main.cpp expects; that header has no includes of its own, so it's safe
# to pull in here. No board macro is defined (no CYD35, no AWOK), which
# selects the same plain XPT2046 path the real cyd board takes.
LIVE_SRCS := $(SRC)/main.cpp \
             $(SRC)/cap_touch.cpp \
             $(SRC)/touch_cal.cpp \
             $(SIM_DIR)/main_live.cpp \
             $(SIM_DIR)/detection_sim.cpp
# -include Arduino.h mirrors what the Arduino build system does for
# every translation unit: cap_touch.cpp and friends call pinMode/delay
# without including it themselves and rely on that being implicit.
LIVE_FLAGS := -include $(SIM_DIR)/Arduino.h -include $(INC)/cyd_user_setup.h

BIN      := muleskinsim
LIVE_BIN := muleskinsim-live

# Objects live in build/, split by flag set: ui/ for the shared UI
# sources (same flags in both binaries, compiled once), sim/ for the
# one-shot renderer's own files, live/ for the ones that need the
# board's user setup -include'd. detection_sim.cpp is in two of those
# with different flags, which is why the split is by directory rather
# than by name.
OBJ_DIR  := build
UI_OBJS   := $(patsubst %.cpp,$(OBJ_DIR)/ui/%.o,$(notdir $(UI_SRCS)))
SIM_OBJS  := $(patsubst %.cpp,$(OBJ_DIR)/sim/%.o,$(notdir $(SIM_SRCS)))
LIVE_OBJS := $(patsubst %.cpp,$(OBJ_DIR)/live/%.o,$(notdir $(LIVE_SRCS)))
DEPS      := $(UI_OBJS:.o=.d) $(SIM_OBJS:.o=.d) $(LIVE_OBJS:.o=.d)

# Where a .cpp can come from. make looks in this directory first (the
# cwd always wins over vpath), then the firmware tree -- so a file here
# that shared a name with one in src/ would silently shadow it. None do
# today; the guard keeps it that way.
vpath %.cpp $(SRC) $(SIM_DIR)
CLASH := $(filter $(notdir $(wildcard $(SIM_DIR)/*.cpp)),$(notdir $(wildcard $(SRC)/*.cpp)))
ifneq ($(CLASH),)
$(error same basename in $(SRC) and $(SIM_DIR), vpath would pick the wrong one: $(CLASH))
endif

all: $(BIN) $(LIVE_BIN)
live: $(LIVE_BIN)

$(OBJ_DIR)/ui/%.o: %.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c -o $@ $<

$(OBJ_DIR)/sim/%.o: %.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c -o $@ $<

$(OBJ_DIR)/live/%.o: %.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(LIVE_FLAGS) -MMD -MP -c -o $@ $<

$(BIN): $(UI_OBJS) $(SIM_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ -lm

$(LIVE_BIN): $(UI_OBJS) $(LIVE_OBJS)
	$(CXX) $(CXXFLAGS) $(LIVE_FLAGS) -o $@ $^ -lm

-include $(DEPS)

shots: $(BIN)
	@mkdir -p out
	./$(BIN) clear    out/clear.png
	./$(BIN) log      out/log.png
	./$(BIN) alert    out/alert.png
	./$(BIN) settings out/settings.png

clean:
	rm -f $(BIN) $(LIVE_BIN)
	rm -rf $(OBJ_DIR)
	# Leftovers from the old single-shot build, which Apple clang gave
	# debug bundles; the object build never makes them.
	rm -rf $(BIN).dSYM $(LIVE_BIN).dSYM
	rm -rf out .nvs

.PHONY: all live shots clean
