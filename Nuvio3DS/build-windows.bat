@echo off
setlocal enabledelayedexpansion

cd /d "%~dp0"

set "DKPRO=C:\devkitPro"
if defined DEVKITPRO if exist "%DEVKITPRO%\devkitARM\bin\arm-none-eabi-g++.exe" set "DKPRO=%DEVKITPRO%"

set "GPP=%DKPRO%\devkitARM\bin\arm-none-eabi-g++.exe"
set "TOOLS=%DKPRO%\tools\bin"
set "PORTLIBS=%DKPRO%\portlibs\3ds"
set "CTRULIB=%DKPRO%\libctru"
set "PACMAN=%DKPRO%\msys2\usr\bin\pacman.exe"

if not exist "%GPP%" (
    echo [Nuvio3DS] devkitARM was not found at %GPP%
    echo Install devkitProUpdater from https://devkitpro.org and check "3DS Development",
    echo OR push this folder to GitHub to build automatically with .github\workflows\build.yml
    pause
    exit /b 1
)

if not exist "%PORTLIBS%\include\curl\curl.h" (
    if exist "%PACMAN%" (
        echo [Nuvio3DS] Installing 3ds-curl, 3ds-mbedtls, and 3ds-zlib libraries...
        "%PACMAN%" -S --needed --noconfirm 3ds-dev 3ds-curl 3ds-mbedtls 3ds-zlib
    ) else (
        echo [Nuvio3DS] Missing 3ds-curl headers in %PORTLIBS%\include\curl\curl.h
        pause
        exit /b 1
    )
)

set "PATH=%DKPRO%\devkitARM\bin;%TOOLS%;%PATH%"
if not exist "build" mkdir "build"

set "ARCH=-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft"
set "CXXFLAGS=-g -Wall -O2 -mword-relocations -ffunction-sections -fdata-sections %ARCH% -fno-rtti -fno-exceptions -std=gnu++17 -D__3DS__ -DNUVIO3DS_VERSION=\"1.0.0\" -Isource -I%PORTLIBS%\include -I%CTRULIB%\include"
set "LDFLAGS=-specs=3dsx.specs -g %ARCH% -Wl,-Map,Nuvio3DS.map -L%PORTLIBS%\lib -L%CTRULIB%\lib"
set "LIBS=-lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -lcitro2d -lcitro3d -lctru -lm"

set "OBJS="
for /r "%~dp0source" %%F in (*.cpp) do (
    echo [CXX] %%~nxF
    "%GPP%" %CXXFLAGS% -c "%%F" -o "build\%%~nF.o"
    if errorlevel 1 (
        echo [Nuvio3DS] Compilation failed on %%~nxF
        pause
        exit /b 1
    )
    set "OBJS=!OBJS! "build\%%~nF.o""
)

echo [LINK] Nuvio3DS.elf
"%GPP%" %LDFLAGS% !OBJS! %LIBS% -o Nuvio3DS.elf
if errorlevel 1 (
    echo [Nuvio3DS] Linking failed.
    pause
    exit /b 1
)

if exist "%CTRULIB%\default_icon.png" (
    "%TOOLS%\smdhtool.exe" --create "Nuvio3DS" "Native Nuvio & Stremio Addon Client for New 3DS XL" "Nuvio3DS Contributors" "%CTRULIB%\default_icon.png" Nuvio3DS.smdh
    "%TOOLS%\3dsxtool.exe" Nuvio3DS.elf Nuvio3DS.3dsx --smdh=Nuvio3DS.smdh
) else (
    "%TOOLS%\3dsxtool.exe" Nuvio3DS.elf Nuvio3DS.3dsx
)
if errorlevel 1 (
    echo [Nuvio3DS] 3dsxtool failed.
    pause
    exit /b 1
)
echo [OK] Built Nuvio3DS.3dsx

set "MAKEROM="
if exist "%TOOLS%\makerom.exe" set "MAKEROM=%TOOLS%\makerom.exe"
if not defined MAKEROM if exist "%~dp0makerom.exe" set "MAKEROM=%~dp0makerom.exe"
if defined MAKEROM (
    echo [CIA] Building Nuvio3DS.cia...
    "%MAKEROM%" -f cia -o Nuvio3DS.cia -elf Nuvio3DS.elf -rsf meta\nuvio3ds.rsf -target t
    if errorlevel 1 (
        echo [Nuvio3DS] makerom failed.
        pause
        exit /b 1
    )
    echo [OK] Built Nuvio3DS.cia
) else (
    echo [NOTE] Nuvio3DS.3dsx is ready. To also build Nuvio3DS.cia for FBI QR install,
    echo        place Project_CTR makerom.exe into %TOOLS%\ or this folder and re-run.
)

echo.
echo Done! Next steps:
echo   - Homebrew Launcher: copy Nuvio3DS.3dsx + nuvio3ds.xml to sdmc:\3ds\Nuvio3DS\
echo   - FBI QR Install: run "py -m http.server 8080" in this folder and scan QR in FBI
pause
