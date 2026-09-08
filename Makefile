# TuringOS v2 — build system. `make help` lists targets.
CC      ?= cc
EMCC    ?= emcc
CFLAGS  := -std=c99 -O2 -Wall -Wextra -Werror -pedantic -I./src $(EXTRA_CFLAGS)
LDFLAGS := $(EXTRA_LDFLAGS)

BUILD := build
BIN   := $(BUILD)/bin
GEN   := $(BUILD)/gen
OBJ   := $(BUILD)/obj
DISK  := $(BUILD)/disk
WEBPUB := web/public
WEBGEN := web/src/generated

ALL_SRC    := $(sort $(shell find src -name '*.c'))
CORE_SRC   := $(filter-out src/main.c src/hal/hal_posix.c src/hal/hal_wasm.c src/shell/shell_tpa.c,$(ALL_SRC))
NATIVE_SRC := $(CORE_SRC) src/hal/hal_posix.c
WASM_SRC   := $(CORE_SRC) src/hal/hal_wasm.c
LIB_OBJ    := $(NATIVE_SRC:%.c=$(OBJ)/%.o) $(OBJ)/gen/shell_blob.o
LIB        := $(BUILD)/libtos.a
TARGET     := $(BUILD)/turingos
SHELL_COM  := $(BIN)/shell.com
SHELL_BLOB := $(GEN)/shell_blob.c

TOOLS := $(BUILD)/mkdisk $(BUILD)/cc_driver $(BUILD)/asm $(BUILD)/tmc $(BUILD)/bfc $(BUILD)/disasm \
         $(BUILD)/bench $(BUILD)/dump_constants $(BUILD)/dump_layout $(BUILD)/bin2c

DEMO_FILES := $(sort $(shell find demos -type f ! -name 'README.md' ! -name 'tour.json' ! -name '*.expected' ! -name '*.py' 2>/dev/null))

.PHONY: all tools shell gen disk demo-disk test wasm web test-web run bench asm tm bf disasm clean help

## all: build the native binary (build/turingos) and all host tools
all: $(TARGET) tools

$(LIB): $(LIB_OBJ)
	@mkdir -p $(dir $@)
	ar rcs $@ $^

$(TARGET): src/main.c $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) src/main.c $(LIB) $(LDFLAGS) -o $@

$(OBJ)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ)/gen/shell_blob.o: $(SHELL_BLOB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

## shell: compile src/shell/shell_tpa.c with the tiny-C compiler -> build/bin/shell.com
shell: $(SHELL_COM)
$(SHELL_COM): $(BUILD)/cc_driver src/shell/shell_tpa.c
	@mkdir -p $(dir $@)
	$(BUILD)/cc_driver src/shell/shell_tpa.c $@
	@echo "Generated $@ ($$(wc -c < $@) bytes)"

$(SHELL_BLOB): $(SHELL_COM) $(BUILD)/bin2c
	@mkdir -p $(dir $@)
	$(BUILD)/bin2c $(SHELL_COM) $@ tos_shell_blob

## tools: build every host tool under build/
tools: $(TOOLS)
$(BUILD)/bin2c: tools/bin2c.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< -o $@
$(BUILD)/cc_driver: tools/cc_driver.c src/compiler/compiler.c src/compiler/compiler.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) tools/cc_driver.c src/compiler/compiler.c -o $@
$(BUILD)/asm: tools/asm.c src/lang/asm.c src/lang/asm.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) tools/asm.c src/lang/asm.c -o $@
$(BUILD)/tmc: tools/tmc.c src/lang/tm.c src/lang/tm.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) tools/tmc.c src/lang/tm.c -o $@
$(BUILD)/bfc: tools/bfc.c src/lang/bf.c src/lang/bf.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) tools/bfc.c src/lang/bf.c -o $@
$(BUILD)/disasm: tools/disasm.c $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) tools/disasm.c $(LIB) $(LDFLAGS) -o $@
$(BUILD)/mkdisk: tools/mkdisk.c $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) tools/mkdisk.c $(LIB) $(LDFLAGS) -o $@
$(BUILD)/bench: tools/bench.c $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) tools/bench.c $(LIB) $(LDFLAGS) -o $@
$(BUILD)/dump_constants: tools/dump_constants.c src/tos.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) tools/dump_constants.c -o $@
$(BUILD)/dump_layout: tools/dump_layout.c src/emu/cpu.h src/kernel/kernel.h src/kernel/trace.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) tools/dump_layout.c -o $@

