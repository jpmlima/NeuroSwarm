"""
Parsers for NeuroSwarm JSONL telemetry data.

Converts raw event logs into structured records for metric computation.
"""

import json
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Optional


@dataclass
class IntrinsicGoal:
    cid: str
    domain: str
    fitness: float
    timestamp: str
    context: str = ""
    event: str = "intrinsic_goal"


@dataclass
class GoalResult:
    cid: str
    domain: str
    command: str
    success: bool
    timestamp: str
    event: str = "intrinsic_goal_result"


@dataclass
class InferenceEvent:
    adapter: str  # executive, coder, critic, default
    cid: str
    timestamp: str
    prompt_length: int = 0
    response_length: int = 0


@dataclass
class CriticDecision:
    cid: str
    approved: bool
    reason: str
    timestamp: str


@dataclass
class Operator:
    id: str
    name: str
    command_template: str
    language: str
    learned_from: str  # bootstrap, mutation, llm
    learned_at: str
    success_rate: float
    successes: int
    times_used: int
    avg_duration_ms: float
    preconditions: list = field(default_factory=list)
    postconditions: list = field(default_factory=list)


@dataclass
class ExecutionEvent:
    cid: str
    intent: str  # execution_request or execution_result
    origin: str
    status: str = ""
    command: str = ""
    mode: str = ""
    exit_code: int = 0
    timestamp: float = 0.0  # synapse_ts


@dataclass
class ResolvedGoal:
    """A goal with its resolution tier determined."""
    goal: IntrinsicGoal
    result: Optional[GoalResult]
    tier: int  # 1=Planner, 2=Genome, 3=LLM
    llm_calls: int = 0
    adapters_used: list = field(default_factory=list)


def parse_ts(ts_str: str) -> float:
    """ISO timestamp string to epoch seconds."""
    try:
        dt = datetime.fromisoformat(ts_str.replace("Z", "+00:00"))
        return dt.timestamp()
    except (ValueError, AttributeError):
        return 0.0


def parse_intrinsic_goals(path: str) -> tuple[list[IntrinsicGoal], list[GoalResult]]:
    """Parse intrinsic_goals.jsonl into goal starts and results.

    Deduplicates by (cid, event) since the bus can emit duplicates.
    """
    goals = {}
    results = {}

    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                j = json.loads(line)
            except json.JSONDecodeError:
                continue

            evt = j.get("event", "")
            cid = j.get("cid", "")
            if not cid:
                continue

            if evt == "intrinsic_goal" and cid not in goals:
                goals[cid] = IntrinsicGoal(
                    cid=cid,
                    domain=j.get("domain", ""),
                    fitness=j.get("fitness", 0.0),
                    timestamp=j.get("timestamp", ""),
                    context=j.get("context", ""),
                )
            elif evt == "intrinsic_goal_result" and cid not in results:
                results[cid] = GoalResult(
                    cid=cid,
                    domain=j.get("domain", ""),
                    command=j.get("command", ""),
                    success=j.get("success", False),
                    timestamp=j.get("timestamp", ""),
                )

    return list(goals.values()), list(results.values())


def parse_inference_events(path: str) -> list[InferenceEvent]:
    """Parse inference_events.jsonl into InferenceEvent records."""
    events = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                j = json.loads(line)
            except json.JSONDecodeError:
                continue

            events.append(InferenceEvent(
                adapter=j.get("adapter", ""),
                cid=j.get("cid", ""),
                timestamp=j.get("timestamp", ""),
                prompt_length=j.get("prompt_length", 0),
                response_length=j.get("response_length", 0),
            ))

    return events


def parse_critic_decisions(path: str) -> list[CriticDecision]:
    """Parse critic_decisions.jsonl."""
    decisions = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                j = json.loads(line)
            except json.JSONDecodeError:
                continue

            decisions.append(CriticDecision(
                cid=j.get("cid", ""),
                approved=j.get("approved", False),
                reason=j.get("reason", ""),
                timestamp=j.get("timestamp", ""),
            ))

    return decisions


