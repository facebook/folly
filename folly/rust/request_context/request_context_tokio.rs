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

//! Automatic `folly::RequestContext` propagation for Tokio tasks.
//!
//! When a Tokio task is spawned, the spawner's current `RequestContext` is
//! captured. Before each poll of that task, the captured context is restored on
//! the worker thread; after each poll, the (possibly updated) context is saved
//! back. When the task terminates, its entry is removed.
//!
//! # Usage
//!
//! ```no_run
//! use request_context_tokio::BuilderExt as _;
//!
//! let runtime = tokio::runtime::Builder::new_multi_thread()
//!     .install_request_context_hooks()
//!     .enable_all()
//!     .build()
//!     .unwrap();
//! ```
//!
//! After installing the hooks, any `tokio::spawn`ed task will automatically
//! inherit the spawner's `RequestContext` — no `.with_current_rctx()` needed.
//!
//! # Limitations
//!
//! - `spawn_blocking`: hooks do **not** fire — manual propagation still needed.
//! - `LocalSet`: not supported by Tokio hooks.
//! - `block_on`: no hooks fire, but the calling thread already has its context.

use std::cell::Cell;
use std::sync::LazyLock;

use papaya::HashMap;
use request_context::RequestContext;
use rustc_hash::FxBuildHasher;
use tokio::runtime;
use tokio::runtime::TaskMeta;
use tokio::task;

/// Per-task context slot that allows interior mutability without locking.
///
/// # Safety
///
/// This type implements `Sync` because Tokio guarantees that at most one thread
/// polls a given task at any time. The `Cell` is only accessed between paired
/// `on_before_task_poll` / `on_after_task_poll` hooks on the same worker thread,
/// so there are no concurrent mutations.
struct TaskContextSlot(Cell<Option<RequestContext>>);

// SAFETY: See struct-level documentation.
unsafe impl Sync for TaskContextSlot {}

impl TaskContextSlot {
    fn new(ctx: Option<RequestContext>) -> Self {
        Self(Cell::new(ctx))
    }

    fn take(&self) -> Option<RequestContext> {
        self.0.take()
    }

    fn set(&self, ctx: Option<RequestContext>) {
        self.0.set(ctx);
    }
}

/// Per-task `RequestContext` storage, keyed by Tokio's globally-unique task ID.
/// Uses `FxBuildHasher` for cheaper hashing — task IDs are integers, so the
/// DoS-resistant default `RandomState` is unnecessary.
static TASK_CONTEXTS: LazyLock<HashMap<task::Id, TaskContextSlot, FxBuildHasher>> =
    LazyLock::new(|| HashMap::with_hasher(FxBuildHasher));

thread_local! {
   /// Saves the worker thread's ambient `RequestContext` while a task is being
   /// polled, so it can be restored in `on_after_task_poll`.
   static PREV_FRC: Cell<Option<Option<RequestContext>>> = const { Cell::new(None) };
}

/// Extension trait that adds `install_request_context_hooks` to
/// [`tokio::runtime::Builder`].
pub trait BuilderExt {
    /// Install lifecycle hooks that automatically propagate
    /// `folly::RequestContext` across `tokio::spawn` boundaries.
    fn install_request_context_hooks(&mut self) -> &mut Self;
}

impl BuilderExt for runtime::Builder {
    fn install_request_context_hooks(&mut self) -> &mut Self {
        self.on_task_spawn(on_task_spawn)
            .on_before_task_poll(on_before_task_poll)
            .on_after_task_poll(on_after_task_poll)
            .on_task_terminate(on_task_terminate)
    }
}

/// Called when `tokio::spawn` is invoked. Captures the spawner's current
/// `RequestContext` and stores it for the new task.
fn on_task_spawn(meta: &TaskMeta<'_>) {
    let rctx = RequestContext::try_get_current();
    TASK_CONTEXTS
        .pin()
        .insert(meta.id(), TaskContextSlot::new(rctx));
}

