@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cl /nologo /O2 /EHsc /std:c++17 /DNAM_SAMPLE_FLOAT /I core /I core\NAM /I core\Dependencies /I core\Dependencies\eigen /I core\Dependencies\nlohmann /I core\Dependencies\AudioDSPTools ref.cpp core\NAM\*.cpp core\NAM\wavenet\*.cpp /Fe:ref.exe
