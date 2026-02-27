CC := gcc
LD := ld
OBJCOPY := objcopy

TARGET := BOOTX64.EFI
BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj
BIN_SO := $(BUILD_DIR)/BOOTX64.so
BIN_EFI := $(BUILD_DIR)/$(TARGET)

SRCS := src/main.c src/gfx.c src/font.c src/shell.c src/util.c
OBJS := $(SRCS:src/%.c=$(OBJ_DIR)/%.o)

EFI_INC := $(firstword $(wildcard /usr/include/efi /usr/local/include/efi))
EFI_ARCH_INC := $(firstword $(wildcard /usr/include/efi/x86_64 /usr/local/include/efi/x86_64))

CRT0 := $(firstword $(wildcard \
    /usr/lib/crt0-efi-x86_64.o \
    /usr/lib64/crt0-efi-x86_64.o \
    /usr/lib/x86_64-linux-gnu/crt0-efi-x86_64.o \
    /usr/lib/x86_64-linux-gnu/gnuefi/crt0-efi-x86_64.o \
    /usr/lib64/gnuefi/crt0-efi-x86_64.o))

EFI_LDS := $(firstword $(wildcard \
    /usr/lib/elf_x86_64_efi.lds \
    /usr/lib64/elf_x86_64_efi.lds \
    /usr/lib/x86_64-linux-gnu/elf_x86_64_efi.lds \
    /usr/lib/x86_64-linux-gnu/gnuefi/elf_x86_64_efi.lds \
    /usr/lib64/gnuefi/elf_x86_64_efi.lds))

LIB_DIR := $(dir $(CRT0))

CFLAGS := -std=c11 -ffreestanding -fno-stack-protector -fpic -fshort-wchar -mno-red-zone -Wall -Wextra -I$(EFI_INC) -I$(EFI_ARCH_INC) -Isrc
LDFLAGS := -nostdlib -znocombreloc -T $(EFI_LDS) -shared -Bsymbolic -L$(LIB_DIR)

.PHONY: all clean check-env copy-efi

all: check-env $(BIN_EFI)

check-env:
	@if [ -z "$(EFI_INC)" ] || [ -z "$(EFI_ARCH_INC)" ]; then \
		echo "GNU-EFI headers not found. Install gnu-efi (or efi headers)."; \
		exit 1; \
	fi
	@if [ -z "$(CRT0)" ] || [ -z "$(EFI_LDS)" ]; then \
		echo "GNU-EFI linker files not found. Install gnu-efi dev package."; \
		exit 1; \
	fi

$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)

$(OBJ_DIR)/%.o: src/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_SO): $(OBJS)
	$(LD) $(LDFLAGS) $(CRT0) $(OBJS) -o $@ -lefi -lgnuefi

$(BIN_EFI): $(BIN_SO)
	$(OBJCOPY) -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel -j .rela -j .reloc --target=efi-app-x86_64 $< $@
	@cp $@ $(TARGET)
	@echo "Built $(BIN_EFI) and copied to ./$(TARGET)"

copy-efi: $(BIN_EFI)
	@cp $(BIN_EFI) $(TARGET)

clean:
	@rm -rf $(BUILD_DIR) $(TARGET)