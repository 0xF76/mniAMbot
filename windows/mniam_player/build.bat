REM Dopasuj poniższe ścieżki:
set MINGW_PATH=D:\AGH\EiT\3_rok\6_sem\mingw64\bin
set CMAKE_PATH=D:\AGH\EiT\3_rok\6_sem\cmake\bin


set PATH=%PATH%;%MINGW_PATH%;%CMAKE_PATH%;

cmake -G "MinGW Makefiles" -B build -DCMAKE_C_FLAGS="-std=c11"
mingw32-make.exe -C build