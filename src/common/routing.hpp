#pragma once
// NeuroSwarm Specialised Routing — topic-based intent filtering via ZMQ XPUB/XSUB.
//
// Biological analogue: the thalamus doesn't broadcast raw sensory data to every
// cortical area — it routes visual signals to V1, auditory to A1, somatosensory
// to S1. This header provides the same selectivity for the neural bus.
//
// Message format: "intent_name payload_json"
// The intent is prepended as a ZMQ topic prefix. Subscribers filter at the
// transport layer — messages that don't match never leave the Thalamus.

#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace routing {

constexpr char TOPIC_SEP = ' ';

// Publish a JSON message with intent-based topic routing.
// Extracts "intent" from the JSON and prepends it as the topic prefix.
inline void publish(zmq::socket_t& pub, const nlohmann::json& data) {
    std::string intent = data.value("intent", "");
    std::string payload = data.dump();
    std::string frame = intent + TOPIC_SEP + payload;
    zmq::message_t msg(frame.size());
    memcpy(msg.data(), frame.c_str(), frame.size());
    pub.send(msg, zmq::send_flags::none);
}

// Subscribe to specific intents only.
inline void subscribe(zmq::socket_t& sub, const std::vector<std::string>& intents) {
    for (const auto& intent : intents) {
        std::string topic = intent + TOPIC_SEP;
        sub.set(zmq::sockopt::subscribe, topic);
    }
}

// Subscribe to everything (monitoring/logging lobes).
inline void subscribe_all(zmq::socket_t& sub) {
    sub.set(zmq::sockopt::subscribe, "");
}

// Receive a routed message. Returns empty json on no message or parse failure.
inline nlohmann::json receive(zmq::socket_t& sub, zmq::recv_flags flags = zmq::recv_flags::none) {
    zmq::message_t msg;
    if (!sub.recv(msg, flags)) return {};

    std::string raw(static_cast<char*>(msg.data()), msg.size());

    // Strip topic prefix (everything before first TOPIC_SEP)
    auto sep = raw.find(TOPIC_SEP);
    if (sep != std::string::npos) {
        raw = raw.substr(sep + 1);
    }

    try {
        if (!raw.empty() && raw[0] == '{') {
            return nlohmann::json::parse(raw);
        }
    } catch (...) {}

    return {};
}

} // namespace routing
