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

// Only do anything if we are on windows.
#ifdef _WIN32
/*
* This header is intended to be used in-place of including <windows.h>
* It includes the file, and undefines certain names defined by it that
* are used in places in Folly. This should also be used as the base for
* any similar structures in other projects.
*/

#include <Windows.h>
#include <WinSock2.h>

#ifdef CAL_GREGORIAN
# undef CAL_GREGORIAN
#endif

// Defined in winnt.h
#ifdef DELETE
# undef DELETE
#endif

// Defined in the GDI interface.
#ifdef ERROR
# undef ERROR
#endif

// Defined in minwindef.h
#ifdef IN
# undef IN
#endif

// Defined in winerror.h
#ifdef NO_ERROR
# undef NO_ERROR
#endif

// Defined in minwindef.h
#ifdef OUT
# undef OUT
#endif

// Defined in minwindef.h
#ifdef STRICT
# undef STRICT
#endif

// Defined in Winbase.h
#ifdef Yield
# undef Yield
#endif

// Lastly, a few functions whose names
// are the same as things used in HHVM,
// because we're nice like that.
#ifdef GetClassName
# undef GetClassName
#endif

#ifdef GetCommandLine
# undef GetCommandLine
#endif

#ifdef GetCurrentDirectory
# undef GetCurrentDirectory
#endif

#endif
#endif
