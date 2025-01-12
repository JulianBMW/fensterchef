# Compiler flags
DEBUG_FLAGS := -DDEBUG -g -fsanitize=address
C_FLAGS := -Wall -Wextra -Wpedantic -Werror
RELEASE_FLAGS := -O3

# Libs
C_LIBS := $(shell pkg-config --libs xcb xcb-icccm xcb-keysyms)

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
.PHONY: build test run stop release clean

build: $(BUILD)/$(RUN)

test: build
	./$(BUILD)/$(RUN)

run: build
	Xephyr -br -ac -noreset -screen 800x600 :8 &
	# wait for x server to start
	sleep 1
	DISPLAY=:8 ./$(BUILD)/$(RUN) &
	# wait for fensterchef to take control
	sleep 1
	# make a test window
	DISPLAY=:8 xterm &

stop:
	pkill Xephyr

release:
	mkdir -p $(RELEASE)
	gcc $(RELEASE_FLAGS) $(C_FLAGS) $(SOURCES) -o $(RELEASE)/$(RUN) $(C_LIBS)

clean:
	rm -rf $(BUILD)
	rm -rf $(RELEASE)
