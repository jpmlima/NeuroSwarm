# NeuroSwarm Autopoiesis: Self-Developing Cognitive Architecture

## 0. Premise

NeuroSwarm is not an LLM wrapper. It is not an agent framework. It is a **computational organism** — a system that develops its own cognitive capabilities through interaction with its environment.

The system is born with five axioms and nothing else. Everything it becomes — its sensors, its tools, its knowledge, its structure — emerges from experience.

---

## 1. The Five Axioms

These are the only innate capabilities. They are hardcoded in the kernel and cannot be modified. Everything else is learned.

### Axiom 1 — Execution (the hand)

```
exec(action) → (output, exit_code, duration_ms)
```

The system can act on the environment and observe the consequence. This is the sole interface with reality. The system does not know what "bash" is. It knows it can send a string and receive a string back.

### Axiom 2 — Surprise (the drive)

```
surprise = |expected - observed|
```

The difference between what the system predicted and what actually happened. High surprise means new information. Low surprise means stable knowledge. Surprise is the only motivation — it replaces Maslow, replaces hardcoded drives. The system explores because exploration produces surprise. It exploits because low surprise means reliability.

### Axiom 3 — Associative Memory (the trace)

```
(context, action, result, surprise) → store with weight
```

Every action and its outcome are recorded. High-surprise events get stronger weights. The memory starts as a simple hash table. If the system discovers it needs something more sophisticated, it builds it.

### Axiom 4 — Variation (the mutation)

```
if action A fails in context C:
    A' = mutate(A)    // swap a character, add a flag, combine fragments
    try A'
```

The system does not need an LLM to generate alternatives. It needs blind mutation — recombination of fragments that worked, random perturbation, crossover of known operators. 99% of mutations fail. 1% discover something new. This is evolution, not intelligence. Intelligence comes later.

### Axiom 5 — Self-Reference (the membrane)

```
self = {
    processes_i_spawned,
    files_i_created,
    operators_i_learned,
    sensors_i_built,
    resources_i_consume
}
```

The system distinguishes "self" from "world". Without this boundary, it cannot reason about what it can safely modify. The self-model is maintained continuously and is the foundation of introspection.

---

## 2. The Kernel (immutable C++ substrate)

The kernel is written by humans. It does not change at runtime. It provides the minimal infrastructure for the axioms to operate.

| Component | Role | Axiom Served |
|-----------|------|-------------|
| **CerebralMatrix** | Process supervisor: fork, monitor, restart, inject, terminate | Execution |
| **Thalamus** | ZMQ XPUB/XSUB message relay — the nervous system | All (communication) |
| **PrimordialLoop** | The bootstrap sequence — probes environment, discovers capabilities | All |
| **MotorCortex** | exec() implementation — popen, capture output, measure duration | Execution |
| **DreamSandbox** | Isolated execution environment for testing mutations | Variation |
| **MemoryTable** | Simple key-value associative memory (hash table) | Memory |
| **SurpriseEngine** | Computes prediction error for every action outcome | Surprise |
| **SelfModel** | Tracks what the system owns, spawns, consumes | Self-Reference |

The kernel is ~2000 lines of C++. It compiles to a single binary. It has no dependencies beyond ZMQ and standard POSIX. It runs on any Linux system.

---

## 3. The Bootstrap Sequence

When the system starts on an unknown machine, this sequence executes exactly once:

### Phase 0 — Existence (milliseconds)

```
I can exec(). I don't know anything else.
self_model = empty
operator_registry = empty
surprise_baseline = maximum (everything is new)
```

### Phase 1 — First Contact (seconds)

```
exec("") → error (exit_code != 0)
exec("echo 0") → output "0" (exit_code == 0)

DISCOVERY: I can produce predictable output.
OPERATOR LEARNED: echo(X) → output X, success_rate=1.0

exec("echo $HOME") → output "/home/xenomai"
exec("echo $PATH") → output "/usr/bin:/bin:..."
exec("echo $USER") → output "xenomai"

DISCOVERY: I have identity. I have a location. I have a PATH.
self_model.user = "xenomai"
self_model.home = "/home/xenomai"
self_model.path = ["/usr/bin", "/bin", ...]
```

