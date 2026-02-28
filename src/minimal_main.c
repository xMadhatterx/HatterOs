#include <efi.h>

EFI_STATUS efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table) {
    (void)image_handle;

    if (system_table != NULL && system_table->ConOut != NULL) {
        system_table->ConOut->ClearScreen(system_table->ConOut);
        system_table->ConOut->OutputString(system_table->ConOut, L"HatterOS minimal EFI test loaded.\r\n");
        system_table->ConOut->OutputString(system_table->ConOut, L"Press any key to return to firmware...\r\n");
    }

    if (system_table != NULL && system_table->BootServices != NULL && system_table->ConIn != NULL) {
        EFI_EVENT event = system_table->ConIn->WaitForKey;
        UINTN idx = 0;
        if (event != NULL) {
            system_table->BootServices->WaitForEvent(1, &event, &idx);
            EFI_INPUT_KEY key;
            system_table->ConIn->ReadKeyStroke(system_table->ConIn, &key);
        }
    }

    return EFI_SUCCESS;
}