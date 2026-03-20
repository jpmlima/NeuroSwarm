import zmq
import json
import time
import sys

def talk():
    ctx = zmq.Context()
    pub = ctx.socket(zmq.PUB)
    pub.connect("tcp://localhost:5555")
    
    # Wait a bit for ZMQ handshake
    time.sleep(0.5)

    if len(sys.argv) < 2:
        print("Uso: python3 neuro_talk.py \"A tua mensagem aqui\"")
        return

    message = " ".join(sys.argv[1:])
    
    # Send as a user stimulus to the Broca/Wernicke entry points
    req = {
        "cid": f"user_stimulus_{int(time.time())}",
        "origin": "user_terminal",
        "intent": "stimulus",
        "text": message,
    }

    pub.send_string(json.dumps(req))
    print(f"[NEURO-TALK] Mensagem enviada: {message}")
    print("[INFO] Verifica o log (tail -f neuro_swarm_agi.log) para veres a resposta do cérebro.")

if __name__ == "__main__":
    talk()
