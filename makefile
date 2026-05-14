# =========================================================
# BUFUS - Blazing USB Flash Utility System
# Native Windows USB Imaging Utility
# MinGW-w64 / MSYS2 Makefile
# =========================================================

# Compiler
CC := gcc

# Directories
SRC_DIR := src
INC_DIR := include
BUILD_DIR := build

# Output
TARGET := $(BUILD_DIR)/bufus.exe

# Source files
SRC := $(wildcard $(SRC_DIR)/*.c)

# Object files
OBJ := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRC))

# Compiler flags
CFLAGS := \
    -O2 \
    -s \
    -Wall \
    -Wextra \
    -std=c11 \
    -I$(INC_DIR)

# Linker flags
LDFLAGS := \
    -lsetupapi \
    -lcfgmgr32 \
    -lkernel32 \
    -luser32

# =========================================================
# Build rules
# =========================================================

all: dirs $(TARGET)

# Create build directory
dirs:
	mkdir -p $(BUILD_DIR)

# Final executable
$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

# Compile source files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# =========================================================
# Utility targets
# =========================================================

run: all
	./$(TARGET) --help

clean:
	@if exist "$(BUILD_DIR)" rmdir /s /q "$(BUILD_DIR)"

rebuild: clean all

debug:
	$(MAKE) CFLAGS="-g -O0 -Wall -Wextra -std=c11 -I$(INC_DIR)"

release:
	$(MAKE) CFLAGS="-O3 -s -DNDEBUG -march=native -I$(INC_DIR)"

# =========================================================
# Phony targets
# =========================================================

.PHONY: all dirs clean rebuild run debug release
