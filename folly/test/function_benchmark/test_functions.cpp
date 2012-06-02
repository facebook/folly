// Copyright 2004-present Facebook.  All rights reserved.
#include "folly/test/function_benchmark/test_functions.h"

/*
 * These functions are defined in a separate file so that
 * gcc won't be able to inline them.
 */

void doNothing() {
}

void TestClass::doNothing() {
}

VirtualClass::~VirtualClass() {
}

void VirtualClass::doNothing() {
};
