#pragma once

#include <Arduino.h>
#include <vector>

namespace Flic {

class SpeechRecognition {
public:
    bool begin();
    void update();
    
    // Start recording audio for STT
    void startRecording();
    void stopRecording();
    bool isRecording() const { return isRecording_; }
    
    // Get transcribed text
    bool popTranscript(String& textOut);

private:
    static constexpr size_t kAudioBufferSize = 16384;  // ~1 second at 16kHz
    static constexpr uint32_t kRecordingTimeoutMs = 5000;  // Max 5 seconds
    
    bool isRecording_ = false;
    std::vector<uint8_t> audioBuffer_;
    uint32_t recordingStartMs_ = 0;
    String lastTranscript_;
    bool hasNewTranscript_ = false;
    
    // Send audio to Google STT API
    bool sendAudioToGoogle(const std::vector<uint8_t>& audioData, String& resultOut);
};

}  // namespace Flic
