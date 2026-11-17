#include "LinuxAudioEngine.hpp"

#include <pulse/error.h>
#include <pulse/pulseaudio.h>
#include <pulse/simple.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace fxsound {

namespace {

std::string runCommandOutput(const std::string& cmd) {
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return result;
    }
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    pclose(pipe);
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' ')) {
        result.pop_back();
    }
    return result;
}

struct PulseSinkEnumContext {
    pa_mainloop* mainloop = nullptr;
    std::string defaultSinkName;
    std::vector<AudioOutputDevice> devices;
};

void serverInfoCallback(pa_context* /*c*/, const pa_server_info* info, void* userdata) {
    auto* ctx = static_cast<PulseSinkEnumContext*>(userdata);
    if (info && info->default_sink_name) {
        ctx->defaultSinkName = info->default_sink_name;
    }
}

void sinkInfoCallback(pa_context* /*c*/, const pa_sink_info* info, int eol, void* userdata) {
    auto* ctx = static_cast<PulseSinkEnumContext*>(userdata);
    if (eol > 0 || !info) {
        return;
    }
    const std::string name = info->name ? info->name : "";
    if (name.empty() || name == LinuxAudioEngine::kVirtualSinkName) {
        return;
    }
    std::string desc = info->description ? info->description : name;
    ctx->devices.push_back(AudioOutputDevice{
        name,
        desc,
        (name == ctx->defaultSinkName)
    });
}

} // namespace

LinuxAudioEngine::LinuxAudioEngine(DfxDspEngine& dspEngine)
    : dspEngine_(dspEngine) {
    const auto devs = enumerateOutputDevices();
    if (!devs.empty()) {
        for (const auto& d : devs) {
            knownSinkIds_.push_back(d.id);
            if (d.isDefault && targetSinkId_.empty()) {
                targetSinkId_ = d.id;
                previousDefaultSink_ = d.id;
            }
        }
        if (targetSinkId_.empty()) {
            targetSinkId_ = devs.front().id;
            previousDefaultSink_ = devs.front().id;
        }
    }
}

LinuxAudioEngine::~LinuxAudioEngine() {
    stop();
}

