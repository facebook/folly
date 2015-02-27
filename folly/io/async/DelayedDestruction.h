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

#include <boost/noncopyable.hpp>
#include <inttypes.h>
#include <assert.h>

namespace folly {

/**
 * DelayedDestruction is a helper class to ensure objects are not deleted
 * while they still have functions executing in a higher stack frame.
 *
 * This is useful for objects that invoke callback functions, to ensure that a
 * callback does not destroy the calling object.
 *
 * Classes needing this functionality should:
 * - derive from DelayedDestruction
 * - make their destructor private or protected, so it cannot be called
 *   directly
 * - create a DestructorGuard object on the stack in each public method that
 *   may invoke a callback
 *
 * DelayedDestruction does not perform any locking.  It is intended to be used
 * only from a single thread.
 */
class DelayedDestruction : private boost::noncopyable {
 public:
  /**
   * Helper class to allow DelayedDestruction classes to be used with
   * std::shared_ptr.
   *
   * This class can be specified as the destructor argument when creating the
   * shared_ptr, and it will destroy the guarded class properly when all
   * shared_ptr references are released.
   */
  class Destructor {
   public:
    void operator()(DelayedDestruction* dd) const {
      dd->destroy();
    }
  };

  /**
   * destroy() requests destruction of the object.
   *
   * This method will destroy the object after it has no more functions running
   * higher up on the stack.  (i.e., No more DestructorGuard objects exist for
   * this object.)  This method must be used instead of the destructor.
   */
  virtual void destroy() {
    // If guardCount_ is not 0, just set destroyPending_ to delay
    // actual destruction.
    if (guardCount_ != 0) {
      destroyPending_ = true;
    } else {
      destroyNow(false);
    }
  }

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

    explicit DestructorGuard(DelayedDestruction* dd) : dd_(dd) {
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
      if (dd_->guardCount_ == 0 && dd_->destroyPending_) {
        dd_->destroyPending_ = false;
        dd_->destroyNow(true);
      }
    }

   private:
    DelayedDestruction* dd_;
  };

 protected:
  /**
   * destroyNow() is invoked to actually destroy the object, after destroy()
   * has been called and no more DestructorGuard objects exist.  By default it
   * calls "delete this", but subclasses may override this behavior.
   *
   * @param delayed  This parameter is true if destruction was delayed because
   *                 of a DestructorGuard object, or false if destroyNow() is
   *                 being called directly from destroy().
   */
  virtual void destroyNow(bool delayed) {
    delete this;
    (void)delayed; // prevent unused variable warnings
  }

  DelayedDestruction()
    : guardCount_(0)
    , destroyPending_(false) {}

  /**
   * Protected destructor.
   *
   * Making this protected ensures that users cannot delete DelayedDestruction
   * objects directly, and that everyone must use destroy() instead.
   * Subclasses of DelayedDestruction must also define their destructors as
   * protected or private in order for this to work.
   *
   * This also means that DelayedDestruction objects cannot be created
   * directly on the stack; they must always be dynamically allocated on the
   * heap.
   *
   * In order to use a DelayedDestruction object with a shared_ptr, create the
   * shared_ptr using a DelayedDestruction::Destructor as the second argument
   * to the shared_ptr constructor.
   */
  virtual ~DelayedDestruction() {}

  /**
   * Get the number of DestructorGuards currently protecting this object.
   *
   * This is primarily intended for debugging purposes, such as asserting
   * that an object has at least 1 guard.
   */
  uint32_t getDestructorGuardCount() const {
    return guardCount_;
  }

 private:
  /**
   * guardCount_ is incremented by DestructorGuard, to indicate that one of
   * the DelayedDestruction object's methods is currently running.
   *
   * If destroy() is called while guardCount_ is non-zero, destruction will
   * be delayed until guardCount_ drops to 0.  This allows DelayedDestruction
   * objects to invoke callbacks without having to worry about being deleted
   * before the callback returns.
   */
  uint32_t guardCount_;

  /**
   * destroyPending_ is set to true if destoy() is called while guardCount_ is
   * non-zero.
   *
   * If destroyPending_ is true, the object will be destroyed the next time
   * guardCount_ drops to 0.
   */
  bool destroyPending_;
};
} // folly
