#!/bin/bash
# NeuroSwarm Shutdown Script - Cerebral Matrix Hibernation

echo "[SYSTEM] Initiating Cerebral Matrix Hibernation..."

# Terminate all neural processes
PROCESSES="thalamus|synaptic_controller|frontal_executive|amygdala|hippocampus|motor_lobe|rem_engine|visualizer|homeostasis|metacognition|wernicke_lobe|visual_lobe"

pkill -f "$PROCESSES"

echo "[SYSTEM] Audit of remaining processes:"
ps aux | grep -E "$PROCESSES" | grep -v grep

echo "[SYSTEM] All lobes are now DORMANT. Goodnight."
