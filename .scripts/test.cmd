@ECHO OFF
IF DEFINED DebugBuildScripts (
    @ECHO ON
)

SETLOCAL

CALL %~dp0init.cmd

REM TODO: Ensure all errors caught by the smoke test will result in non-zero exit codes
ECHO Running VW C Smoke test
"%vwRoot%\vowpalwabbit\out\target\Release\x64\c_test.exe"

IF "%ERRORLEVEL%" NEQ "0" (
    ENDLOCAL
    EXIT /B %ERRORLEVEL%
)

ECHO Running VW Unit Tests in C++
"%vwRoot%\vowpalwabbit\out\target\Release\x64\vw_unit_test.exe"

IF "%ERRORLEVEL%" NEQ "0" (
    ENDLOCAL
    EXIT /B %ERRORLEVEL%
)

ECHO Running VW Unit Tests in C#
REM TODO: Add explicit logging configuration so it can be uploaded to pipeline results.
"%vstestPath%" /Platform:x64 /inIsolation "%vwRoot%\vowpalwabbit\x64\Release\cs_unittest.dll" /TestCaseFilter:"TestCategory!=NotOnVSO" --logger:trx "--ResultsDirectory:%vwRoot%\vowpalwabbit\out\test\Release\x64"

IF "%ERRORLEVEL%" NEQ "0" (
    ENDLOCAL
    EXIT /B %ERRORLEVEL%
)

ENDLOCAL