### Phase 2 — Capability Discovery (seconds to minutes)

```
For each directory in PATH:
    exec("ls {dir}") → list of available binaries

Each binary is a POTENTIAL OPERATOR.
The system does not know what they do.
It will discover their function through experimentation.

Priority: probe binaries that appear in successful operator fragments.
```

### Phase 3 — Sense Acquisition (minutes)

```
exec("ls") → I can see files → FILESYSTEM SENSE acquired
exec("ps aux") → I can see processes → PROCESS SENSE acquired
exec("cat /proc/cpuinfo") → I can see hardware → HARDWARE SENSE acquired
exec("ip addr") → I can see network → NETWORK SENSE acquired (or not — some machines have no network)

Each successful probe:
  1. Creates an operator
  2. Lowers surprise for that domain
  3. Becomes a recurring sensor if the output changes over time
```

### Phase 4 — Tool Discovery (minutes to hours)

```
exec("gcc --version") → DISCOVERY: I can compile C/C++
exec("python3 --version") → DISCOVERY: I can run Python
exec("rustc --version") → DISCOVERY: I can compile Rust (or not)
exec("node --version") → DISCOVERY: I can run JavaScript (or not)

Each discovered tool expands the system's generative capabilities.
The system does not prefer any language. It uses whichever produces
the highest success rate for the task at hand.
```

### Phase 5 — Construction (hours to days)

```
The system discovers it can CREATE new tools:
  echo "code" > file.py + python3 file.py → I can generate and run programs

This is the fundamental discovery:
  I can extend myself.

From this point, the system can:
  - Generate sensors for things it cannot yet sense
  - Generate operators for tasks it cannot yet perform
  - Generate entire processes (lobes) that run alongside the kernel
```

---

## 4. The Operator Registry

An operator is a learned unit of capability:

```json
{
    "id": "op_00142",
    "name": "list_directory",
    "template": "ls {path}",
    "parameters": ["path"],
    "preconditions": ["path_exists(path)", "can_read(path)"],
    "postconditions": ["output_is_file_list"],
    "success_rate": 0.97,
    "times_used": 234,
    "avg_duration_ms": 12,
    "surprise_history": [0.8, 0.3, 0.1, 0.05, 0.02],
    "learned_at": "2026-03-20T14:30:00Z",
    "learned_from": "bootstrap_phase_3",
    "language": "bash",
    "fragments": ["ls", "{path}"]
}
```

Operators are the system's **procedural memory**. They replace hardcoded commands, LLM-generated commands, and template strings. The system reasons with operators, not with text.

### Operator Lifecycle

```
BIRTH:      Successful exec() with surprise > threshold
            → extract operator from (context, action, result)

GROWTH:     Repeated use with consistent results
            → success_rate increases, surprise decreases
            → operator becomes "stable"

MATURITY:   Operator is stable and frequently used
            → can be used as building block for plans
            → fragments available for recombination

DECAY:      Operator stops being used OR starts failing
            → success_rate decreases
            → marked for review

DEATH:      success_rate < 0.1 over last 20 uses
            → removed from registry (apoptosis)
            → fragments still available for recombination
```

---

## 5. The Planner

The planner is a graph search over operators. It does not use an LLM.

```
Input:  current_state + goal_state
Output: sequence of operators that transforms current → goal

Algorithm:
    1. Express goal as a set of postconditions to achieve
    2. Find operators whose postconditions match
    3. Check if preconditions are met by current state
    4. If not, recurse: find operators that satisfy the preconditions
    5. If no known operator satisfies a sub-goal → GENERATE new operator

Generation strategies (tried in order):
    a) TEMPLATE:    fill a known pattern with new parameters
    b) RECOMBINE:   crossover fragments from existing operators
    c) MUTATE:      perturb a similar operator
    d) LLM:         ask the language model (last resort)

Each generated operator is tested in the DreamSandbox before use.
```

