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

use std::future::Future;
use std::pin::Pin;
use std::task::Context;
use std::task::Poll;

use futures_core::Stream;
use pin_project::pin_project;
use request_context::RequestContext;

pub trait FutureExt {
    /// Wrap invocations of a [`Future`] with the thread's current [`RequestContext`].
    ///
    /// If there's no current `RequestContext`, then just run the `Future` as-is.
    fn with_current_rctx(self) -> WithRequestContext<Self>
    where
        Self: Sized;

    /// Wrap invocations of a [`Future`] with a given [`RequestContext`].
    fn with_rctx(self, rctx: RequestContext) -> WithRequestContext<Self>
    where
        Self: Sized;
}

impl<F: Future> FutureExt for F {
    fn with_current_rctx(self) -> WithRequestContext<Self> {
        WithRequestContext::with_current_rctx(self)
    }

    fn with_rctx(self, rctx: RequestContext) -> WithRequestContext<Self> {
        WithRequestContext::with_rctx(self, rctx)
    }
}

pub trait StreamExt {
    /// Wrap invocations of a [`Stream`] with the thread's current [`RequestContext`].
    ///
    /// If there's no current `RequestContext`, then just continue the `Stream` as-is.
    fn with_current_rctx(self) -> WithRequestContext<Self>
    where
        Self: Sized;

    /// Wrap invocations of a [`Stream`] with a given [`RequestContext`].
    fn with_rctx(self, rctx: RequestContext) -> WithRequestContext<Self>
    where
        Self: Sized;
}

impl<F: Stream> StreamExt for F {
    fn with_current_rctx(self) -> WithRequestContext<Self> {
        WithRequestContext::with_current_rctx(self)
    }

    fn with_rctx(self, rctx: RequestContext) -> WithRequestContext<Self> {
        WithRequestContext::with_rctx(self, rctx)
    }
}

/// Future wrapper which sets folly::RequestContext while running poll()
#[pin_project]
pub struct WithRequestContext<F> {
    rctxt: Option<RequestContext>,
    #[pin]
    fut: F,
}

impl<F> WithRequestContext<F> {
    /// Wrap invocations of a [`Future`] with the thread's current [`RequestContext`].
    ///
    /// If there's no current `RequestContext`, then just run the `Future` as-is.
    pub fn with_current_rctx(fut: F) -> Self {
        let rctxt = RequestContext::try_get_current();
        Self { rctxt, fut }
    }

    /// Wrap invocations of a [`Future`] with a given [`RequestContext`].
    pub fn with_rctx(fut: F, rctxt: RequestContext) -> Self {
        Self {
            rctxt: Some(rctxt),
            fut,
        }
    }
}

impl<F: Future> Future for WithRequestContext<F> {
    type Output = F::Output;

    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let mut res = None;

        let this = self.project();
        let fut = this.fut;

        *this.rctxt = if let Some(rctx) = this.rctxt {
            Some(rctx.with_context(|| res = Some(fut.poll(cx))))
        } else {
            res = Some(fut.poll(cx));
            None
        };

        res.unwrap()
    }
}

impl<F: Stream> Stream for WithRequestContext<F> {
    type Item = F::Item;

    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let mut res = None;

        let this = self.project();
        let fut = this.fut;

        *this.rctxt = if let Some(rctx) = this.rctxt {
            Some(rctx.with_context(|| res = Some(fut.poll_next(cx))))
        } else {
            res = Some(fut.poll_next(cx));
            None
        };

        res.unwrap()
    }
}
