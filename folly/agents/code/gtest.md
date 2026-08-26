# gtest

Load with `testing.md` when gtest tests are in scope.

## Assertions in helpers

A fatal `ASSERT_*` in a helper returns from the helper, not the calling test;
the test continues. Prefer keeping the fatal assertion in the test or returning
a value for the test to assert. If a helper must contain one, name it `check*`
or `expect*`, not `assert*`. Wrap the call in `ASSERT_NO_FATAL_FAILURE(...)`
when the caller must stop.

## Setup state

Prefer `TEST()` with explicit test-local state. For reusable setup, compose
small state objects that each test constructs directly. Use `TEST_F()` only when
flat shared setup is clearer; do not grow fixture inheritance or unrelated
fixture state.