## gen: generate build/gen/{constants,layout}.json and copy them to web/src/generated/
gen: $(BUILD)/dump_constants $(BUILD)/dump_layout $(SHELL_BLOB)
	@mkdir -p $(GEN) $(WEBGEN)
	$(BUILD)/dump_constants > $(GEN)/constants.json
	$(BUILD)/dump_layout > $(GEN)/layout.json
	cp $(GEN)/constants.json $(GEN)/layout.json $(WEBGEN)/

## disk: create a blank build/disk/disk.img
disk: $(BUILD)/mkdisk
	@mkdir -p $(DISK)
	$(BUILD)/mkdisk $(DISK)/disk.img --format

## demo-disk: build build/disk/demo.img from demos/** (+ SHELL.C) and copy it to web/public/
demo-disk: $(BUILD)/mkdisk
	@mkdir -p $(DISK) $(WEBPUB)
	rm -f $(DISK)/demo.img
	$(BUILD)/mkdisk $(DISK)/demo.img --format $(foreach f,$(DEMO_FILES),--add $(f)) --add src/shell/shell_tpa.c:SHELL.C
	cp $(DISK)/demo.img $(WEBPUB)/demo.img

## test: build everything and run the C + shell test suite
test: all shell gen disk demo-disk
	./tests/run_tests.sh

## wasm: build web/public/turingos.{js,wasm} with Emscripten
wasm: $(SHELL_BLOB)
	@mkdir -p $(WEBPUB)
	EXPORTS="$$(grep -oE 'tos_[a-z0-9_]+\(' src/api/api.h | tr -d '(' | sort -u | sed 's/^/"_/; s/$$/"/' | paste -sd, -)"; \
	$(EMCC) -Os -std=c99 -I./src $(WASM_SRC) $(SHELL_BLOB) \
	  -s MODULARIZE=1 -s EXPORT_NAME=createTuringOS -s ENVIRONMENT=web,node \
	  -s ALLOW_MEMORY_GROWTH=0 -s INITIAL_MEMORY=67108864 -s STACK_SIZE=1048576 \
	  -s EXPORTED_FUNCTIONS="[$$EXPORTS,\"_malloc\",\"_free\"]" \
	  -s EXPORTED_RUNTIME_METHODS='["HEAPU8","HEAPU32","HEAP32","UTF8ToString","stringToUTF8","lengthBytesUTF8","getValue","setValue"]' \
	  -o $(WEBPUB)/turingos.js
	@ls -la $(WEBPUB)/turingos.wasm | awk '{print "wasm size: " $$5 " bytes"}'

## web: wasm + demo disk + generated json, then `npm ci && npm run build` in web/
web: wasm gen demo-disk
	cd web && npm ci && npm run build

## test-web: regenerate web content and run the Node tests under web/test against the wasm build (needs `npm ci` in web/ once)
test-web: wasm gen demo-disk
	cd web && npm run content && node --test test/

## run: run the OS interactively in this terminal
run: all shell disk
	$(TARGET) --disk=$(DISK)/disk.img

## bench: print native steps/s
bench: $(BUILD)/bench
	$(BUILD)/bench
	$(BUILD)/bench --trace

## asm: assemble FILE=<path.asm> into <path>.com
asm: $(BUILD)/asm
	$(BUILD)/asm $(FILE) $(basename $(FILE)).com

## tm: compile FILE=<path.tm> (Turing-machine language) into <path>.com
tm: $(BUILD)/tmc
	$(BUILD)/tmc $(FILE) $(basename $(FILE)).com

## bf: compile FILE=<path.bf> (Brainfuck) into <path>.com
bf: $(BUILD)/bfc
	$(BUILD)/bfc $(FILE) $(basename $(FILE)).com

## disasm: disassemble FILE=<path.com>
disasm: $(BUILD)/disasm
	$(BUILD)/disasm $(FILE)

## clean: remove build outputs
clean:
	rm -rf $(BUILD) web/dist $(WEBPUB)/turingos.js $(WEBPUB)/turingos.wasm $(WEBPUB)/demo.img $(WEBGEN)

## help: list targets
help:
	@grep -E '^## ' $(MAKEFILE_LIST) | sed 's/^## //' | sort
