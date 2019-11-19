// Copyright (c) Facebook, Inc. and its affiliates.

#define Py_LIMITED_API 1
#define WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <Python.h>
#include <stdio.h>

int wmain() {
  /*
   * This executable will be prepended to the start of a Python ZIP archive.
   * Python will be able to directly execute the ZIP archive, so we simply
   * need to tell Py_Main() to run our own file.  Duplicate the argument list
   * and add our file name to the beginning to tell Python what file to invoke.
   */
  wchar_t** pyargv = malloc(sizeof(wchar_t*) * (__argc + 1));
  if (!pyargv) {
    fprintf(stderr, "error: failed to allocate argument vector\n");
    return 1;
  }
  pyargv[0] = __wargv[0];
  for (int n = 0; n < __argc; ++n) {
    pyargv[n + 1] = __wargv[n];
  }
  return Py_Main(__argc + 1, pyargv);
}
