@echo off
REM tools\graph_export\run.bat
REM Usage: tools\graph_export\run.bat <config_or_dot> <output_json> [--seed N] [--directed]
REM
REM Example (from project root):
REM   tools\graph_export\run.bat configs\small.conf outputs\graph.json

if "%~1"=="" (
    echo Usage: %~nx0 ^<config_or_dot_file^> ^<output_json^> [extra args]
    exit /b 1
)

set SCRIPT_DIR=%~dp0
python "%SCRIPT_DIR%export_graph.py" %*
