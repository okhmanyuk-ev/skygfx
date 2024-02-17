mkdir build
cd build
mkdir win_dll
cd ..


cmake -B build/win_dll -DSKYGFX_DLL=True
pause
