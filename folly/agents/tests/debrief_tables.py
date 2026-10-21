# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Render production cost and token tables for a checkpointed backtest."""

from __future__ import annotations

import argparse
import json
import sys
from decimal import Decimal
from pathlib import Path
from typing import Any, Sequence


_MILLION = Decimal(1_000_000)
_COST_PRECISION = Decimal("0.0001")
_TIME_PRECISION = Decimal("0.1")
_RATES = {
    "gpt-5.6-sol": {
        "uncached_input": Decimal("8.00"),
        "cached_input": Decimal("0.80"),
        "cache_write": Decimal("10.00"),
        "output": Decimal("30.00"),
    }
}


def _phase_label(phase: str) -> str:
    if phase == "initial":
        return "Initial draft"
    if phase == "author":
        return "Author review"
    return f"Review {phase.removeprefix('review')} fixes"


def _tokens(record: dict[str, Any]) -> dict[str, int]:
    """Combine author and nested-review usage into billed categories."""
    author = record["author_tokens"]
    reviewers = record["reviewer_tokens"]
    cached = author["cached_input_tokens"] + sum(
        reviewer["cached_input_tokens"] for reviewer in reviewers
    )
    return {
        "uncached_input": author["input_tokens"]
        + sum(reviewer["input_tokens"] for reviewer in reviewers)
        - cached,
        "cached_input": cached,
        "cache_write": author["cache_write_input_tokens"]
        + sum(reviewer["cache_write_input_tokens"] for reviewer in reviewers),
        "output": author["output_tokens"]
        + sum(reviewer["output_tokens"] for reviewer in reviewers),
    }


def _cost(tokens: dict[str, int], model: str) -> Decimal | None:
    rates = _RATES.get(model)
    if rates is None:
        return None
    return sum(Decimal(tokens[name]) * rate for name, rate in rates.items()) / _MILLION


def _duration(seconds: float | Decimal) -> str:
    rounded = Decimal(str(seconds)).quantize(_TIME_PRECISION)
    minutes, remainder = divmod(rounded, Decimal(60))
    return f"{int(minutes)}:{remainder:04.1f}"


def _cost_text(cost: Decimal | None) -> str:
    return "unavailable" if cost is None else f"${cost:.4f}"


def _total_cost(costs: list[Decimal | None]) -> Decimal | None:
    if any(cost is None for cost in costs):
        return None
    return sum((cost for cost in costs if cost is not None), Decimal())


def _impact_table(records: list[dict[str, Any]], model: str) -> list[str]:
    """Pair each phase's cost with room for the author's semantic summary."""
    lines = [
        "| Phase | Main effect | Changed words vs prior | Wall time | Est. cost |",
        "| --- | --- | ---: | ---: | ---: |",
    ]
    costs: list[Decimal | None] = []
    durations = []
    for record in records:
        cost = _cost(_tokens(record), model)
        costs.append(cost.quantize(_COST_PRECISION) if cost is not None else None)
        duration = Decimal(str(record["wall_seconds"])).quantize(_TIME_PRECISION)
        durations.append(duration)
        changed = record["changed_word_percent"]
        lines.append(
            f"| {_phase_label(record['phase'])} | "
            f"{'Initial artifact' if record['phase'] == 'initial' else 'TODO'} | "
            f"{'—' if changed is None else f'{changed:.1f}%'} | "
            f"{_duration(duration)} | {_cost_text(cost)} |"
        )

    total_seconds = sum(durations, Decimal())
    total_cost = _total_cost(costs)
    lines.append(
        f"| **Production total** |  | — | **{_duration(total_seconds)}** | "
        f"**{_cost_text(total_cost)}** |"
    )
    return lines


def _notes(model: str) -> list[str]:
    """State the pricing basis for the estimated production cost."""
    if model not in _RATES:
        return [f"Published rates are not configured for `{model}`."]
    return [f"Estimated cost uses published `{model}` rates for all production tokens."]


def _token_table(records: list[dict[str, Any]]) -> list[str]:
    """Show the billed token categories behind the cost estimate."""
    rows = [(_phase_label(record["phase"]), _tokens(record)) for record in records]
    total_tokens = {
        name: sum(tokens[name] for _, tokens in rows)
        for name in ("uncached_input", "cached_input", "cache_write", "output")
    }
    return [
        "| Phase | Uncached input | Cached input | Cache write | Output |",
        "| --- | ---: | ---: | ---: | ---: |",
        *(
            f"| {phase} | {tokens['uncached_input']:,} | "
            f"{tokens['cached_input']:,} | {tokens['cache_write']:,} | "
            f"{tokens['output']:,} |"
            for phase, tokens in rows
        ),
        f"| **Production total** | **{total_tokens['uncached_input']:,}** | "
        f"**{total_tokens['cached_input']:,}** | "
        f"**{total_tokens['cache_write']:,}** | "
        f"**{total_tokens['output']:,}** |",
    ]


def render(records: list[dict[str, Any]], model: str) -> str:
    """Render table-ready facts while leaving semantic impact for the author."""
    return "\n".join(
        [
            *_impact_table(records, model),
            "",
            *_token_table(records),
            "",
            *_notes(model),
            "",
        ]
    )


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="render deterministic tables from a checkpointed backtest"
    )
    parser.add_argument("run_directory", type=Path)
    args = parser.parse_args(argv)
    records = json.loads((args.run_directory / "checkpoints.json").read_text())
    metadata = json.loads((args.run_directory / "run.json").read_text())
    sys.stdout.write(render(records, metadata["model"]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
