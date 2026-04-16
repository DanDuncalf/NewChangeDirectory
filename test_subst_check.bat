@echo off
mkdir C:\Users\Dan\AppData\Local\Temp\ncd_test_subst_check 2>nul
subst Y: C:\Users\Dan\AppData\Local\Temp\ncd_test_subst_check >nul 2>&1
if errorlevel 1 (
    echo SUBST FAILED
) else (
    echo SUBST OK
    subst Y: /d >nul 2>&1
)
rmdir C:\Users\Dan\AppData\Local\Temp\ncd_test_subst_check 2>nul
