@echo off
echo ========================================
echo Configurando o projeto...
echo ========================================

cmake -S . -B build -A x64

if errorlevel 1 (
    echo.
    echo [ERRO] Falha na configuracao do CMake!
    pause
    exit /b 1
)

echo.
echo ========================================
echo Limpando artefatos antigos...
echo ========================================

if exist build\Release\plugin rmdir /s /q build\Release\plugin
if exist build\plugin rmdir /s /q build\plugin
for /r build %%F in (NppGrandFantasia.dll) do if exist "%%F" del /q "%%F"

echo.
echo ========================================
echo Compilando Release...
echo ========================================

cmake --build build --config Release --clean-first

if errorlevel 1 (
    echo.
    echo [ERRO] Falha na compilacao!
    pause
    exit /b 1
)

if exist build\Release\NppGrandFantasia.exp del /q build\Release\NppGrandFantasia.exp

echo.
echo ========================================
echo BUILD CONCLUIDO COM SUCESSO!
echo ========================================
echo DLL: build\Release\NppGrandFantasia.dll
echo LIB: build\Release\NppGrandFantasia.lib
echo PDB: build\Release\NppGrandFantasia.pdb

exit /b 0
