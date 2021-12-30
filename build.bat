@REM Copyright (c) Meta Platforms, Inc. and affiliates.
@REM
@REM Licensed under the Apache License, Version 2.0 (the "License");
@REM you may not use this file except in compliance with the License.
@REM You may obtain a copy of the License at
@REM
@REM     http://www.apache.org/licenses/LICENSE-2.0
@REM
@REM Unless required by applicable law or agreed to in writing, software
@REM distributed under the License is distributed on an "AS IS" BASIS,
@REM WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
@REM See the License for the specific language governing permissions and
@REM limitations under the License.

@echo off
SETLOCAL
if exist %~dp0\..\..\opensource\fbcode_builder\getdeps.py (
  set GETDEPS=%~dp0\..\..\opensource\fbcode_builder\getdeps.py
) else if exist %~dp0\build\fbcode_builder\getdeps.py (
  set GETDEPS=%~dp0\build\fbcode_builder\getdeps.py
) else (
  echo "error: unable to find getdeps.py"
  exit 1
)

python3.exe %GETDEPS% build folly %*