/// Called just before the runtime polls a task. Restores the task's saved
/// `RequestContext` onto the current thread and stashes the thread's previous
/// context so it can be restored afterwards.
fn on_before_task_poll(meta: &TaskMeta<'_>) {
    let task_rctx = TASK_CONTEXTS
        .pin()
        .get(&meta.id())
        .and_then(|entry| entry.take());
    let prev = RequestContext::swap_current(task_rctx.as_ref());
    PREV_FRC.set(Some(prev));
}

/// Called just after the runtime finishes polling a task. Saves the task's
/// (possibly updated) context back and restores the thread's previous context.
fn on_after_task_poll(meta: &TaskMeta<'_>) {
    let saved = PREV_FRC.take().unwrap_or(None);

    let task_ctx = RequestContext::swap_current(saved.as_ref());
    if let Some(entry) = TASK_CONTEXTS.pin().get(&meta.id()) {
        entry.set(task_ctx);
    }
}

/// Called when a task is done (completed, cancelled, or dropped). Removes the
/// task's context from the map.
fn on_task_terminate(meta: &TaskMeta<'_>) {
    TASK_CONTEXTS.pin().remove(&meta.id());
}

#[cfg(test)]
mod tests {
    use std::sync::Arc;

    use tokio::sync::Barrier;

    use super::*;

    /// Helper: build a multi-thread runtime with the hooks installed.
    fn build_runtime() -> runtime::Runtime {
        runtime::Builder::new_multi_thread()
            .install_request_context_hooks()
            .worker_threads(2)
            .enable_all()
            .build()
            .unwrap()
    }

    #[test]
    fn spawned_task_inherits_context() {
        let rt = build_runtime();
        rt.block_on(async {
            RequestContext::create();
            let parent_id = RequestContext::get_current().get_root_id();

            let child_id =
                tokio::spawn(
                    async move { RequestContext::try_get_current().map(|c| c.get_root_id()) },
                )
                .await
                .unwrap();

            assert_eq!(child_id, Some(parent_id));
        });
    }

    #[test]
    fn spawned_task_with_no_context() {
        let rt = build_runtime();
        rt.block_on(async {
            // No RequestContext on this thread.
            let child_ctx =
                tokio::spawn(async { RequestContext::try_get_current().map(|c| c.get_root_id()) })
                    .await
                    .unwrap();

            assert!(child_ctx.is_none());
        });
    }

    #[test]
    fn nested_spawns_propagate() {
        let rt = build_runtime();
        rt.block_on(async {
            RequestContext::create();
            let parent_id = RequestContext::get_current().get_root_id();

            let grandchild_id = tokio::spawn(async move {
                // Child should have the parent's context.
                let child_id = RequestContext::try_get_current().map(|c| c.get_root_id());
                assert_eq!(child_id, Some(parent_id));

                // Spawn a grandchild — should also inherit.
                tokio::spawn(
                    async move { RequestContext::try_get_current().map(|c| c.get_root_id()) },
                )
                .await
                .unwrap()
            })
            .await
            .unwrap();

            assert_eq!(grandchild_id, Some(parent_id));
        });
    }

    #[test]
    fn context_survives_yield() {
        let rt = build_runtime();
        rt.block_on(async {
            RequestContext::create();
            let parent_id = RequestContext::get_current().get_root_id();

            let child_id = tokio::spawn(async move {
                // Yield to the runtime — the hook should restore context
                // when we're polled again.
                tokio::task::yield_now().await;
                RequestContext::try_get_current().map(|c| c.get_root_id())
            })
            .await
            .unwrap();

            assert_eq!(child_id, Some(parent_id));
        });
    }

