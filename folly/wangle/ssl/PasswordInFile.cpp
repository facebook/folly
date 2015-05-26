/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/wangle/ssl/PasswordInFile.h>

#include <folly/FileUtil.h>

using namespace std;

namespace folly {

PasswordInFile::PasswordInFile(const string& file)
    : fileName_(file) {
  folly::readFile(file.c_str(), password_);
  auto p = password_.find('\0');
  if (p != std::string::npos) {
    password_.erase(p);
  }
}

PasswordInFile::~PasswordInFile() {
  OPENSSL_cleanse((char *)password_.data(), password_.length());
}

}
