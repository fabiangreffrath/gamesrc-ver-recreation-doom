@echo off
cls
type BATCHLST.TXT
choice /C:123456789ABC0 /N Please select what to build:
echo.
if ERRORLEVEL 13 goto end
if ERRORLEVEL 1 set TARGET=DM1666P
if ERRORLEVEL 2 set TARGET=DM1666E
if ERRORLEVEL 3 set TARGET=DM1666
if ERRORLEVEL 4 set TARGET=DM17
if ERRORLEVEL 5 set TARGET=DM17A
if ERRORLEVEL 6 set TARGET=DM18FR
if ERRORLEVEL 7 set TARGET=DM18
if ERRORLEVEL 8 set TARGET=DM19
if ERRORLEVEL 9 set TARGET=DM950328
if ERRORLEVEL 10 set TARGET=DM19U
if ERRORLEVEL 11 set TARGET=DM19F
if ERRORLEVEL 12 set TARGET=DM19F2

if "%1" == "USE_APODMX" goto apodmx

if not exist ..\dmx goto dmxerror
set USE_APODMX=0
goto task

:dmxerror
echo Can't recreate Doom EXE, you need a compatible version of
echo DMX headers under ..\dmx. You also need a compatible version of
echo the DMX library, again under ..\dmx. See makefile for more details.
echo Alternatively, run "DOBUILD.BAT USE_APODMX" to use APODMX instead.
goto end

:apodmx
if not exist ..\apodmx\apodmx.lib goto apodmxerror
REM AUDIO_WF.LIB is copied as a workaround for too long path
set USE_APODMX=1
goto task

:apodmxerror
echo Can't recreate Doom EXE, you need the APODMX headers
echo and the APODMX.LIB file under ..\apodmx ready, as well
echo as ..\audiolib\origexes\109\AUDIO_WF.LIB.
goto end

:task
REM Since environment variables may actually impact the compiler output,
REM use a helper script in order to try and refrain from them

mkdir %TARGET%
echo wmake.exe %TARGET%\newdoom.exe "appver_exedef = %TARGET%" "use_apodmx = %USE_APODMX%" > BUILDTMP.BAT
echo del BUILDTMP.BAT >> BUILDTMP.BAT
set TARGET=
set USE_APODMX=
BUILDTMP.BAT

:end
set TARGET=
