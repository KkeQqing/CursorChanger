@ECHO OFF
REM ============================================================================
REM  CursorSwitch build (M0)
REM
REM  Machine-specific traps this script is written around (AGENTS section 8):
REM   1. vcvars64.bat is NEVER redirected. Redirecting it silently loses the
REM      environment, and cl then cannot be found.
REM   2. No redirection characters inside ECHO text. The usual "arrow" sequences
REM      in status messages all contain one, and cmd then overwrites the exe with
REM      text - the classic "compiled OK but the exe is 13 bytes" result.
REM   3. A previous instance still holding the exe causes the same truncated
REM      output, so instances are killed before linking.
REM   4. Artifacts are size-checked only AFTER a successful cl exit code. That
REM      order is what makes a stale artifact unable to fake a green run.
REM   5. Version/build metadata come from src/core/version.h, not from command
REM      line defines, because quoting through cmd plus cl is a needless failure
REM      mode for a value with one home.
REM   6. This script performs NO directory deletion. Every listed source is
REM      recompiled each run and the linker overwrites, so output stays fresh.
REM      If you want a hard clean, delete the "build" folder interactively.
REM ============================================================================
SETLOCAL EnableExtensions EnableDelayedExpansion
PUSHD "%~dp0"

SET "VCVARS=D:\VS IDE\VC\Auxiliary\Build\vcvars64.bat"
SET "OUTDIR=build"
SET "APP=cursorswitch.exe"
SET "TESTS=cursorswitch_tests.exe"
SET "MINBYTES=65536"
SET "FLAGS=/nologo /std:c++17 /W4 /utf-8 /EHsc /MT /Od /Zi /FC /Isrc"

IF NOT EXIST "%VCVARS%" GOTO :L_NOVCVARS

REM ---- 1. drop stale instances (link race) ----
TASKKILL /IM %APP% /F 1>NUL 2>&1
TASKKILL /IM %TESTS% /F 1>NUL 2>&1

REM ---- 2. output dir (create only, never delete) ----
IF NOT EXIST "%OUTDIR%" MKDIR "%OUTDIR%"

REM ---- 3. compiler environment: do NOT redirect this call ----
SET "VSLANG=1033"
CALL "%VCVARS%"
IF ERRORLEVEL 1 GOTO :L_FAIL_ENV

REM ---- 4. compile app ----
CL %FLAGS% /Fo%OUTDIR%\ src\core\cli.cpp src\app\main.cpp ^
   /Fe%OUTDIR%\%APP% /link /SUBSYSTEM:CONSOLE > "%OUTDIR%\compile-app.log" 2>&1
IF ERRORLEVEL 1 GOTO :L_FAIL_COMPILE

REM ---- 5. compile tests ----
CL %FLAGS% /Fo%OUTDIR%\ src\core\cli.cpp src\core\ir.cpp src\core\slots.cpp ^
   src\core\claim.cpp src\core\cur_writer.cpp src\core\cur_reader.cpp ^
   src\tests\tests_main.cpp src\tests\tests_tables.cpp src\tests\tests_cur.cpp ^
   /Fe%OUTDIR%\%TESTS% /link /SUBSYSTEM:CONSOLE > "%OUTDIR%\compile-tests.log" 2>&1
IF ERRORLEVEL 1 GOTO :L_FAIL_COMPILE

REM ---- 6. verify both artifacts exist and are big enough ----
SET "BAD=0"
CALL :L_CHECKSIZE "%OUTDIR%\%APP%"
CALL :L_CHECKSIZE "%OUTDIR%\%TESTS%"
IF NOT "!BAD!"=="0" GOTO :L_FAIL_SIZE

POPD
ECHO [ OK ] build complete: 2 artifacts verified
EXIT /B 0

REM ---------------------------------------------------------------------------
:L_CHECKSIZE
SET "F=%~1"
IF NOT EXIST "%F%" (
    ECHO [FAIL] missing artifact: "%F%"
    SET "BAD=1"
    EXIT /B 0
)
SET "SZ=0"
FOR %%A IN ("%F%") DO SET "SZ=%%~zA"
ECHO [SIZE] "%F%" = !SZ! bytes
IF !SZ! LSS %MINBYTES% (
    ECHO [FAIL] too small - broken link, or an ECHO line redirected text into it
    SET "BAD=1"
)
EXIT /B 0

:L_NOVCVARS
ECHO [FAIL] vcvars64.bat not found at "%VCVARS%"
ECHO [HINT] point VCVARS in build.bat at this machine's copy
EXIT /B 2

:L_FAIL_ENV
ECHO [FAIL] vcvars64 call failed
EXIT /B 3

:L_FAIL_COMPILE
ECHO [FAIL] cl failed, logs follow
ECHO --- compile-app.log ---
IF EXIST "%OUTDIR%\compile-app.log" TYPE "%OUTDIR%\compile-app.log"
ECHO --- compile-tests.log ---
IF EXIST "%OUTDIR%\compile-tests.log" TYPE "%OUTDIR%\compile-tests.log"
EXIT /B 4

:L_FAIL_SIZE
ECHO [FAIL] artifact size gate failed, see the SIZE lines above
EXIT /B 5
