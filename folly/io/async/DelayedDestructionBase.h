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

#pragma once

#include <assert.h>
#include <boost/noncopyable.hpp>
#include <functional>
#include <glog/logging.h>
#include <inttypes.h>

namespace folly {

/**
 * DelayedDestructionBase is a helper class to ensure objects are not deleted
 * while they still have functions executing in a higher stack frame.
 *
 * This is useful for objects that invoke callback functions, to ensure that a
 * callback does not destroy the calling object.
 *
 * Classes needing this functionality should:
 * - derive from DelayedDestructionBase directly
 * - pass a callback to onDestroy_ which'll be called before the object is
 *   going to be destructed
 * - create a DestructorGuard object on the stack in each public method that
 *   may invoke a callback
 *
 * DelayedDestructionBase does not perform any locking.  It is intended to be
 * used only from a single thread.
 */
class DelayedDestructionBase : private boost::noncopyable {
 public:
  virtual ~DelayedDestructionBase() = default;

  /**
   * Classes should create a DestructorGuard object on the stack in any
   * function that may invoke callback functions.
   *
   * The DestructorGuard prevents the guarded class from being destroyed while
   * it exists.  Without this, the callback function could delete the guarded
   * object, causing problems when the callback function returns and the
   * guarded object's method resumes execution.
   */
  class DestructorGuard {
   public:

    explicit DestructorGuard(DelayedDestructionBase* dd) : dd_(dd) {
      ++dd_->guardCount_;
      assert(dd_->guardCount_ > 0); // check for wrapping
    }

    DestructorGuard(const DestructorGuard& dg) : dd_(dg.dd_) {
      ++dd_->guardCount_;
      assert(dd_->guardCount_ > 0); // check for wrapping
    }

    ~DestructorGuard() {
      assert(dd_->guardCount_ > 0);
      --dd_->guardCount_;
      if (dd_->guardCount_ == 0) {
        dd_->onDestroy_(true);
      }
    }

   private:
    DelayedDestructionBase* dd_;
  };

 protected:
  DelayedDestructionBase()
    : guardCount_(0) {}

  /**
   * Get the number of DestructorGuards currently protecting this object.
   *
   * This is primarily intended for debugging purposes, such as asserting
   * that an object has at least 1 guard.
   */
  uint32_t getDestructorGuardCount() const {
    return guardCount_;
  }

  /**
   * Implement onDestroy_ in subclasses.
   * onDestroy_() is invoked when the object is potentially being destroyed.
   *
   * @param delayed  This parameter is true if destruction was delayed because
   *                 of a DestructorGuard object, or false if onDestroy_() is
   *                 being called directly from the destructor.
   */
  std::function<void(bool)> onDestroy_;

 private:
  /**
   * guardCount_ is incremented by DestructorGuard, to indicate that one of
   * the DelayedDestructionBase object's methods is currently running.
   *
   * If the destructor is called while guardCount_ is non-zero, destruction
   * will be delayed until guardCount_ drops to 0.  This allows
   * DelayedDestructionBase objects to invoke callbacks without having to worry
   * about being deleted before the callback returns.
   */
  uint32_t guardCount_;
};
} // folly
