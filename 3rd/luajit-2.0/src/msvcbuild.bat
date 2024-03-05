@rem Script to build LuaJIT with MSVC.
@rem Copyright (C) 2005-2013 Mike Pall. See Copyright Notice in luajit.h
@rem
@rem Either open a "Visual Studio .NET Command Prompt"
@rem (Note that the Express Edition does not contain an x64 compiler)
@rem -or-
@rem Open a "Windows SDK Command Shell" and set the compiler environment:
@rem     setenv /release /x86
@rem   -or-
@rem     setenv /release /x64
@rem
@rem Then cd to this directory and run this script.

@if not defined INCLUDE goto :FAIL

@setlocal
@rem fancy
@set LJCOMPILE=cl /nologo /c /MT /O2 /W3 /D_CRT_SECURE_NO_DEPRECATE /DLUAJIT_DISABLE_FFI
@set LJCOMPILED=cl /nologo /c /MTd /Zi /Fd"lua51d.pdb" /W3 /D_CRT_SECURE_NO_DEPRECATE /DLUAJIT_DISABLE_FFI /DLUA_USE_ASSERT /DLUA_USE_APICHECK
@rem /DLUAJIT_DISABLE_JIT
@set LJLINK=link /nologo
@set LJMT=mt /nologo
@set LJLIB=lib /nologo
@set DASMDIR=..\dynasm
@set DASM=%DASMDIR%\dynasm.lua
@set LJDLLNAME=lua51.dll
@set LJLIBNAME=lua51.lib
@set LJEXENAME=luajit.exe
@set ALL_LIB=lib_base.c lib_math.c lib_bit.c lib_string.c lib_table.c lib_io.c lib_os.c lib_package.c lib_debug.c lib_jit.c lib_ffi.c

@rem fancy
@set LJDLLNAME=lua51.dll
@set LJLIBNAME=lua51.lib
@set LJEXENAME=lua51.exe
@set LJDLLNAMED=lua51d.dll
@set LJLIBNAMED=lua51d.lib
@set LJEXENAMED=lua51d.exe

%LJCOMPILE% host\minilua.c
@if errorlevel 1 goto :BAD
%LJLINK% /out:minilua.exe minilua.obj
@if errorlevel 1 goto :BAD
if exist minilua.exe.manifest^
  %LJMT% -manifest minilua.exe.manifest -outputresource:minilua.exe

@rem fancy
@rem @set DASMFLAGS=-D WIN -D JIT -D FFI -D P64
@set DASMFLAGS=-D WIN -D JIT -D P64
@set LJARCH=x64
@minilua
@if errorlevel 8 goto :X64
@rem fancy
@rem @set DASMFLAGS=-D WIN -D JIT -D FFI
@set DASMFLAGS=-D WIN -D JIT
@set LJARCH=x86
:X64
minilua %DASM% -LN %DASMFLAGS% -o host\buildvm_arch.h vm_x86.dasc
@if errorlevel 1 goto :BAD

%LJCOMPILE% /I "." /I %DASMDIR% host\buildvm*.c
@if errorlevel 1 goto :BAD
%LJLINK% /out:buildvm.exe buildvm*.obj
@if errorlevel 1 goto :BAD
if exist buildvm.exe.manifest^
  %LJMT% -manifest buildvm.exe.manifest -outputresource:buildvm.exe

buildvm -m peobj -o lj_vm.obj
@if errorlevel 1 goto :BAD
buildvm -m bcdef -o lj_bcdef.h %ALL_LIB%
@if errorlevel 1 goto :BAD
buildvm -m ffdef -o lj_ffdef.h %ALL_LIB%
@if errorlevel 1 goto :BAD
buildvm -m libdef -o lj_libdef.h %ALL_LIB%
@if errorlevel 1 goto :BAD
buildvm -m recdef -o lj_recdef.h %ALL_LIB%
@if errorlevel 1 goto :BAD
buildvm -m vmdef -o jit\vmdef.lua %ALL_LIB%
@if errorlevel 1 goto :BAD
buildvm -m folddef -o lj_folddef.h lj_opt_fold.c
@if errorlevel 1 goto :BAD

@if "%1" neq "debug" goto :NODEBUG
@shift
@rem fancy
@set LJDLLNAME=%LJDLLNAMED%
@set LJLIBNAME=%LJLIBNAMED%
@set LJEXENAME=%LJEXENAMED%
@set LJCOMPILE=%LJCOMPILED%
@set LJLINK=%LJLINK% /debug
:NODEBUG
@if "%1"=="amalg" goto :AMALGDLL
@if "%1"=="static" goto :STATIC
%LJCOMPILE% /DLUA_BUILD_AS_DLL lj_*.c lib_*.c
@if errorlevel 1 goto :BAD
%LJLINK% /DLL /out:%LJDLLNAME% lj_*.obj lib_*.obj
@if errorlevel 1 goto :BAD
@goto :MTDLL
:STATIC
%LJCOMPILE% /DLUA_BUILD_AS_DLL lj_*.c lib_*.c
@if errorlevel 1 goto :BAD
%LJLIB% /OUT:%LJLIBNAME% lj_*.obj lib_*.obj
@if errorlevel 1 goto :BAD
@goto :MTDLL
:AMALGDLL
%LJCOMPILE% /DLUA_BUILD_AS_DLL ljamalg.c
@if errorlevel 1 goto :BAD
%LJLINK% /DLL /out:%LJDLLNAME% ljamalg.obj lj_vm.obj
@if errorlevel 1 goto :BAD
:MTDLL
if exist %LJDLLNAME%.manifest^
  %LJMT% -manifest %LJDLLNAME%.manifest -outputresource:%LJDLLNAME%;2

%LJCOMPILE% luajit.c
@if errorlevel 1 goto :BAD
@rem fancy
%LJLINK% /out:%LJEXENAME% luajit.obj %LJLIBNAME%
@if errorlevel 1 goto :BAD
if exist luajit.exe.manifest^
  %LJMT% -manifest luajit.exe.manifest -outputresource:luajit.exe

@del *.obj *.manifest minilua.exe buildvm.exe
@echo.
@echo === Successfully built LuaJIT for Windows/%LJARCH% ===

@goto :END
:BAD
@echo.
@echo *******************************************************
@echo *** Build FAILED -- Please check the error messages ***
@echo *******************************************************
@goto :END
:FAIL
@echo You must open a "Visual Studio .NET Command Prompt" to run this script
:END
REM copy /y %LJLIBNAME% ..\..\thirdpart\luajit\lib\lua51d-VS2015.lib
REM copy /y luaconf.h ..\..\thirdpart\luajit\include\luaconf.h
