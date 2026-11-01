@ECHO OFF
REM ============================================================================
REM  CursorSwitch test runner (M0)
REM
REM  This script is the harness self-proof. It asserts BOTH directions:
REM    - normal mode must exit 0
REM    - force-fail mode must NOT exit 0
REM  Without the second assertion, "tests.bat is green" means nothing: a harness
REM  that never counts failures would also always be green. (AGENTS section 4)
REM ============================================================================
SETLOCAL EnableExtensions EnableDelayedExpansion
PUSHD "%~dp0"

SET "OUTDIR=build"
SET "TESTS=%OUTDIR%\cursorswitch_tests.exe"

IF NOT EXIST "%TESTS%" (
    ECHO [FAIL] test exe missing, run build.bat first
    EXIT /B 2
)

REM ---- run A: normal ----
"%TESTS%" > "%OUTDIR%\tests-normal.log" 2>&1
SET "RA=!ERRORLEVEL!"
ECHO [RUN] normal      exit=!RA!

REM ---- run B: force-fail (must be non-zero) ----
"%TESTS%" --force-fail > "%OUTDIR%\tests-forcefail.log" 2>&1
SET "RB=!ERRORLEVEL!"
ECHO [RUN] force-fail  exit=!RB!

ECHO --- tests-normal.log ---
TYPE "%OUTDIR%\tests-normal.log"
ECHO --- tests-forcefail.log ---
TYPE "%OUTDIR%\tests-forcefail.log"

IF NOT "!RA!"=="0" GOTO :L_FAIL_NORMAL_GREEN
IF "!RB!"=="0" GOTO :L_FAIL_RED_PROOF

ECHO [ OK ] harness self-proof passed: green when honest, red when sabotaged
EXIT /B 0

:L_FAIL_NORMAL_GREEN
ECHO [FAIL] normal mode must exit 0
EXIT /B 3

:L_FAIL_RED_PROOF
ECHO [FAIL] force-fail mode exited 0 - the harness cannot detect a failing case
EXIT /B 4
