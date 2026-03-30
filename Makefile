CC := gcc
CFLAGS := -std=c99 -Wall -Wextra -Werror -pedantic -I./src
LDFLAGS :=

BUILD_DIR := build
BIN_DIR := $(BUILD_DIR)/bin
TARGET := $(BUILD_DIR)/turingos

SRC := \
	src/main.c \
	src/emu/mem.c \
	src/emu/cpu.c \
	src/bios/bios.c \
	src/kernel/kernel.c \
	src/fs/fs.c \
	src/shell/shell.c \
	src/compiler/compiler.c

OBJ := $(SRC:%.c=$(BUILD_DIR)/%.o)

.PHONY: all run test disk shell clean viz agents

all: $(TARGET)

$(TARGET): $(OBJ)
	@mkdir -p "$(dir $@)"
	$(CC) $(OBJ) $(LDFLAGS) -o "$@"

$(BUILD_DIR)/%.o: %.c
	@mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -c "$<" -o "$@"

run: all
	"$(TARGET)"

test: all
	./tests/run_tests.sh

disk: $(BUILD_DIR)/mkdisk
	@mkdir -p "$(BUILD_DIR)/disk"
	"$(BUILD_DIR)/mkdisk"

$(BUILD_DIR)/mkdisk: tools/mkdisk.c
	@mkdir -p "$(dir $@)"
	$(CC) -std=c99 -Wall -Wextra -Werror -pedantic "$<" -o "$@"

$(BUILD_DIR)/cc_driver: tools/cc_driver.c src/compiler/compiler.c src/compiler/compiler.h
	@mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) tools/cc_driver.c src/compiler/compiler.c -o "$@"

shell: $(BUILD_DIR)/cc_driver
	@mkdir -p "$(BIN_DIR)"
	"$(BUILD_DIR)/cc_driver" src/shell/shell.c "$(BIN_DIR)/shell.com"
	@echo "Generated $(BIN_DIR)/shell.com."

clean:
	rm -rf "$(BUILD_DIR)" "$(BIN_DIR)/shell.com" disk.img

viz:
	python3 viz/visualizer.py

agents:
	python3 tools/agent_supervisor.py
