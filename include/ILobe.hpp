#pragma once

namespace neuroswarm {

// Common interface for dynamically loadable Lobes
class ILobe {
public:
    virtual ~ILobe() = default;
    
    // Starts the Lobe's main loop (expected to block or manage its own thread loop)
    virtual void start() = 0;
    
    // Signals the Lobe to stop its operations and gracefully exit its start() loop
    virtual void stop() = 0;
};

} // namespace neuroswarm

// Standard C-linkage entry points for dlopen / dlsym
extern "C" {
    neuroswarm::ILobe* create_lobe();
    void destroy_lobe(neuroswarm::ILobe* lobe);
    void start_lobe(neuroswarm::ILobe* lobe);
    void stop_lobe(neuroswarm::ILobe* lobe);
}
