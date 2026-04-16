@echo off
REM tools\partition\run.bat
REM Usage: tools\partition\run.bat <graph_json> --ranks <N> --out <output_json> [--strategy S]
REM
REM Example (from project root):
REM   tools\partition\run.bat outputs\graph.json --ranks 8 --out outputs\part.json

setlocal enabledelayedexpansion

if "%~1"=="" (
    echo Usage: %~nx0 ^<graph_json^> --ranks ^<N^> --out ^<output_json^> [--strategy contiguous^|round-robin]
    exit /b 1
)

set SCRIPT_DIR=%~dp0
set GRAPH_JSON=%~1
set OUTPUT_JSON=
set RANKS_ARG=
set STRATEGY_ARG=

shift

:loop
if "%~1"=="" goto run
if "%~1"=="--out" (
    set OUTPUT_JSON=%~2
    shift
    shift
    goto loop
)
if "%~1"=="--ranks" (
    set RANKS_ARG=--ranks %~2
    shift
    shift
    goto loop
)
if "%~1"=="--strategy" (
    set STRATEGY_ARG=--strategy %~2
    shift
    shift
    goto loop
)
shift
goto loop

:run
if "%GRAPH_JSON%"=="" (
    echo ERROR: graph_json required
    exit /b 1
)
if "%OUTPUT_JSON%"=="" (
    echo ERROR: --out required
    exit /b 1
)

python "%SCRIPT_DIR%partition_graph.py" "%GRAPH_JSON%" "%OUTPUT_JSON%" %RANKS_ARG% %STRATEGY_ARG%
