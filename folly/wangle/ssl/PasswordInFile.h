/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <folly/io/async/SSLContext.h> // PasswordCollector

namespace folly {

class PasswordInFile: public folly::PasswordCollector {
 public:
  explicit PasswordInFile(const std::string& file);
  ~PasswordInFile();

  void getPassword(std::string& password, int size) override {
    password = password_;
  }

  const char* getPasswordStr() const {
    return password_.c_str();
  }

  std::string describe() const override {
    return fileName_;
  }

 protected:
  std::string fileName_;
  std::string password_;
};

}
