#!/bin/bash
PROJECT_ROOT="/home/xenomai/Documents/NeuroSwarm"
LOG_FILE="$PROJECT_ROOT/logs/neuroswarm.log"

cd $PROJECT_ROOT
mkdir -p logs

# Instant Cleanup
pkill -9 -f "CerebralMatrix|thalamus|synaptic|motor|executive|broca|visual|amygdala|wernicke|homeostasis|metacognition|critic|visualizer"
fuser -k 5555/tcp 5556/tcp 8080/tcp 2>/dev/null || true

rm -f $LOG_FILE
touch $LOG_FILE

echo "[LAUNCHER] Instant Awakening Triggered."
ulimit -s unlimited
nohup ./build/CerebralMatrix --awaken > $LOG_FILE 2>&1 &

echo "[SUCCESS] All processes dispatched. Check 'tail -f logs/neuroswarm.log' for model loading progress."
