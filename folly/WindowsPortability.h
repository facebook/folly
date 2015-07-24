/*
 * Copyright 2015 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef WINDOWSPORTABILITY_H
#define WINDOWSPORTABILITY_H
/*
* This header is intended to be used in-place of including <windows.h>
* It includes the file, and undefines certain names defined by it that
* are used in places in Folly. This should also be used as the base for
* any similar structures in other projects.
*/

#ifndef _WIN32
# error This header should only be included on windows!
#endif
#include <Windows.h>

// Defined in the GDI interface.
#ifdef ERROR
# undef ERROR
#endif

// Defined in Winbase.h
#ifdef Yield
# undef Yield
#endif

#endif
