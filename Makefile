# Compiler flags
DEBUG_FLAGS := -DDEBUG -DLOG_FILE=\"/tmp/fensterchef-log.txt\" -g -fsanitize=address -pg
C_FLAGS := -Iinclude $(shell pkg-config --cflags freetype2 fontconfig) -Wall -Wextra -Wpedantic -Werror -Wno-format-zero-length
RELEASE_FLAGS := -O3

# Libs
C_LIBS := $(shell pkg-config --libs xcb xcb-randr xcb-ewmh xcb-icccm xcb-keysyms xcb-event xcb-render xcb-renderutil freetype2 fontconfig)

# Input
SRC := src

# Output
RUN := fensterchef
BUILD := build
RELEASE := release

# Find all source files
SOURCES := $(shell find $(SRC) -name '*.c')
# Get all corresponding object paths
OBJECTS := $(patsubst $(SRC)/%.c,$(BUILD)/%.o,$(SOURCES))

# Get dependencies
DEPENDENCIES := $(patsubst %.o,%.d,$(OBJECTS))

.PHONY: default
default: build

# Include dependencies (.d files) generated by gcc
-include $(DEPENDENCIES)
# Build each object from corresponding source file
$(BUILD)/%.o: $(SRC)/%.c
	mkdir -p $(dir $@)
	gcc $(DEBUG_FLAGS) $(C_FLAGS) -c $< -o $@ -MMD

# Build the main executable from all object files
$(BUILD)/$(RUN): $(OBJECTS)
	mkdir -p $(dir $@)
	gcc $(DEBUG_FLAGS) $(C_FLAGS) $(OBJECTS) -o $@ $(C_LIBS)

# Functions
.PHONY: build test test-multi stop release clean

build: $(BUILD)/$(RUN)

DISPLAY_NO := 8
XEPHYR := Xephyr :$(DISPLAY_NO) +extension RANDR -br -ac -noreset -screen 800x600

test: build
	$(XEPHYR) &
	# wait for x server to start
	sleep 1
	DISPLAY=:$(DISPLAY_NO) gdb -ex run ./$(BUILD)/$(RUN)

test-multi: build
	$(XEPHYR) -screen 800x600+800+0 &
	# wait for x server to start
	sleep 1
	DISPLAY=:$(DISPLAY_NO) gdb -ex run ./$(BUILD)/$(RUN)

stop:
	pkill Xephyr

release:
	mkdir -p $(RELEASE)
	gcc $(RELEASE_FLAGS) $(C_FLAGS) $(SOURCES) -o $(RELEASE)/$(RUN) $(C_LIBS)

clean:
	rm -rf $(BUILD)
	rm -rf $(RELEASE)