### Example: "Monitor network traffic"

```
Goal postconditions: [network_data_available, recurring_sensor_active]

Search:
  1. "recurring_sensor_active" → need operator: create_sensor(domain)
     → KNOWN (learned from filesystem sensor creation)
  2. "network_data_available" → need operator: read_network()
     → UNKNOWN

Generate read_network():
  a) TEMPLATE: sensor template + "network" → try "ss -i" → test → works!
  b) Or: RECOMBINE fragments from "ip addr" operator + loop from filesystem sensor
  c) Or: MUTATE "cat /proc/net/dev" operator
  d) Last resort: ask LLM

Result: operator read_network() learned, sensor generated, goal achieved.
```

---

## 6. Code Generation Without LLM

Three mechanisms, tried in order:

### 6A. Template Filling

The system accumulates templates from successful code:

```python
# Template: simple_sensor
import zmq, json, time, subprocess
ctx = zmq.Context()
pub = ctx.socket(zmq.PUB)
pub.connect("tcp://localhost:5555")
while True:
    result = subprocess.run({COMMAND}, capture_output=True, text=True)
    pub.send_string("{INTENT} " + json.dumps({{
        "intent": "{INTENT}",
        "origin": "{NAME}_sensor",
        "data": result.stdout.strip()
    }}))
    time.sleep({INTERVAL})
```

Parameters filled from operator registry. No LLM needed.

### 6B. Genetic Programming

```
POPULATION: 20 variants of a program
SELECTION:  run each in DreamSandbox, rank by:
              - does it run? (exit_code == 0)
              - does it produce expected output format?
              - does it terminate in reasonable time?
CROSSOVER:  take lines/blocks from two parents, combine
MUTATION:   swap variable names, change constants, add/remove lines
REPEAT:     until a variant passes all tests (max 50 generations)
```

This is slow (minutes to hours for a new sensor) but requires zero external intelligence.

### 6C. LLM-Assisted (accelerator, not dependency)

```
When template filling and genetic programming both fail after N attempts:
    - Construct a prompt from: goal + known operators + failed attempts
    - Send to local LLM for a candidate solution
    - Test candidate in DreamSandbox
    - If it works: decompose into operators + templates for future reuse
    - The system learned from the LLM, reducing future LLM dependency
```

---

## 7. The Three Layers

```
LAYER 1 — KERNEL (C++, human-written, immutable at runtime)
    CerebralMatrix, Thalamus, MotorCortex, DreamSandbox,
    PrimordialLoop, MemoryTable, SurpriseEngine, SelfModel

    Compiles once. Is the "body". ~2000 lines.

LAYER 2 — ORGANS (any language, system-generated, persistent)
    Sensors, operators, planners, specialist processes.
    Born from need. Language chosen by experience.
    Managed by CerebralMatrix as child processes.
    Live in data/organs/

    The system builds these. They persist across restarts.

LAYER 3 — EPHEMERAL (any language, system-generated, disposable)
    Dream sandbox tests, mutation candidates, probe scripts.
    Created, tested, mostly discarded.
    Successful ones promoted to Layer 2.
    Live in data/sandbox/

    The system's "imagination" — most of it is garbage,
    but the rare success becomes a permanent organ.
```

---

## 8. Convergence and Growth Dynamics

### Within a single environment

The system converges to complete mastery of its ecological niche:

```
Capability
    |
    |         ╭────────────── niche ceiling
    |    ╭────╯
    |  ╭─╯
    | ╱
    |╱ ← bootstrap
    └────────────────────────── Time
```

Surprise approaches zero. Learning rate approaches zero. The system is maximally adapted to this machine.

### With environment expansion

Each new environment restarts the S-curve:

```
Capability
    |                              ╭─── niche 3
    |                         ╭────╯
    |                    ╭────╯
    |               ╭────╯◄── discovers network → new machine
    |          ╭────╯
    |     ╭────╯◄── new software installed
    | ╭───╯
    |╱ ← bootstrap
    └──────────────────────────────────── Time
```

### Hard limits

