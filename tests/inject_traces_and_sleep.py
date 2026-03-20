#!/usr/bin/env python3
"""Inject synthetic successful execution traces into global_stream to test
REM training data export. Then trigger a sleep cycle."""
import zmq, json, time, uuid

STREAM = "./data/engrams/global_stream.jsonl"

# Generate 25 synthetic successful reality traces with matching inference_requests
traces = []
commands = [
    "ls -la /home/xenomai/Documents/NeuroSwarm/",
    "cat README.md | head -20",
    "grep -r 'ModelManager' include/",
    "wc -l src/inference/*.cpp",
    "find . -name '*.hpp' | head -10",
    "cmake --build build -j4 2>&1 | tail -5",
    "git log --oneline -5",
    "ps aux | grep thalamus",
    "df -h /home",
    "free -m",
    "uname -a",
    "date +%Y-%m-%d",
    "echo hello world",
    "cat /proc/cpuinfo | head -5",
    "ip addr show | head -10",
    "du -sh build/",
    "file build/synaptic_controller",
    "ldd build/thalamus | head -5",
    "stat models/Phi-4-mini-instruct-Q4_K_M.gguf",
    "head -1 data/system_knowledge.md",
    "tail -3 logs/neuroswarm.log",
    "env | grep HOME",
    "whoami",
    "pwd",
    "uptime",
]

ts = int(time.time())
with open(STREAM, "a") as f:
    for i, cmd in enumerate(commands):
        cid = f"synth_test_{ts}_{i}"
        # inference_request with GOAL
        req = {
            "cid": cid,
            "origin": "frontal_executive",
            "intent": "inference_request",
            "text": f"GOAL: {cmd}\nExecute this command.",
            "adapter": "coder"
        }
        f.write(json.dumps(req) + "\n")
        # execution_request
        ereq = {
            "cid": cid,
            "origin": "motor_cortex",
            "intent": "execution_request",
            "command": cmd,
            "mode": "reality"
        }
        f.write(json.dumps(ereq) + "\n")
        # execution_result — success
        eres = {
            "cid": cid,
            "origin": "motor_cortex",
            "intent": "execution_result",
            "command": cmd,
            "mode": "reality",
            "status": "success",
            "exit_code": 0,
            "proprioception": "OK",
            "synapse_ts": ts + i
        }
        f.write(json.dumps(eres) + "\n")

print(f"[TEST] Injected {len(commands)} synthetic success traces into global_stream.jsonl")

# Now trigger sleep cycle
ctx = zmq.Context()
pub = ctx.socket(zmq.PUB)
pub.connect("tcp://localhost:5555")
time.sleep(0.5)

msg = json.dumps({
    "origin": "test_harness",
    "intent": "initiate_sleep_cycle",
    "cid": "test_rem_export"
})
pub.send_string("initiate_sleep_cycle " + msg)
print("[TEST] Sent initiate_sleep_cycle — waiting for REM to process...")
pub.close()
ctx.term()
