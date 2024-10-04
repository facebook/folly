# Buck2 Shims for Meta

These files implement shims for Meta internal buck2 cells, macros, and targets.

Via these shims, the buck2 experience when building Meta open source projects
should be nearly identical to the internal buck2 experience.

## These shims are not recommended for non-Meta projects!!!

Prefer to use [rules from the buck2 prelude](https://buck2.build/docs/prelude/globals/)
and the [buck2 build apis](https://buck2.build/docs/api/build/globals/)
