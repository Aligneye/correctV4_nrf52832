@echo off
setlocal

set OPENOCD="C:\Users\harsh\.platformio\packages\tool-openocd\bin\openocd.exe"
set OPENOCD_SCRIPTS="C:\Users\harsh\.platformio\packages\tool-openocd\scripts"

echo [RTT] Starting OpenOCD RTT server...
start /b "" %OPENOCD% -s %OPENOCD_SCRIPTS% -f interface/cmsis-dap.cfg -f target/nrf52.cfg -c "init" -c "rtt setup 0x20000000 0x10000 \"SEGGER RTT\"" -c "rtt start" -c "rtt server start 9090 0"

echo [RTT] Waiting for RTT server to initialize...
timeout /t 3 /nobreak >nul

echo [RTT] Starting monitor...
pio device monitor --port socket://localhost:9090 --baud 115200

endlocal