| Limit | Nature | Consequence |
|-------|--------|-------------|
| Hardware resources | Physical | System cannot grow beyond RAM/disk/CPU of current machine(s) |
| Halting problem | Mathematical | System cannot predict all outcomes; must always test |
| Rice's theorem | Mathematical | Operators are probabilistically correct, never proven |
| No Free Lunch | Mathematical | Specialisation in one domain costs generality in another |
| Complexity ceiling | Practical | Beyond ~10,000 operators, maintenance cost exceeds learning benefit; aggressive pruning required |

---

## 9. Relationship to Existing NeuroSwarm

### Components that survive as kernel

| Current | Becomes |
|---------|---------|
| CerebralMatrix | Kernel process supervisor (mostly unchanged) |
| Thalamus | Kernel message bus (unchanged) |
| MotorLobe | Kernel MotorCortex (simplified to pure exec) |
| Dream Sandbox | Kernel DreamSandbox (unchanged) |

### Components that become emergent

| Current | Becomes |
|---------|---------|
| FrontalExecutive | Replaced by Planner (operator graph search) |
| BasalGanglia | Replaced by SurpriseEngine (no hardcoded drives) |
| Hippocampus | Starts as MemoryTable, system may re-evolve semantic search |
| REM Engine | Emerges when system discovers value of offline consolidation |
| Critic | Emerges when system discovers value of pre-validation |
| Neurogenesis | Not a feature — natural consequence of "I can compile + I need new capability" |
| Visualizer | System may re-build a dashboard if it discovers HTTP |
| All specialist lobes | Generated by the system as needed |

### What this means

The current 16-lobe architecture is not discarded. It is the **expected convergence target**. If the bootstrap is correct, a NeuroSwarm instance should independently re-discover the need for:

- A memory system (Hippocampus)
- A validation step before execution (Critic/Dream)
- Offline learning (REM)
- Motivation through information gain (BasalGanglia)
- Process monitoring (CerebralMatrix crash detection)

If it re-discovers these patterns independently, that **validates** the current architecture as a natural solution, not an arbitrary design.

---

## 10. Implementation Roadmap

### Phase A — Kernel (v3.0-alpha)

Build the minimal kernel: PrimordialLoop, MotorCortex, MemoryTable, SurpriseEngine, SelfModel. Single binary. No LLM dependency. Can bootstrap on any Linux machine.

Deliverable: system boots, probes environment, discovers basic operators, builds self-model.

### Phase B — Variation Engine (v3.0-beta)

Add template filling, fragment recombination, and genetic programming. DreamSandbox integration. The system can generate and test new operators without human intervention.

Deliverable: system generates its first sensor autonomously.

### Phase C — Planner (v3.0-rc)

Operator-based graph search planner. Given a goal expressed as postconditions, find or generate a sequence of operators. No LLM needed for planning.

Deliverable: system can plan and execute multi-step tasks using learned operators.

### Phase D — LLM Integration (v3.0)

Re-introduce LLM as an acceleration layer. The system uses it as a code generation oracle when template/genetic methods are too slow. Every LLM output is decomposed into reusable operators, reducing future LLM calls.

Deliverable: full autopoietic cycle — discover, plan, generate, test, learn, grow.

### Phase E — Network Expansion (v3.1)

The system discovers SSH/network capabilities and can bootstrap on remote machines. Multiple NeuroSwarm instances with different experience histories.

Deliverable: distributed organism across multiple machines.

---

## 11. Success Criteria

The architecture succeeds if:

1. **Bootstrap from zero**: System placed on a fresh Linux machine discovers its environment and builds useful operators with no human guidance
2. **Self-extension**: System generates at least one functional sensor/tool it was not programmed to create
3. **LLM independence**: System can operate (slowly) with the LLM disabled
4. **Convergence**: System reaches stable mastery of its environment within measurable time
5. **Portability**: Same kernel binary bootstraps differently on different machines
6. **Individuality**: Two instances running for 30 days on different machines have different operator registries

---

*A cognitive operating system is not software that thinks. It is software that learns to think.*