std::vector<AudioOutputDevice> LinuxAudioEngine::enumerateOutputDevices() {
    PulseSinkEnumContext enumCtx;
    enumCtx.mainloop = pa_mainloop_new();
    if (!enumCtx.mainloop) {
        return {};
    }

    pa_mainloop_api* api = pa_mainloop_get_api(enumCtx.mainloop);
    pa_context* context = pa_context_new(api, "FxSound-DeviceEnumerator");
    if (!context) {
        pa_mainloop_free(enumCtx.mainloop);
        return {};
    }

    if (pa_context_connect(context, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
        pa_context_unref(context);
        pa_mainloop_free(enumCtx.mainloop);
        return {};
    }

    // Wait until context is ready (with timeout)
    bool ready = false;
    for (int iter = 0; iter < 200; ++iter) {
        pa_mainloop_iterate(enumCtx.mainloop, 0, nullptr);
        const pa_context_state_t state = pa_context_get_state(context);
        if (state == PA_CONTEXT_READY) {
            ready = true;
            break;
        }
        if (!PA_CONTEXT_IS_GOOD(state)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    if (ready) {
        pa_operation* opServer = pa_context_get_server_info(context, serverInfoCallback, &enumCtx);
        if (opServer) {
            while (pa_operation_get_state(opServer) == PA_OPERATION_RUNNING) {
                pa_mainloop_iterate(enumCtx.mainloop, 1, nullptr);
            }
            pa_operation_unref(opServer);
        }

        pa_operation* opSinks = pa_context_get_sink_info_list(context, sinkInfoCallback, &enumCtx);
        if (opSinks) {
            while (pa_operation_get_state(opSinks) == PA_OPERATION_RUNNING) {
                pa_mainloop_iterate(enumCtx.mainloop, 1, nullptr);
            }
            pa_operation_unref(opSinks);
        }
    }

    pa_context_disconnect(context);
    pa_context_unref(context);
    pa_mainloop_free(enumCtx.mainloop);

    for (auto& d : enumCtx.devices) {
        d.isDefault = (d.id == enumCtx.defaultSinkName);
    }

    return enumCtx.devices;
}

bool LinuxAudioEngine::ensureVirtualSinkCreated(bool makeDefaultSink) {
    if (virtualSinkModuleId_ >= 0) {
        return true;
    }

    // Check if an existing FxSound_Audio_Enhancer module is already loaded
    const std::string modulesShort = runCommandOutput("pactl list short modules 2>/dev/null");
    if (!modulesShort.empty()) {
        std::istringstream iss(modulesShort);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.find("module-null-sink") != std::string::npos &&
                line.find(kVirtualSinkName) != std::string::npos) {
                try {
                    virtualSinkModuleId_ = std::stoi(line.substr(0, line.find('\t')));
                } catch (...) {
                    virtualSinkModuleId_ = -1;
                }
                break;
            }
        }
    }

    if (virtualSinkModuleId_ < 0) {
        // Save current default sink before creating our virtual sink
        const std::string curDefault = runCommandOutput("pactl get-default-sink 2>/dev/null");
        if (!curDefault.empty() && curDefault != kVirtualSinkName) {
            std::lock_guard<std::mutex> lock(deviceMutex_);
            previousDefaultSink_ = curDefault;
            if (targetSinkId_.empty()) {
                targetSinkId_ = curDefault;
            }
        }

        std::ostringstream cmd;
        cmd << "pactl load-module module-null-sink "
            << "sink_name=" << kVirtualSinkName << " "
            << "sink_properties=device.description=FxSound_Audio_Enhancer "
            << "format=float32le rate=48000 channels=2 2>/dev/null";
        const std::string modIdStr = runCommandOutput(cmd.str());
        if (!modIdStr.empty()) {
            try {
                virtualSinkModuleId_ = std::stoi(modIdStr);
            } catch (...) {
                virtualSinkModuleId_ = -1;
            }
        }
    }

    if (virtualSinkModuleId_ >= 0) {
        // Ensure virtual sink and its monitor source are unmuted and locked to 100% (0 dB unity)
        // so songs never suffer double volume attenuation between the virtual sink and hardware sink
        runCommandOutput(std::string("pactl set-sink-mute ") + kVirtualSinkName + " 0 2>/dev/null");
        runCommandOutput(std::string("pactl set-sink-volume ") + kVirtualSinkName + " 100% 2>/dev/null");
        runCommandOutput(std::string("pactl set-source-mute ") + kVirtualSinkName + ".monitor 0 2>/dev/null");
        runCommandOutput(std::string("pactl set-source-volume ") + kVirtualSinkName + ".monitor 100% 2>/dev/null");

        if (makeDefaultSink) {
            std::ostringstream setDefCmd;
            setDefCmd << "pactl set-default-sink " << kVirtualSinkName << " 2>/dev/null";
            runCommandOutput(setDefCmd.str());
        }
    }

    return virtualSinkModuleId_ >= 0;
}

void LinuxAudioEngine::removeVirtualSink() {
    std::string restoreSink;
    {
        std::lock_guard<std::mutex> lock(deviceMutex_);
        restoreSink = previousDefaultSink_;
    }
    if (!restoreSink.empty() && restoreSink != kVirtualSinkName) {
        std::ostringstream restoreCmd;
        restoreCmd << "pactl set-default-sink " << restoreSink << " 2>/dev/null";
        runCommandOutput(restoreCmd.str());
    }

    if (virtualSinkModuleId_ >= 0) {
        std::ostringstream unloadCmd;
        unloadCmd << "pactl unload-module " << virtualSinkModuleId_ << " 2>/dev/null";
        runCommandOutput(unloadCmd.str());
        virtualSinkModuleId_ = -1;
    }
}

bool LinuxAudioEngine::start(bool makeDefaultSink) {
    if (running_.load(std::memory_order_relaxed)) {
        return true;
    }

    ensureVirtualSinkCreated(makeDefaultSink);
    dspEngine_.setSampleRate(48000.0f);

    stopRequested_.store(false, std::memory_order_relaxed);
    running_.store(true, std::memory_order_relaxed);
    audioThread_ = std::thread(&LinuxAudioEngine::audioThreadLoop, this);
    return true;
}

void LinuxAudioEngine::stop() {
    if (!running_.load(std::memory_order_relaxed)) {
        removeVirtualSink();
        return;
    }
    stopRequested_.store(true, std::memory_order_relaxed);
    if (audioThread_.joinable()) {
        audioThread_.join();
    }
    running_.store(false, std::memory_order_relaxed);
    removeVirtualSink();
}

void LinuxAudioEngine::setTargetOutputDevice(const std::string& sinkId) {
    if (sinkId == kVirtualSinkName) {
        return; // Never route virtual sink back into itself
    }
    {
        std::lock_guard<std::mutex> lock(deviceMutex_);
        if (targetSinkId_ == sinkId) {
            return;
        }
        targetSinkId_ = sinkId;
    }
    streamReconnectNeeded_.store(true, std::memory_order_relaxed);
}

std::string LinuxAudioEngine::targetOutputDevice() const {
    std::lock_guard<std::mutex> lock(deviceMutex_);
    return targetSinkId_;
}

void LinuxAudioEngine::audioThreadLoop() {
    constexpr uint32_t kSampleRate = 48000;
    constexpr size_t kFramesPerBlock = 512; // 10.67 ms glitch-free studio block
    constexpr size_t kSamplesPerBlock = kFramesPerBlock * 2;

    pa_sample_spec sampleSpec{};
    sampleSpec.format = PA_SAMPLE_FLOAT32LE;
    sampleSpec.rate = kSampleRate;
    sampleSpec.channels = 2;

    pa_buffer_attr recordAttr{};
    recordAttr.maxlength = static_cast<uint32_t>(-1);
    recordAttr.tlength = static_cast<uint32_t>(-1);
    recordAttr.prebuf = static_cast<uint32_t>(-1);
    recordAttr.minreq = static_cast<uint32_t>(-1);
    recordAttr.fragsize = static_cast<uint32_t>(kSamplesPerBlock * sizeof(float));

    pa_buffer_attr playbackAttr{};
    playbackAttr.maxlength = static_cast<uint32_t>(kSamplesPerBlock * sizeof(float) * 6);
    playbackAttr.tlength = static_cast<uint32_t>(kSamplesPerBlock * sizeof(float) * 3);
    playbackAttr.prebuf = static_cast<uint32_t>(-1);
    playbackAttr.minreq = static_cast<uint32_t>(kSamplesPerBlock * sizeof(float));
    playbackAttr.fragsize = static_cast<uint32_t>(-1);

    const std::string monitorSource = std::string(kVirtualSinkName) + ".monitor";
    std::vector<float> audioBuffer(kSamplesPerBlock, 0.0f);

    pa_simple* captureStream = nullptr;
    pa_simple* playbackStream = nullptr;

    auto closeStreams = [&]() {
        if (captureStream) {
            pa_simple_free(captureStream);
            captureStream = nullptr;
        }
        if (playbackStream) {
            pa_simple_free(playbackStream);
            playbackStream = nullptr;
        }
    };

    while (!stopRequested_.load(std::memory_order_relaxed)) {
        if (!captureStream || !playbackStream || streamReconnectNeeded_.exchange(false)) {
            closeStreams();

            std::string outSink;
            {
                std::lock_guard<std::mutex> lock(deviceMutex_);
                outSink = targetSinkId_;
            }

            int paErr = 0;
            captureStream = pa_simple_new(
                nullptr,
                "FxSound",
                PA_STREAM_RECORD,
                monitorSource.c_str(),
                "FxSound Virtual Sink Capture",
                &sampleSpec,
                nullptr,
                &recordAttr,
                &paErr
            );

            playbackStream = pa_simple_new(
                nullptr,
                "FxSound",
                PA_STREAM_PLAYBACK,
                outSink.empty() ? nullptr : outSink.c_str(),
                "FxSound Enhanced Output",
                &sampleSpec,
                nullptr,
                &playbackAttr,
                &paErr
            );

            if (!captureStream || !playbackStream) {
                closeStreams();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
        }

        int paErr = 0;
        if (pa_simple_read(captureStream, audioBuffer.data(), audioBuffer.size() * sizeof(float), &paErr) < 0) {
            closeStreams();
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            continue;
        }

        // Run the full reversed FxSound DSP pipeline in-place on the 256-frame block
        dspEngine_.processStereoInterleaved(audioBuffer.data(), kFramesPerBlock);

        if (pa_simple_write(playbackStream, audioBuffer.data(), audioBuffer.size() * sizeof(float), &paErr) < 0) {
            closeStreams();
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            continue;
        }
    }

    closeStreams();
}

} // namespace fxsound
