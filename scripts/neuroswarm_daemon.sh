#!/bin/bash
# NeuroSwarm Daemon - Persistent Cerebral Matrix Activation

PROJECT_ROOT="/home/xenomai/Documents/NeuroSwarm"
BUILD_PATH="$PROJECT_ROOT/build"
LOG_DIR="$PROJECT_ROOT/logs"

mkdir -p "$LOG_DIR"
cd "$PROJECT_ROOT"

# Robust cleanup: Kill by exact names to avoid killing current shell/test runner
PROCESSES="thalamus synaptic_controller frontal_executive amygdala hippocampus motor_lobe rem_engine visualizer homeostasis metacognition wernicke_lobe critic_lobe auditory_lobe visual_lobe"
for p in $PROCESSES; do
    pkill -9 -x "$p" || true
done

echo "[DAEMON] Starting Neural Bus (Thalamus)..."
nohup $BUILD_PATH/thalamus > "$LOG_DIR/thalamus.log" 2>&1 &
sleep 2

echo "[DAEMON] Activating Autonomic Homeostasis..."
nohup $BUILD_PATH/homeostasis > "$LOG_DIR/homeostasis.log" 2>&1 &
nohup $BUILD_PATH/metacognition > "$LOG_DIR/metacognition.log" 2>&1 &
sleep 1

echo "[DAEMON] Starting Synaptic Controller (GPU: Qwen2.5-1.5B)..."
nohup $BUILD_PATH/synaptic_controller > "$LOG_DIR/synaptic.log" 2>&1 &
sleep 45

echo "[DAEMON] Activating Support Lobes..."
nohup $BUILD_PATH/amygdala > "$LOG_DIR/amygdala.log" 2>&1 &
nohup $BUILD_PATH/motor_lobe > "$LOG_DIR/motor.log" 2>&1 &
nohup $BUILD_PATH/hippocampus > "$LOG_DIR/hippocampus.log" 2>&1 &
nohup $BUILD_PATH/wernicke_lobe > "$LOG_DIR/wernicke.log" 2>&1 &
nohup $BUILD_PATH/critic_lobe > "$LOG_DIR/critic.log" 2>&1 &
nohup $BUILD_PATH/auditory_lobe > "$LOG_DIR/auditory.log" 2>&1 &
nohup $BUILD_PATH/visual_lobe > "$LOG_DIR/visual.log" 2>&1 &
nohup $BUILD_PATH/rem_engine > "$LOG_DIR/rem.log" 2>&1 &
nohup $BUILD_PATH/visualizer > "$LOG_DIR/visualizer.log" 2>&1 &
sleep 3

echo "[DAEMON] Firing Frontal Executive..."
nohup $BUILD_PATH/frontal_executive > "$LOG_DIR/executive.log" 2>&1 &
sleep 2

echo "[DAEMON] Final Process Audit:"
ps aux | grep -E "thalamus|synaptic_controller|frontal_executive|amygdala|hippocampus|motor_lobe|rem_engine|visualizer|critic_lobe|auditory_lobe|visual_lobe" | grep -v grep

echo "[DAEMON] Matrix is PERSISTENT. Logs in $LOG_DIR"
echo "[DAEMON] Use './build/broca_chat' to talk."
