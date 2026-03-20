import zmq
import json
import time

ctx = zmq.Context()
pub = ctx.socket(zmq.PUB)
pub.connect("tcp://localhost:5555")

time.sleep(1)  # Let it connect

req = {
    "cid": "manual_test_1",
    "origin": "broca_lobe",  # Simulate user input via broca/wernicke, wait FrontalExecutive listens to synaptic_controller "nlu_specialist"
    "intent": "inference_result",
    "adapter": "nlu_specialist",
    "text": "Write a bash script that lists all running processes sorted by memory usage.",
}

pub.send_string(json.dumps(req))
print("Sent goal to Frontal Executive.")
