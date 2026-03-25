#!/usr/bin/env python3
import time
import os
import json

LOG_FILE = "data/engrams/global_stream.jsonl"
SENTINEL_LOG = "/tmp/ns_sentinel_alert.log"

def monitor():
    print(f"[SENTINEL] Monitoring {LOG_FILE} for atomic_writer usage...")
    if not os.path.exists(LOG_FILE):
        open(LOG_FILE, 'a').close()
    
    with open(LOG_FILE, 'r') as f:
        # Ir para o fim do ficheiro
        f.seek(0, os.SEEK_END)
        
        while True:
            line = f.readline()
            if not line:
                time.sleep(1)
                continue
            
            if "atomic_writer" in line or "PROOFS.md" in line:
                try:
                    data = json.loads(line)
                    with open(SENTINEL_LOG, 'a') as alert:
                        alert.write(f"ALERT: System is using the tool! Time: {time.ctime()}\n")
                        alert.write(f"ENTRY: {line}\n")
                    print(f"[SENTINEL] Alert captured in {SENTINEL_LOG}")
                except:
                    pass

if __name__ == "__main__":
    monitor()
