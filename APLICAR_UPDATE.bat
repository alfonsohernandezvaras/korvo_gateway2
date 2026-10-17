@echo off
setlocal
cd /d C:\korvo_gateway2
if errorlevel 1 goto :error
py -3 "%~dp0APLICAR_UPDATE.py"
if errorlevel 1 goto :error
echo.
echo R8 aplicado. Compilar con idf.py build antes de flashear.
pause
exit /b 0
:error
echo ERROR: R8 NO APLICADO. Revise ruta, Git y mensajes anteriores.
pause
exit /b 1
