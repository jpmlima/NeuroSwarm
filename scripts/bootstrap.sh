#!/bin/bash
set -e
PROJECT_ROOT="/home/xenomai/Documents/NeuroSwarm"
cd ""

export PATH=/home/xenomai/.opencode/bin:/home/xenomai/.local/share/mise/installs/node/25.2.1/bin:/home/xenomai/.local/share/omarchy/bin:/home/xenomai/.local/share/mise/shims:/home/xenomai/.local/share/omarchy/bin:/usr/local/sbin:/usr/local/bin:/usr/bin:/opt/cuda/bin:/usr/lib/jvm/default/bin:/usr/bin/site_perl:/usr/bin/vendor_perl:/usr/bin/core_perl:/home/xenomai/.local/bin:/opt/cuda/bin

echo "[BOOTSTRAP] Cleaning previous failed build..."
rm -rf external/llama.cpp/build

echo "[BOOTSTRAP] Setting up llama.cpp synaptic backend for 1080Ti..."
cd external/llama.cpp
mkdir -p build && cd build

# Force CUDA architecture for Pascal (1080Ti is sm_61)
# We try to bypass the 13.1 restriction by setting common flags
cmake .. -DBUILD_SHARED_LIBS=ON -DGGML_CUDA=ON -DCMAKE_CUDA_ARCHITECTURES=61

make -j24
cd ""

echo "[BOOTSTRAP] Final compilation of NeuroSwarm..."
mkdir -p build && cd build
cmake ..
make -j24
