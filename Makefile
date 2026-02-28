CC ?= gcc
LD ?= ld
OBJCOPY ?= objcopy

TARGET := BOOTX64.EFI
MIN_TARGET := BOOTX64_MIN.EFI
BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj
MAIN_SO := $(BUILD_DIR)/BOOTX64.so
MAIN_EFI := $(BUILD_DIR)/$(TARGET)
MIN_SO := $(BUILD_DIR)/BOOTX64_MIN.so
MIN_EFI := $(BUILD_DIR)/$(MIN_TARGET)

SRCS := src/main.c src/gfx.c src/font.c src/shell.c src/util.c
OBJS := $(SRCS:src/%.c=$(OBJ_DIR)/%.o)
MIN_SRCS := src/minimal_main.c
MIN_OBJS := $(MIN_SRCS:src/%.c=$(OBJ_DIR)/%.o)

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

CFLAGS := -std=c11 -ffreestanding -fno-stack-protector -fpic -fshort-wchar -mno-red-zone -maccumulate-outgoing-args -DEFI_FUNCTION_WRAPPER -Wall -Wextra -I$(EFI_INC) -I$(EFI_ARCH_INC) -Isrc
LDFLAGS := -nostdlib -znocombreloc -T $(EFI_LDS) -shared -Bsymbolic -L$(LIB_DIR) -L/usr/lib -L/usr/lib64 -L/usr/lib/x86_64-linux-gnu
OBJCOPY_EFI_FLAGS := -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel -j .rela -j .rel.* -j .rela.* -j .reloc --target=efi-app-x86_64

.PHONY: all minimal clean check-env copy-efi

all: check-env $(MAIN_EFI)

minimal: check-env $(MIN_EFI)

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

$(MAIN_SO): $(OBJS)
	$(LD) $(LDFLAGS) $(CRT0) $(OBJS) -o $@ -lefi -lgnuefi

$(MIN_SO): $(MIN_OBJS)
	$(LD) $(LDFLAGS) $(CRT0) $(MIN_OBJS) -o $@ -lefi -lgnuefi

$(MAIN_EFI): $(MAIN_SO)
	$(OBJCOPY) $(OBJCOPY_EFI_FLAGS) $< $@
	@cp $@ $(TARGET)
	@echo "Built $(MAIN_EFI) and copied to ./$(TARGET)"

$(MIN_EFI): $(MIN_SO)
	$(OBJCOPY) $(OBJCOPY_EFI_FLAGS) $< $@
	@cp $@ $(MIN_TARGET)
	@echo "Built $(MIN_EFI) and copied to ./$(MIN_TARGET)"

copy-efi: $(MAIN_EFI)
	@cp $(MAIN_EFI) $(TARGET)

clean:
	@rm -rf $(BUILD_DIR) $(TARGET) $(MIN_TARGET)
