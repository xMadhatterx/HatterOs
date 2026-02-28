echo -off
cls
echo HatterOS startup.nsh smoke script
echo.
echo Attempting to launch \EFI\BOOT\BOOTX64.EFI from fs0..fs3 ...
if exist fs0:\EFI\BOOT\BOOTX64.EFI then
  fs0:\EFI\BOOT\BOOTX64.EFI
  goto done
endif
if exist fs1:\EFI\BOOT\BOOTX64.EFI then
  fs1:\EFI\BOOT\BOOTX64.EFI
  goto done
endif
if exist fs2:\EFI\BOOT\BOOTX64.EFI then
  fs2:\EFI\BOOT\BOOTX64.EFI
  goto done
endif
if exist fs3:\EFI\BOOT\BOOTX64.EFI then
  fs3:\EFI\BOOT\BOOTX64.EFI
  goto done
endif
echo ERROR: \EFI\BOOT\BOOTX64.EFI not found on fs0..fs3
:done
echo.
echo Returned from HatterOS EFI app.
echo Suggested shell checks once booted:
echo   help
echo   pwd
echo   ls
echo   cat /EFI/BOOT/STARTUP.NSH
