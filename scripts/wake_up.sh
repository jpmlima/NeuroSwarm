#!/bin/bash
# NeuroSwarm Awakening Script - Orchestration Engine

PROJECT_ROOT="/home/xenomai/Documents/NeuroSwarm"
BUILD_PATH="$PROJECT_ROOT/build"
LOG_DIR="$PROJECT_ROOT/logs"
DATA_DIR="$PROJECT_ROOT/data/engrams"

# Ensure directory structure
mkdir -p "$LOG_DIR"
mkdir -p "$DATA_DIR"

echo "[SYSTEM] Initializing Cerebral Matrix..."

# Terminate legacy processes
pkill -f "thalamus|motor_lobe|broca_lobe|amygdala|hippocampus|frontal_executive|synaptic_controller" || true

# 1. Neural Bus (Thalamus)
echo "[SYSTEM] Activating Neural Bus (Thalamus)..."
$BUILD_PATH/thalamus > "$LOG_DIR/thalamus.log" 2>&1 &
sleep 1

# 1.5 Autonomic System (Homeostasis & MetaCognition)
echo "[SYSTEM] Initializing Homeostatic Balance & Meta-Cognition..."
$BUILD_PATH/homeostasis > "$LOG_DIR/homeostasis.log" 2>&1 &
$BUILD_PATH/metacognition > "$LOG_DIR/metacognition.log" 2>&1 &
sleep 1

# 2. Inference Engine (Synaptic Controller)
echo "[SYSTEM] Loading Gray Matter into GPU..."
$BUILD_PATH/synaptic_controller > "$LOG_DIR/synaptic.log" 2>&1 &
sleep 25 # VRAM allocation buffer

# 3. Functional Lobes
echo "[SYSTEM] Activating Functional Lobes..."
$BUILD_PATH/amygdala > "$LOG_DIR/amygdala.log" 2>&1 &
$BUILD_PATH/motor_lobe > "$LOG_DIR/motor.log" 2>&1 &
$BUILD_PATH/hippocampus > "$LOG_DIR/hippocampus.log" 2>&1 &
sleep 2

# 4. Frontal Executive
echo "[SYSTEM] Firing Frontal Executive..."
$BUILD_PATH/frontal_executive > "$LOG_DIR/executive.log" 2>&1 &

echo "[SYSTEM] CEREBRAL MATRIX ONLINE. Logs: $LOG_DIR/"
echo "[SYSTEM] Execute './build/broca_chat' to interact."
