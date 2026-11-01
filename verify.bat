@ECHO OFF
REM ============================================================================
REM  verify.bat - the ONLY supported "is M-green?" entry point
REM
REM  Why this file exists: on M1a a compile failed (build.bat exit 4) but
REM  tests.bat was run anyway, against the STALE test binary left over from the
REM  previous build. It printed a plausible-looking red list that described a
REM  source tree which no longer existed. tests.bat cannot tell fresh from stale,
REM  so the ordering has to be enforced here.
REM
REM  Rule: never trust a test run whose build step did not exit 0.
REM ============================================================================
SETLOCAL EnableExtensions EnableDelayedExpansion
PUSHD "%~dp0"

CALL build.bat
IF ERRORLEVEL 1 (
    ECHO [FAIL] build did not succeed - refusing to run test binaries of unknown age
    EXIT /B 1
)

CALL tests.bat
SET "TR=!ERRORLEVEL!"
IF NOT "!TR!"=="0" (
    ECHO [FAIL] tests reported failure, exit !TR!
    EXIT /B 2
)

ECHO [ OK ] verify: build green AND tests green in both directions
EXIT /B 0
