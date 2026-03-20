#!/usr/bin/env python3
"""Send initiate_sleep_cycle to trigger REM engine."""
import zmq, json, time

ctx = zmq.Context()
pub = ctx.socket(zmq.PUB)
pub.connect("tcp://localhost:5555")
time.sleep(0.5)  # let subscription propagate

msg = json.dumps({
    "origin": "test_harness",
    "intent": "initiate_sleep_cycle",
    "cid": "test_rem_finetune"
})
pub.send_string("initiate_sleep_cycle " + msg)
print("[TEST] Sent initiate_sleep_cycle")
pub.close()
ctx.term()
