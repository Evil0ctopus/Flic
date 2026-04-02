#include "speech_recognition.h"

#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

namespace Flic {

bool SpeechRecognition::begin() {
    isRecording_ = false;
    audioBuffer_.clear();
    audioBuffer_.reserve(kAudioBufferSize);
    recordingStartMs_ = 0;
    lastTranscript_ = "";
    hasNewTranscript_ = false;
    return true;
}

void SpeechRecognition::startRecording() {
    if (!M5.Mic.isEnabled()) {
        Serial.println("[SpeechRecognition] Microphone not available");
        return;
    }
    
    isRecording_ = true;
    audioBuffer_.clear();
    recordingStartMs_ = millis();
    Serial.println("[SpeechRecognition] Started recording audio...");
}

void SpeechRecognition::stopRecording() {
    if (!isRecording_) {
        return;
    }
    
    isRecording_ = false;
    Serial.printf("[SpeechRecognition] Stopped recording. Buffer size: %d bytes\n", (int)audioBuffer_.size());
    
    // Send to Google STT
    if (audioBuffer_.size() > 0) {
        String result;
        if (sendAudioToGoogle(audioBuffer_, result)) {
            lastTranscript_ = result;
            hasNewTranscript_ = true;
            Serial.println("[SpeechRecognition] Transcript: " + result);
        } else {
            Serial.println("[SpeechRecognition] Failed to get transcript");
        }
    }
}

void SpeechRecognition::update() {
    if (!isRecording_) {
        return;
    }
    
    // Check for recording timeout
    if ((millis() - recordingStartMs_) > kRecordingTimeoutMs) {
        Serial.println("[SpeechRecognition] Recording timeout");
        stopRecording();
        return;
    }
    
    // Capture audio samples from microphone
    if (!M5.Mic.isEnabled()) {
        return;
    }
    
    // Get audio data from microphone
    static constexpr size_t kChunkSize = 256;
    static int16_t audioChunk[kChunkSize];
    
    if (!M5.Mic.isRecording()) {
        if (M5.Mic.record(audioChunk, kChunkSize)) {
            // Convert int16 samples to uint8 for storage
            for (size_t i = 0; i < kChunkSize && audioBuffer_.size() < kAudioBufferSize; ++i) {
                audioBuffer_.push_back((uint8_t)(audioChunk[i] >> 8));
                if (audioBuffer_.size() < kAudioBufferSize) {
                    audioBuffer_.push_back((uint8_t)(audioChunk[i] & 0xFF));
                }
            }
        }
    }
}

bool SpeechRecognition::popTranscript(String& textOut) {
    if (!hasNewTranscript_) {
        return false;
    }
    
    textOut = lastTranscript_;
    hasNewTranscript_ = false;
    lastTranscript_ = "";
    return true;
}

bool SpeechRecognition::sendAudioToGoogle(const std::vector<uint8_t>& audioData, String& resultOut) {
    if (audioData.size() == 0) {
        return false;
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[SpeechRecognition] WiFi not connected");
        return false;
    }
    
    // Google Cloud Speech-to-Text API endpoint
    // Uses a free tier with limited requests - for production use your own API key
    String url = "https://www.google.com/speech-api/json?action=D&client=chromium&maxresults=1&pair=";
    
    HTTPClient http;
    http.setConnectTimeout(5000);
    http.setTimeout(10000);
    
    // For a simpler approach, use the Vosk offline ASR or local WebSpeechAPI via WebUI
    // This is a demonstration - actual implementation would require:
    // 1. Google Cloud Speech API key (paid service)
    // 2. WebRTC-based recording from browser
    // 3. Local Vosk model for on-device recognition
    
    Serial.println("[SpeechRecognition] Audio buffer ready for transcription");
    Serial.printf("[SpeechRecognition] Audio size: %d bytes\n", (int)audioData.size());
    
    // Return mock response for now
    resultOut = "I received your audio message";
    return true;
}

}  // namespace Flic
