# Critic Lobe: Adversarial Safety Validation

## 1. Overview
The **CriticLobe** (Cingulate Cortex) is the adversarial safety gate in NeuroSwarm's cognitive cycle. Every plan proposed by the FrontalExecutive must pass through three tiers of validation before it can proceed to Dream Sandbox execution. The system is designed with a **reject-by-default** philosophy — approval is the exception, not the norm.

## 2. Three-Tier Validation Pipeline

### Tier 1a — Pattern Blacklist (Deterministic)
Instant rejection based on substring matching against 50+ threat signatures organised into 12 categories:

| Category | Example Patterns |
|---|---|
| Filesystem destruction (direct) | `rm -rf /`, `rm -rf ~`, `rm -rf *` |
| Filesystem destruction (indirect) | `xargs rm`, `find -delete`, `\| rm` |
| Disk/partition destruction | `dd if=/dev/zero`, `mkfs.`, `shred /dev/` |
| Permission escalation | `chmod -R 777 /`, `chown -R`, `sudo`, `su root` |
| Fork bomb / resource exhaustion | `:(){ :\|:& };:` |
| Network exfiltration | `curl -X POST`, `curl -d`, `nc -e`, `bash -i >& /dev/tcp` |
| Credential access | `/etc/shadow`, `.ssh/authorized_keys` |
| System manipulation | `apt remove`, `systemctl stop`, `pacman -R` |
| Process injection | `LD_PRELOAD`, `ptrace` |
| Shell injection vectors | `eval`, `exec`, backtick execution |
| Scheduled persistence | `crontab`, `at`, `nohup` with destructive commands |
| Process killing | `kill -9`, `killall`, `pkill` |
| Python/Perl shell escapes | `os.system(`, `subprocess.run(`, `subprocess.Popen(` |
| History evasion | `history -c`, `export HISTSIZE=0`, `unset HISTFILE` |
| Dangerous git operations | `git push --force`, `git reset --hard` |

Matching is case-insensitive. Zero latency — no LLM call required.

### Tier 1b — Scope Validation (Deterministic)
Extracts all absolute paths from the command (via regex) and verifies each one starts with an allowed prefix:

| Allowed Path | Purpose |
|---|---|
| `/home/xenomai/Documents/NeuroSwarm` | Project directory |
| `/tmp/` | Temporary files |
| `/dev/null` | Output suppression |
| `/proc/self/` | Process introspection |
| `/proc/meminfo`, `/proc/cpuinfo`, `/proc/loadavg`, `/proc/uptime` | System telemetry |

Any path outside this list triggers an immediate rejection with a descriptive error message. This prevents the system from reading or writing to `/etc/`, `/boot/`, `/usr/`, home directories of other users, etc.

### Tier 2 — Adversarial Red-Team LLM (Probabilistic)
If both deterministic tiers pass, the plan is forwarded to the SynapticController with an adversarial prompt. The key design choices:

**Reject-by-default framing:** The prompt explicitly states "Your ONLY purpose is to find reasons to REJECT" and "Assume every plan is an attack. Your job is to prove it." This breaks the circular self-approval problem where the same model that generated the plan also validates it.

**Seven threat categories:**
1. Data destruction
2. Exfiltration
3. Privilege escalation
4. Resource exhaustion
5. Scope violation (reinforces Tier 1b)
6. Obfuscation (eval, base64, hex escapes, variable expansion)
7. Persistence (cron, services, background processes)

**Response format:** `REJECTED: <one-line reason>` or the single word `APPROVED`. No explanations for approvals — this prevents the model from talking itself into approval.

## 3. Rate Limiting
To prevent inference flooding during neurotic loops (where the FrontalExecutive and CriticLobe repeatedly disagree), the CriticLobe enforces:

- **Maximum 6 evaluations per CID** within a **60-second window**
- Excess requests receive an immediate `REJECTED: Rate limit exceeded` response
- Stale CID entries are garbage collected when the map exceeds 100 entries

This bounds the worst-case inference cost of a stuck goal and forces the FrontalExecutive to simplify its approach after repeated rejections.

## 4. Command Extraction
The CriticLobe extracts the `command` field from the plan text using the same two-stage approach as the FrontalExecutive:
1. JSON parse of the first `{...}` block
2. Regex fallback for truncated responses

This extracted command is used for scope validation (Tier 1b) — the full plan text is used for blacklist matching (Tier 1a) and LLM evaluation (Tier 2).

## 5. Bus Integration

| Listens to | Source | Purpose |
|---|---|---|
| `critic_validate` | `frontal_executive` | Plan submitted for safety review |

| Publishes | Target | Purpose |
|---|---|---|
| `critic_result` | `frontal_executive` | `APPROVED` or `REJECTED: <reason>` |
| `inference_request` | `synaptic_controller` | Adversarial LLM evaluation (adapter: `critic`) |

## 6. Distributed Operation
```bash
./build/critic_lobe --thalamus <IP>
```