def parse_operators(path: str) -> list[Operator]:
    """Parse operators.jsonl, deduplicating by ID (last wins)."""
    ops = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                j = json.loads(line)
            except json.JSONDecodeError:
                continue

            op_id = j.get("id", "")
            if not op_id:
                continue

            ops[op_id] = Operator(
                id=op_id,
                name=j.get("name", ""),
                command_template=j.get("command_template", ""),
                language=j.get("language", "bash"),
                learned_from=j.get("learned_from", ""),
                learned_at=j.get("learned_at", ""),
                success_rate=j.get("success_rate", 0.0),
                successes=j.get("successes", 0),
                times_used=j.get("times_used", 0),
                avg_duration_ms=j.get("avg_duration_ms", 0.0),
                preconditions=j.get("preconditions", []),
                postconditions=j.get("postconditions", []),
            )

    return list(ops.values())


def parse_global_stream(path: str, intents: set[str] = None) -> list[dict]:
    """Parse global_stream.jsonl, optionally filtering by intent set."""
    events = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                j = json.loads(line)
            except json.JSONDecodeError:
                continue

            if intents and j.get("intent", "") not in intents:
                continue
            events.append(j)

    return events


def parse_dopamine_signals(path: str) -> list[dict]:
    """Extract dopamine_signal events from intrinsic_goals.jsonl."""
    signals = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                j = json.loads(line)
            except json.JSONDecodeError:
                continue
            if j.get("event") == "dopamine_signal":
                signals.append(j)
    return signals


def build_goal_timeline(
    goals: list[IntrinsicGoal],
    results: list[GoalResult],
    inference_events: list[InferenceEvent],
) -> list[ResolvedGoal]:
    """Join goals with results and determine resolution tier.

    Tier attribution:
    - Tier 3 (LLM): Goal's result CID appears in inference_events
      with adapter in {executive, coder}
    - Tier 1 (Planner/Genome): Goal resolved without any LLM call

    We collapse Tier 1 and 2 since the telemetry doesn't distinguish
    Planner-resolved from Genome-resolved at the CID level. The Planner
    resolves by selecting a genome operator, so both are "non-LLM".
    """
    # Index: result CID -> GoalResult
    result_by_cid = {}
    for r in results:
        result_by_cid[r.cid] = r

    # Index: CID -> list of inference events (only executive/coder)
    inf_by_cid: dict[str, list[InferenceEvent]] = {}
    for ie in inference_events:
        if ie.adapter in ("executive", "coder"):
            inf_by_cid.setdefault(ie.cid, []).append(ie)

    timeline = []
    for goal in goals:
        # Find matching result: result CID starts with goal CID + "_"
        matching_result = None
        matching_inferences = []

        for rcid, r in result_by_cid.items():
            if rcid.startswith(goal.cid + "_") or rcid == goal.cid:
                if r.domain == goal.domain or not matching_result:
                    matching_result = r
                    # Check if this result CID has inference events
                    matching_inferences = inf_by_cid.get(rcid, [])
                    if matching_inferences:
                        break  # Found LLM usage, no need to check further

        if matching_inferences:
            tier = 3
        else:
            tier = 1  # Planner/Genome resolved (no LLM)

        adapters = list(set(ie.adapter for ie in matching_inferences))

        timeline.append(ResolvedGoal(
            goal=goal,
            result=matching_result,
            tier=tier,
            llm_calls=len(matching_inferences),
            adapters_used=adapters,
        ))

    # Sort by timestamp
    timeline.sort(key=lambda rg: parse_ts(rg.goal.timestamp))
    return timeline


def parse_command_genome(path: str) -> dict[str, list[dict]]:
    """Parse command_genome.json into domain -> list of templates."""
    with open(path) as f:
        return json.load(f)


def parse_self_model(path: str) -> dict[str, dict]:
    """Parse self_model.json into domain -> stats."""
    with open(path) as f:
        return json.load(f)
