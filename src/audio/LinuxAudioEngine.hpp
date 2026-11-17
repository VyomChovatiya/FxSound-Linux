#pragma once

#include "../dsp/DfxDspEngine.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fxsound {

struct AudioOutputDevice {
    std::string id;          // PulseAudio / PipeWire sink name (e.g. alsa_output.pci-...)
    std::string description; // Human-readable device label (e.g. "Built-in Audio Analog Stereo")
    bool isDefault = false;
};

class LinuxAudioEngine {
public:
    static constexpr const char* kVirtualSinkName = "FxSound_Audio_Enhancer";
    static constexpr const char* kVirtualSinkDescription = "FxSound Audio Enhancer";

    explicit LinuxAudioEngine(DfxDspEngine& dspEngine);
    ~LinuxAudioEngine();

    LinuxAudioEngine(const LinuxAudioEngine&) = delete;
    LinuxAudioEngine& operator=(const LinuxAudioEngine&) = delete;

    // Enumerate available physical output devices (excluding the FxSound virtual sink itself)
    std::vector<AudioOutputDevice> enumerateOutputDevices();

    // Start virtual sink + real-time capture -> DfxDspEngine -> hardware playback loop
    bool start(bool makeDefaultSink = true);
    void stop();
    bool isRunning() const noexcept { return running_.load(std::memory_order_relaxed); }

    // Select physical output sink by ID (empty = auto/first hardware sink)
    void setTargetOutputDevice(const std::string& sinkId);
    std::string targetOutputDevice() const;

    void setAutoSwitchNewDevice(bool enabled) noexcept {
        autoSwitchNewDevice_.store(enabled, std::memory_order_relaxed);
    }
    bool autoSwitchNewDevice() const noexcept {
        return autoSwitchNewDevice_.load(std::memory_order_relaxed);
    }

    int virtualSinkModuleId() const noexcept { return virtualSinkModuleId_; }

private:
    bool ensureVirtualSinkCreated(bool makeDefaultSink);
    void removeVirtualSink();
    void audioThreadLoop();

    DfxDspEngine& dspEngine_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> streamReconnectNeeded_{false};
    std::atomic<bool> autoSwitchNewDevice_{true};

    mutable std::mutex deviceMutex_;
    std::string targetSinkId_;
    std::string previousDefaultSink_;
    std::vector<std::string> knownSinkIds_;

    int virtualSinkModuleId_ = -1;
    std::thread audioThread_;
};

} // namespace fxsound
