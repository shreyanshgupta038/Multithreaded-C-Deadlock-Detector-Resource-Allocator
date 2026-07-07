@echo off
echo ========================================================
echo Compiling Deadlock Detector and Resource Allocator C App...
echo ========================================================

gcc -Wall -O2 -o main.exe main.c rag.c smart_mutex.c

if %ERRORLEVEL% EQU 0 (
    echo [SUCCESS] Compilation complete: main.exe created!
) else (
    echo [ERROR] Compilation failed. See compiler output above.
)
