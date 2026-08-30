#pragma once
#include <vector>
#include <atomic>
#include <mutex>
#include <string>
#include "../vendor/miniaudio/miniaudio.h"

class AudioRecorder {
public:
    struct InputDevice {
        std::string name;
        ma_device_id id;
    };

    AudioRecorder() : isRecording(false), sampleRate(44100), isInitialized(false), currentPeak(0.0f) {}
    
    ~AudioRecorder() {
        if (isRecording) {
            stop();
        }
        if (isInitialized) {
            ma_device_uninit(&device);
        }
    }

    std::vector<InputDevice> getAvailableDevices() {
        std::vector<InputDevice> devices;
        ma_context context;
        if (ma_context_init(NULL, 0, NULL, &context) != MA_SUCCESS) {
            return devices;
        }

        ma_device_info* pPlaybackDeviceInfos;
        ma_uint32 playbackDeviceCount;
        ma_device_info* pCaptureDeviceInfos;
        ma_uint32 captureDeviceCount;

        if (ma_context_get_devices(&context, &pPlaybackDeviceInfos, &playbackDeviceCount, &pCaptureDeviceInfos, &captureDeviceCount) == MA_SUCCESS) {
            for (ma_uint32 i = 0; i < captureDeviceCount; ++i) {
                devices.push_back({ pCaptureDeviceInfos[i].name, pCaptureDeviceInfos[i].id });
            }
        }

        ma_context_uninit(&context);
        return devices;
    }

    bool init(int deviceIndex = -1, const std::vector<InputDevice>& devices = {}) {
        if (isInitialized) {
            ma_device_uninit(&device);
            isInitialized = false;
        }

        ma_device_config config = ma_device_config_init(ma_device_type_capture);
        config.capture.format = ma_format_f32;
        config.capture.channels = 1;
        config.sampleRate = 0; // Use native device sample rate to avoid WASAPI errors (AirPods often 48k/16k)
        config.dataCallback = data_callback;
        config.pUserData = this;

        if (deviceIndex >= 0 && deviceIndex < (int)devices.size()) {
            config.capture.pDeviceID = const_cast<ma_device_id*>(&devices[deviceIndex].id);
        }

        if (ma_device_init(NULL, &config, &device) != MA_SUCCESS) {
            // If specific device failed, try default
            config.capture.pDeviceID = NULL;
            if (ma_device_init(NULL, &config, &device) != MA_SUCCESS) {
                return false;
            }
        }
        
        this->sampleRate = device.sampleRate; // Capture actual rate for analyzer
        isInitialized = true;
        return true;
    }

    void start() {
        if (isRecording || !isInitialized) return;
        capturedSamples.clear();
        isRecording = true;
        ma_device_start(&device);
    }

    void stop() {
        if (!isRecording || !isInitialized) return;
        ma_device_stop(&device);
        isRecording = false;
    }

    bool getIsRecording() const { return isRecording; }

    std::vector<float> getSamples() {
        std::lock_guard<std::mutex> lock(mutex);
        return capturedSamples;
    }

    int getSampleRate() const { return sampleRate; }
    float getPeak() const { return currentPeak; }
    int getChannels() const { return device.capture.channels; }

    std::vector<float> getLastChunk(int count) {
        std::lock_guard<std::mutex> lock(mutex);
        if (capturedSamples.size() < (size_t)count) return capturedSamples;
        return std::vector<float>(capturedSamples.end() - count, capturedSamples.end());
    }

    static bool saveToWav(const std::string& filepath, int sampleRate, const std::vector<float>& samples);

private:
    static void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
        AudioRecorder* recorder = (AudioRecorder*)pDevice->pUserData;
        if (!recorder->isRecording) return;

        const float* input = (const float*)pInput;
        
        // Metering
        float peak = 0.0f;
        for (ma_uint32 i = 0; i < frameCount; ++i) {
            float absVal = std::abs(input[i]);
            if (absVal > peak) peak = absVal;
        }
        recorder->currentPeak = peak;

        std::lock_guard<std::mutex> lock(recorder->mutex);
        
        // Append new samples
        recorder->capturedSamples.insert(recorder->capturedSamples.end(), input, input + frameCount);
    }

    ma_device device;
    std::atomic<bool> isRecording;
    bool isInitialized;
    std::vector<float> capturedSamples;
    std::atomic<float> currentPeak;
    std::mutex mutex;
    int sampleRate;
};
