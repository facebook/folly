// Copyright 2004-present Facebook.  All rights reserved.
#ifndef TEST_FUNCTIONS_H_
#define TEST_FUNCTIONS_H_

void doNothing();

class TestClass {
 public:
  void doNothing();
};

class VirtualClass {
 public:
  virtual ~VirtualClass();
  virtual void doNothing();
};

#endif // TEST_FUNCTIONS_H_
