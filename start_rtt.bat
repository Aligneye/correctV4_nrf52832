@echo off
echo Starting OpenOCD RTT Server on port 9090...
echo Leave this window open while you want to read the serial monitor!
echo.
"C:\Users\harsh\.platformio\packages\tool-openocd\bin\openocd.exe" -s "C:\Users\harsh\.platformio\packages\tool-openocd\scripts" -f interface/cmsis-dap.cfg -f target/nrf52.cfg -c "init" -c "rtt setup 0x20000000 0x10000 \"SEGGER RTT\"" -c "rtt start" -c "rtt server start 9090 0"
pause
