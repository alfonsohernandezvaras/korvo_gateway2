@echo off
title KORVO GATEWAY 2 - ESP-IDF 5.3.1

set "IDF_PATH=C:\Espressif\frameworks\esp-idf-v5.3.1"
set "IDF_PYTHON_ENV_PATH=C:\Espressif\python_env\idf5.3_py3.11_env"

set "PATH=C:\Espressif\python_env\idf5.3_py3.11_env\Scripts;%PATH%"

call C:\Espressif\frameworks\esp-idf-v5.3.1\export.bat

cd /d C:\korvo_gateway2

cls
echo ============================================================
echo          KORVO GATEWAY 2 - ESP-IDF 5.3.1
echo ============================================================
echo.
echo Proyecto:
cd
echo.
echo Hardware:
echo ESP32-ETH01 / LAN8720
echo.
echo Comandos:
echo   idf.py fullclean
echo   idf.py set-target esp32
echo   idf.py build
echo   idf.py -p COM9 -b 115200 flash
echo   idf.py -p COM9 -b 115200 monitor
echo   idf.py -p COM9 -b 115200 flash monitor
echo.
echo ============================================================
echo.

cd /d C:\korvo_gateway2

%COMSPEC% /D /K