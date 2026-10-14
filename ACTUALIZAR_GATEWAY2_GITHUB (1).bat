@echo off
setlocal EnableExtensions EnableDelayedExpansion
title ACTUALIZAR KORVO GATEWAY2 - GITHUB

set "PROJECT=C:\korvo_gateway2"
set "REPO=https://github.com/alfonsohernandezvaras/korvo_gateway2.git"

where git >nul 2>&1
if errorlevel 1 (echo ERROR: Git no esta instalado o no esta en PATH.&pause&exit /b 1)

if not exist "%PROJECT%\CMakeLists.txt" (echo ERROR: No se encuentra el proyecto en %PROJECT%&pause&exit /b 1)
cd /d "%PROJECT%"
if errorlevel 1 (echo ERROR: No se pudo entrar al proyecto.&pause&exit /b 1)

git rev-parse --is-inside-work-tree >nul 2>&1
if errorlevel 1 (echo ERROR: La carpeta no es un repositorio Git. No se inicializara automaticamente.&pause&exit /b 1)

for /f "delims=" %%R in ('git rev-parse --show-toplevel') do set "ROOT=%%R"
rem Git suele devolver C:/ruta mientras Windows usa C:\ruta.
set "ROOT=!ROOT:/=\!"
for %%P in ("%PROJECT%") do set "EXPECTED_ROOT=%%~fP"
if /I not "!ROOT!"=="!EXPECTED_ROOT!" (echo ERROR: La raiz Git es !ROOT!, no !EXPECTED_ROOT!.&pause&exit /b 1)

git remote get-url origin >nul 2>&1
if errorlevel 1 (echo Configurando remoto origin...&git remote add origin "%REPO%")
for /f "delims=" %%U in ('git remote get-url origin') do set "ACTUAL_REMOTE=%%U"
echo Remoto actual: !ACTUAL_REMOTE!
echo Remoto esperado: %REPO%
set "REMOTE_OK=0"
if /I "!ACTUAL_REMOTE!"=="%REPO%" set "REMOTE_OK=1"
if /I "!ACTUAL_REMOTE!"=="git@github.com:alfonsohernandezvaras/korvo_gateway2.git" set "REMOTE_OK=1"
if "!REMOTE_OK!"=="0" (echo ERROR: El remoto no corresponde al repositorio esperado. No se cambia automaticamente.&pause&exit /b 1)

for /f "delims=" %%B in ('git branch --show-current') do set "BRANCH=%%B"
if /I not "!BRANCH!"=="main" (echo ERROR: Rama actual !BRANCH!. Cambia manualmente a main antes de continuar.&pause&exit /b 1)

if exist ".git\rebase-merge" (echo ERROR: Hay un rebase pendiente.&pause&exit /b 1)
if exist ".git\rebase-apply" (echo ERROR: Hay un rebase pendiente.&pause&exit /b 1)
if exist ".git\MERGE_HEAD" (echo ERROR: Hay un merge pendiente.&pause&exit /b 1)

echo.
echo ==================================================
echo PROYECTO: %PROJECT%
echo DESTINO:  %REPO%
echo ==================================================
echo.
git status --short
echo.
echo ARCHIVOS IGNORADOS (ejemplo: build, sdkconfig):
git status --short --ignored | findstr /B "!!"
echo.
echo ATENCION: Se agregaran archivos nuevos, modificados y eliminados NO ignorados.
choice /M "Continuar con commit y push"
if errorlevel 2 (echo Cancelado.&pause&exit /b 0)

git add -A
if errorlevel 1 (echo ERROR en git add.&pause&exit /b 1)
echo.
echo CAMBIOS PREPARADOS:
git diff --cached --stat
echo.
choice /M "Confirmar estos cambios"
if errorlevel 2 (echo Cancelado. Los cambios quedaron en staging.&pause&exit /b 0)

git diff --cached --quiet
if errorlevel 1 (
  set "COMMIT_MSG="
  set /p "COMMIT_MSG=Mensaje commit [ENTER = Gateway2 respaldo antes etapa 2]: "
  if not defined COMMIT_MSG set "COMMIT_MSG=Gateway2 respaldo antes etapa 2"
  git commit -m "!COMMIT_MSG!"
  if errorlevel 1 (echo ERROR al crear commit.&pause&exit /b 1)
) else (
  echo No hay cambios nuevos. Se verificara sincronizacion.
)

echo.
echo Consultando origin...
git fetch origin
if errorlevel 1 (echo ERROR en git fetch. Commit local conservado.&pause&exit /b 1)

echo.
echo Actualizando main sin reescribir historia...
git pull --rebase origin main
if errorlevel 1 (echo ERROR en pull/rebase. Resolver conflictos antes de push.&pause&exit /b 1)

echo.
echo Subiendo a GitHub...
git push -u origin main
if errorlevel 1 (echo ERROR en git push. Commit local conservado.&pause&exit /b 1)

echo.
echo ===== GATEWAY2 SINCRONIZADO =====
git log -1 --oneline
git status
pause
endlocal