    #[test]
    fn independent_tasks_have_independent_contexts() {
        let rt = build_runtime();
        rt.block_on(async {
            // Create context A.
            RequestContext::create();
            let id_a = RequestContext::get_current().get_root_id();
            let barrier = Arc::new(Barrier::new(2));

            let b1 = barrier.clone();
            let h1 = tokio::spawn(async move {
                // This task has context A.
                let my_id = RequestContext::try_get_current().map(|c| c.get_root_id());
                assert_eq!(my_id, Some(id_a));
                b1.wait().await;
                // After barrier, should still have context A.
                RequestContext::try_get_current().map(|c| c.get_root_id())
            });

            // Create a new context B for the second spawn.
            RequestContext::create();
            let id_b = RequestContext::get_current().get_root_id();
            assert_ne!(id_a, id_b);

            let b2 = barrier.clone();
            let h2 = tokio::spawn(async move {
                let my_id = RequestContext::try_get_current().map(|c| c.get_root_id());
                assert_eq!(my_id, Some(id_b));
                b2.wait().await;
                RequestContext::try_get_current().map(|c| c.get_root_id())
            });

            let r1 = h1.await.unwrap();
            let r2 = h2.await.unwrap();
            assert_eq!(r1, Some(id_a));
            assert_eq!(r2, Some(id_b));
        });
    }

    #[test]
    fn compatible_with_with_current_rctx() {
        use request_context_future::FutureExt as _;

        let rt = build_runtime();
        rt.block_on(async {
            RequestContext::create();
            let parent_id = RequestContext::get_current().get_root_id();

            // Using .with_current_rctx() on top of the hooks should still work.
            let child_id = tokio::spawn(
                async move { RequestContext::try_get_current().map(|c| c.get_root_id()) }
                    .with_current_rctx(),
            )
            .await
            .unwrap();

            assert_eq!(child_id, Some(parent_id));
        });
    }

    #[test]
    fn cleanup_on_task_completion() {
        let rt = build_runtime();
        rt.block_on(async {
            RequestContext::create();

            tokio::spawn(async {
                // Just complete immediately.
            })
            .await
            .unwrap();

            // Give a moment for terminate hook to fire.
            tokio::task::yield_now().await;

            // The map should not grow unboundedly. We can't easily check a
            // specific task ID, but we can verify the map isn't leaking by
            // checking it's small.
            let len = TASK_CONTEXTS.pin().len();
            assert!(
                len <= 2,
                "task context map should not accumulate entries, found {}",
                len
            );
        });
    }

    #[test]
    fn context_restored_after_panic() {
        let rt = build_runtime();
        rt.block_on(async {
            RequestContext::create();
            let parent_id = RequestContext::get_current().get_root_id();

            // Spawn a task that panics — on_after_task_poll should still
            // restore the worker thread's previous context.
            let handle = tokio::spawn(async {
                panic!("intentional panic for testing unwind safety");
            });

            // The JoinHandle returns Err on panic.
            assert!(handle.await.is_err());

            // Verify the parent context is still intact on this thread.
            let current_id = RequestContext::try_get_current().map(|c| c.get_root_id());
            assert_eq!(current_id, Some(parent_id));
        });
    }

    #[test]
    fn context_survives_abort() {
        let rt = build_runtime();
        rt.block_on(async {
            RequestContext::create();
            let parent_id = RequestContext::get_current().get_root_id();

            let barrier = Arc::new(Barrier::new(2));
            let b = barrier.clone();

            // Spawn a long-running task, then abort it while it's waiting.
            let handle = tokio::spawn(async move {
                // Signal that we've started and have been polled at least once.
                b.wait().await;
                // Sleep long enough to be aborted mid-execution.
                tokio::time::sleep(std::time::Duration::from_secs(60)).await;
                RequestContext::try_get_current().map(|c| c.get_root_id())
            });

            // Wait until the spawned task has been polled at least once.
            barrier.wait().await;

            // Abort the task — this cancels it between polls.
            handle.abort();

            // The JoinHandle returns a JoinError with is_cancelled() == true.
            let result = handle.await;
            assert!(result.is_err());
            assert!(result.unwrap_err().is_cancelled());

            // The parent's RequestContext must still be intact.
            let current_id = RequestContext::try_get_current().map(|c| c.get_root_id());
            assert_eq!(
                current_id,
                Some(parent_id),
                "parent RequestContext must survive child task abort"
            );

            // Allow terminate hook to fire, then verify cleanup.
            tokio::task::yield_now().await;
            let len = TASK_CONTEXTS.pin().len();
            assert!(
                len <= 2,
                "aborted task should be cleaned up from TASK_CONTEXTS, found {} entries",
                len
            );
        });
    }
}
