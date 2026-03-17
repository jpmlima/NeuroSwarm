cmake_minimum_required(VERSION 3.15)
project(NeuroSwarm VERSION 1.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

find_package(cppzmq REQUIRED)
find_package(nlohmann_json REQUIRED)

include_directories(include 
    external/llama.cpp/include 
    external/llama.cpp/ggml/include
    external/httplib)

link_directories("/home/xenomai/Documents/NeuroSwarm/external/llama.cpp/build/bin")

# Base system libs
set(SYSTEM_LIBS zmq pthread atomic dl vulkan llama ggml ggml-base ggml-cpu ggml-vulkan)

# 1. Synaptic Controller (Core)
add_executable(synaptic_controller 
               src/inference/ModelManager.cpp 
               src/inference/SynapticController_Main.cpp)
target_link_libraries(synaptic_controller PRIVATE cppzmq nlohmann_json::nlohmann_json ${SYSTEM_LIBS})

# 2. Thalamus (Neural Bus)
add_executable(thalamus src/brainstem/Thalamus.cpp)
target_link_libraries(thalamus PRIVATE zmq nlohmann_json::nlohmann_json)

add_executable(homeostasis src/brainstem/Homeostasis.cpp)
target_link_libraries(homeostasis PRIVATE zmq nlohmann_json::nlohmann_json)

add_executable(metacognition src/brainstem/MetaCognition.cpp)
target_link_libraries(metacognition PRIVATE zmq nlohmann_json::nlohmann_json)

# 3. Frontal Executive (Autonomy)
add_executable(frontal_executive src/brainstem/FrontalExecutive.cpp)
target_link_libraries(frontal_executive PRIVATE zmq nlohmann_json::nlohmann_json)

# 4. Functional Lobes
add_executable(motor_lobe src/lobes/MotorLobe/MotorLobe.cpp)
target_link_libraries(motor_lobe PRIVATE zmq nlohmann_json::nlohmann_json)

add_executable(amygdala src/lobes/Amygdala/AmygdalaLobe.cpp)
target_link_libraries(amygdala PRIVATE zmq nlohmann_json::nlohmann_json)

add_executable(hippocampus src/lobes/Hippocampus/HippocampusLobe.cpp)
target_link_libraries(hippocampus PRIVATE zmq nlohmann_json::nlohmann_json)

add_executable(broca_chat src/lobes/BrocaLobe/BrocaChat.cpp)
target_link_libraries(broca_chat PRIVATE zmq nlohmann_json::nlohmann_json)

add_executable(wernicke_lobe src/lobes/WernickeLobe/WernickeLobe.cpp)
target_link_libraries(wernicke_lobe PRIVATE zmq nlohmann_json::nlohmann_json)

add_executable(critic_lobe src/lobes/CingulateLobe/CriticLobe.cpp)
target_link_libraries(critic_lobe PRIVATE zmq nlohmann_json::nlohmann_json)

add_executable(visual_lobe src/lobes/VisualLobe/VisualLobe.cpp)
target_link_libraries(visual_lobe PRIVATE zmq nlohmann_json::nlohmann_json)

add_executable(auditory_lobe src/lobes/AuditoryLobe/AuditoryLobe.cpp)
target_link_libraries(auditory_lobe PRIVATE zmq nlohmann_json::nlohmann_json)

add_executable(visualizer src/lobes/Visualizer/VisualizerLobe.cpp)
target_link_libraries(visualizer PRIVATE zmq nlohmann_json::nlohmann_json pthread)

add_executable(rem_engine src/inference/REMEngine.cpp)
target_link_libraries(rem_engine PRIVATE zmq nlohmann_json::nlohmann_json)

add_executable(CerebralMatrix src/brainstem/CerebralMatrix.cpp)
target_link_libraries(CerebralMatrix PRIVATE pthread)

# 5. Tests & Diagnostics
add_executable(inference_test tests/inference_test.cpp src/inference/ModelManager.cpp)
target_link_libraries(inference_test PRIVATE cppzmq nlohmann_json::nlohmann_json ${SYSTEM_LIBS})

add_executable(diagnostic_tool tests/diagnostic_tool.cpp)
target_link_libraries(diagnostic_tool PRIVATE zmq nlohmann_json::nlohmann_json)

add_executable(monologue_test tests/monologue_test.cpp)
target_link_libraries(monologue_test PRIVATE zmq nlohmann_json::nlohmann_json)

add_executable(dream_sandbox_test tests/dream_sandbox_test.cpp)
target_link_libraries(dream_sandbox_test PRIVATE zmq nlohmann_json::nlohmann_json)

add_executable(full_cognitive_test tests/full_cognitive_test.cpp)
target_link_libraries(full_cognitive_test PRIVATE zmq nlohmann_json::nlohmann_json)

add_executable(curiosity_test tests/curiosity_test.cpp)
target_link_libraries(curiosity_test PRIVATE zmq nlohmann_json::nlohmann_json)

add_executable(neuro_surgery_test tests/neuro_surgery_test.cpp)
target_link_libraries(neuro_surgery_test PRIVATE zmq nlohmann_json::nlohmann_json)

add_executable(sensory_test tests/sensory_test.cpp)
target_link_libraries(sensory_test PRIVATE zmq nlohmann_json::nlohmann_json)
