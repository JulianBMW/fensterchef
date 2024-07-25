#Compiler flags
DEBUG_FLAGS := -g -fsanitize=address
C_FLAGS := -Wall -Wextra -Wpedantic -Werror
RELEASE_FLAGS := -O3

#Libs
XCB := -lxcb -lX11

#Link
LINK := src/*.c

#Output
OUT := out
RUN := $(OUT)/fensterchef

#Find all source files
SOURCES := $(shell find src -name '*.c')
#Get all corresponding object paths
OBJECTS := $(patsubst src/%.c,$(OUT)/%.o,$(SOURCES))

#Get dependencies
DEPENDENCIES := $(patsubst %.o,%.d,$(OBJECTS))

#Include dependencies (.d files) generated by gcc
-include $(DEPENDENCIES)
#Build each object from corresponding source file
$(OUT)/%.o: src/%.c
	mkdir -p $(dir $@)
	gcc $(DEBUG_FLAGS) $(C_FLAGS) -c $< -o $@ -MMD

#Build the main executable from all object files
$(RUN): $(OBJECTS)
	mkdir -p $(dir $@)
	gcc $(DEBUG_FLAGS) $(C_FLAGS) $(OBJECTS) -o $@ $(XCB)

#Functions
.PHONY:
build: $(RUN)

.PHONY:
run: build
	Xephyr -br -ac -noreset -screen 800x600 :1 &
	sleep 1
	DISPLAY=:1 ./$(RUN) &
	DISPLAY=:1 alacritty &

.PHONY:
stop:
	pkill Xephyr

.PHONY:
release:
	mkdir -p release
	gcc $(RELEASE_FLAGS) $(C_FLAGS) $(SOURCES) -o release/fensterchef $(XCB)

.PHONY:
clean:
	rm -rf $(OUT)
