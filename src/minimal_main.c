#include <efi.h>
#include <efilib.h>

EFI_STATUS efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table) {
    (void)image_handle;

    if (system_table != NULL && system_table->ConOut != NULL) {
        uefi_call_wrapper(system_table->ConOut->ClearScreen, 1, system_table->ConOut);
        uefi_call_wrapper(system_table->ConOut->OutputString, 2, system_table->ConOut, L"HatterOS minimal EFI test loaded.\r\n");
        uefi_call_wrapper(system_table->ConOut->OutputString, 2, system_table->ConOut, L"Press any key to return to firmware...\r\n");
    }

    if (system_table != NULL && system_table->BootServices != NULL && system_table->ConIn != NULL) {
        EFI_EVENT event = system_table->ConIn->WaitForKey;
        UINTN idx = 0;
        if (event != NULL) {
            uefi_call_wrapper(system_table->BootServices->WaitForEvent, 3, 1, &event, &idx);
            EFI_INPUT_KEY key;
            uefi_call_wrapper(system_table->ConIn->ReadKeyStroke, 2, system_table->ConIn, &key);
        }
    }

    return EFI_SUCCESS;
}
