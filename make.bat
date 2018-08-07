@ECHO OFF
SETLOCAL

SET BAZELROOT=.
REM rename bazel-0.16.0-windows-x86_64.exe to bazel.exe
SET BAZEL=%BAZELROOT%\bazel.exe
REM Python must be 64-bit
SET PYROOT=C:\Users\hpabst\AppData\Local\Programs\Python\Python37
SET PATH=%BAZELROOT%;%PYROOT%;%PYROOT%\Scripts;%PATH%

REM python -m pip install --upgrade pip
REM pip install numpy

REM Update MinGW-64 using the MinGW-Shell
REM pacman -Syu

REM python .\configure.py
REM SET TF_NEED_KAFKA=0
REM SET BAZEL_BUILD=%BAZEL% build -s --verbose_failures
SET BAZEL_BUILD=%BAZEL% build

%BAZEL_BUILD% --config=mkl -c opt --copt=-O2 --copt=-mfma --copt=-mavx2 //tensorflow/tools/pip_package:build_pip_package

ENDLOCAL
