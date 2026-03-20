import zmq
import json

ctx = zmq.Context()
sub = ctx.socket(zmq.SUB)
sub.connect("tcp://localhost:5556")
sub.setsockopt_string(zmq.SUBSCRIBE, "")

print("Listening to NeuroSwarm bus... (Ctrl+C to stop)")
try:
    while True:
        raw = sub.recv_string()
        print(f"RAW FRAME: {raw}")
except KeyboardInterrupt:
    pass
