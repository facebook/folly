/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//! Simple utilities for managing a folly::RequestContext

use std::panic::AssertUnwindSafe;
use std::panic::catch_unwind;
use std::panic::resume_unwind;

#[cxx::bridge]
unsafe mod ffi {
    #[namespace = "folly"]
    extern "C++" {
        type RequestContext;
    }

    extern "C++" {
        include!("folly/rust/request_context/request_context.h");

        #[cxx_name = "getRootId"]
        fn get_root_id(self: &RequestContext) -> isize;

        #[namespace = "facebook::rust"]
        fn create_request_context();
        #[namespace = "facebook::rust"]
        fn get_folly_request_context() -> SharedPtr<RequestContext>;
        #[namespace = "facebook::rust"]
        fn set_folly_request_context(ctx: &SharedPtr<RequestContext>) -> SharedPtr<RequestContext>;
        #[namespace = "facebook::rust"]
        fn copy_folly_request_context_as_root(
            ctx: &SharedPtr<RequestContext>,
            rootid: isize,
        ) -> SharedPtr<RequestContext>;
        #[namespace = "facebook::rust"]
        fn copy_folly_request_context_as_child(
            ctx: &SharedPtr<RequestContext>,
        ) -> SharedPtr<RequestContext>;
        #[namespace = "facebook::rust"]
        fn with_folly_request_context(
            rctx: &SharedPtr<RequestContext>,
            func: fn(&mut WithInner),
            arg: &mut WithInner,
        ) -> SharedPtr<RequestContext>;
    }

    #[namespace = "facebook::rust"]
    extern "Rust" {
        type WithInner<'a>;
    }
}

unsafe impl Send for ffi::RequestContext {}
unsafe impl Sync for ffi::RequestContext {}

/// Closure for with_folly_request_context
type WithInner<'a> = &'a mut dyn FnTake;

pub trait FnTake {
    fn call(&mut self);
}

impl<F: FnOnce()> FnTake for Option<F> {
    fn call(&mut self) {
        let f = self.take().expect("already called");
        f();
    }
}

#[derive(Clone)]
pub struct RequestContext(cxx::SharedPtr<ffi::RequestContext>);

impl RequestContext {
    /// Create a new request context. This implicitly sets the request context
    /// for the current thread, overriding any existing context.
    pub fn create() {
        ffi::create_request_context();
    }

    /// Return root id for request context.
    pub fn get_root_id(&self) -> isize {
        self.0.get_root_id()
    }

    /// Return id for request as string
    ///
    /// This matches fbcode/thrift/lib/cpp2/server/RequestsRegistry.cpp
    /// RequestsRegistry::getRequestId but always using the folly request root
    /// id.
    pub fn get_request_id(&self) -> String {
        format!("{:016x}", self.get_root_id())
    }

    /// Shallow-copy this context while overriding the root id.
    pub fn copy_as_root(&self, root_id: isize) -> RequestContext {
        RequestContext(ffi::copy_folly_request_context_as_root(&self.0, root_id))
    }

    /// Shallow-copy this context while preserving the parent's root id.
    pub fn copy_as_child(&self) -> RequestContext {
        RequestContext(ffi::copy_folly_request_context_as_child(&self.0))
    }

    /// Get current per-thread RequestContext
    ///
    /// This will panic if no request context has been set up.
    #[track_caller]
    pub fn get_current() -> RequestContext {
        Self::try_get_current().expect("No folly::RequestContext set")
    }

    /// Get current per-thread RequestContext
    ///
    /// This may return None if no request context has been set up (ie, there's
    /// only a static default request context).
    pub fn try_get_current() -> Option<RequestContext> {
        let ctx = ffi::get_folly_request_context();
        if ctx.is_null() {
            None
        } else {
            Some(RequestContext(ffi::get_folly_request_context()))
        }
    }

    /// Run a function with a given RequestContext. This also re-captures the
    /// request context after the code has run and returns it.
    ///
    /// Assumes F is [std::panic::UnwindSafe].
    pub fn with_context<F: FnOnce()>(&self, func: F) -> RequestContext {
        let mut caught = None;
        let caught_ref = &mut caught;
        let mut inner = &mut Some(move || match catch_unwind(AssertUnwindSafe(func)) {
            Ok(()) => (),
            Err(e) => {
                *caught_ref = Some(e);
            }
        }) as WithInner;

        let new_context =
            ffi::with_folly_request_context(&self.0, |inner| inner.call(), &mut inner);
        if let Some(e) = caught {
            resume_unwind(e);
        }

        RequestContext(new_context)
    }

    /// Swap the current thread's `RequestContext` with the given one.
    ///
    /// Sets `ctx` as the current thread-local `RequestContext` and returns the
    /// previous context. If `ctx` is `None`, an empty (null) `SharedPtr` is set,
    /// which effectively clears the current context back to the global default.
    ///
    /// This is useful for manually managing `RequestContext` propagation across
    /// thread or task boundaries without RAII scope guards.
    pub fn swap_current(ctx: Option<&RequestContext>) -> Option<RequestContext> {
        let empty = cxx::SharedPtr::null();
        let to_set = match ctx {
            Some(rc) => &rc.0,
            None => &empty,
        };
        let prev = ffi::set_folly_request_context(to_set);
        if prev.is_null() {
            None
        } else {
            Some(RequestContext(prev))
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_new_context() {
        let id1 = RequestContext::try_get_current().map(|ctx| ctx.get_root_id());
        assert!(id1.is_none());

        RequestContext::create();

        let id2 = RequestContext::try_get_current().map(|ctx| ctx.get_root_id());
        assert!(id2.is_some());

        assert_ne!(id1, id2);
    }

    #[test]
    #[should_panic(expected = "expected panic")]
    fn with_context_propagates_panics() {
        assert!(RequestContext::try_get_current().is_none());
        RequestContext::create();

        RequestContext::get_current().with_context(|| panic!("expected panic"));
    }

    #[test]
    fn swap_current_sets_and_restores() {
        // Start with no context.
        assert!(RequestContext::try_get_current().is_none());

        // Create a context and capture it.
        RequestContext::create();
        let ctx = RequestContext::try_get_current().unwrap();
        let root_id = ctx.get_root_id();

        // Swap in None — clears the current context, returns the previous one.
        let prev = RequestContext::swap_current(None);
        assert!(prev.is_some());
        assert_eq!(prev.unwrap().get_root_id(), root_id);

        // Current context should now be empty.
        assert!(RequestContext::try_get_current().is_none());

        // Swap the original back in.
        let prev2 = RequestContext::swap_current(Some(&ctx));
        assert!(prev2.is_none());

        // Current context should be the original again.
        let restored = RequestContext::try_get_current().unwrap();
        assert_eq!(restored.get_root_id(), root_id);

        // Clean up: clear the context for other tests.
        RequestContext::swap_current(None);
    }

    #[test]
    fn copy_as_root_sets_requested_root_id() {
        RequestContext::swap_current(None);
        RequestContext::create();

        let ctx = RequestContext::get_current();
        let original_root_id = ctx.get_root_id();
        let copied = ctx.copy_as_root(12345);

        assert_eq!(copied.get_root_id(), 12345);
        assert_eq!(ctx.get_root_id(), original_root_id);

        RequestContext::swap_current(None);
    }

    #[test]
    fn copy_as_child_preserves_root_id() {
        RequestContext::swap_current(None);
        RequestContext::create();

        let ctx = RequestContext::get_current();
        let root_id = ctx.get_root_id();
        let copied = ctx.copy_as_child();

        assert_eq!(copied.get_root_id(), root_id);

        RequestContext::swap_current(None);
    }
}
