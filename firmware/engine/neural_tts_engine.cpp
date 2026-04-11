#include "neural_tts_engine.h"

#include "../subsystems/sd_manager.h"

#include <M5Unified.h>
#include <SD.h>

#include <cmath>

namespace Flic {
namespace {
constexpr const char* kVoicesDir = "/Flic/voices";
constexpr uint32_t kTtsQueueWaitMs = 0;
constexpr uint32_t kNeuralTaskStackWords = 8192;
constexpr uint32_t kNeuralTaskPriority = 2;

struct NeuralTtsRequest {
    char text[224];
    char emotion[24];
    char lang[8];
    uint32_t queuedAtMs = 0;
};

QueueHandle_t gNeuralTtsQueue = nullptr;
TaskHandle_t gNeuralTtsTask = nullptr;
volatile bool gNeuralTtsBusy = false;
NeuralTtsEngine* gNeuralOwner = nullptr;

String sanitizeSpeechText(const String& input) {
    String cleaned;
    cleaned.reserve(input.length());
    for (size_t index = 0; index < static_cast<size_t>(input.length()); ++index) {
        char c = input.charAt(index);
        if (c == '\n' || c == '\r' || c == '\t') {
            c = ' ';
        }
        if (c == '"' || c == '\'' || c == '[' || c == ']' || c == '{' || c == '}' || c == '<' || c == '>') {
            continue;
        }
        cleaned += c;
    }
    cleaned.trim();
    if (cleaned.length() > 180) {
        cleaned = cleaned.substring(0, 180);
    }
    return cleaned;
}

void neuralTtsTask(void* parameter) {
    auto* owner = static_cast<NeuralTtsEngine*>(parameter);
    NeuralTtsRequest request{};

    for (;;) {
        if (gNeuralTtsQueue == nullptr || owner == nullptr) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (xQueueReceive(gNeuralTtsQueue, &request, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        gNeuralTtsBusy = true;
        const unsigned long startMs = millis();
        Serial.printf("[NeuralTts] speak start (queued %lu ms ago): %s\n",
                      static_cast<unsigned long>(startMs - request.queuedAtMs), request.text);

        const String text = String(request.text);
        const String emotion = String(request.emotion);
        const String lang = String(request.lang);

        bool spoken = owner->processQueuedSpeech(text, emotion, lang);
        if (!spoken) {
            M5.Speaker.tone(280.0f, 80);
            Serial.println("[NeuralTts] No neural voice output available");
        }

        const unsigned long elapsedMs = millis() - startMs;
        Serial.printf("[NeuralTts] speak end (%lu ms)\n", static_cast<unsigned long>(elapsedMs));
        gNeuralTtsBusy = false;
    }
}
}  // namespace

bool NeuralTtsEngine::begin() {
    if (SdManager::isMounted()) {
        SdManager::ensureDirectory(SdManager::rootDir());
        SdManager::ensureDirectory(SdManager::voicesDir());
    }

    if (gNeuralTtsQueue == nullptr) {
        gNeuralTtsQueue = xQueueCreate(3, sizeof(NeuralTtsRequest));
    }

    gNeuralOwner = this;
    if (gNeuralTtsTask == nullptr && gNeuralTtsQueue != nullptr) {
        xTaskCreatePinnedToCore(neuralTtsTask,
                                "neural_tts",
                                kNeuralTaskStackWords,
                                this,
                                kNeuralTaskPriority,
                                &gNeuralTtsTask,
                                0);
    }

    const std::vector<String> voices = listVoices();
    if (!voices.empty() && activeVoiceModel_.length() == 0) {
        activeVoiceModel_ = voices.front();
    }

    return gNeuralTtsQueue != nullptr && gNeuralTtsTask != nullptr;
}

bool NeuralTtsEngine::speak(const String& text, const String& emotion, const String& lang) {
    if (gNeuralTtsQueue == nullptr) {
        return false;
    }

    String speechText = sanitizeSpeechText(text);
    if (speechText.length() == 0) {
        return false;
    }
    if (!speechText.endsWith(".") && !speechText.endsWith("!") && !speechText.endsWith("?")) {
        speechText += ".";
    }

    NeuralTtsRequest request{};
    snprintf(request.text, sizeof(request.text), "%s", speechText.c_str());
    snprintf(request.emotion, sizeof(request.emotion), "%s", emotion.c_str());
    snprintf(request.lang, sizeof(request.lang), "%s", lang.c_str());
    request.queuedAtMs = millis();

    return xQueueSend(gNeuralTtsQueue, &request, kTtsQueueWaitMs) == pdTRUE;
}

bool NeuralTtsEngine::processQueuedSpeech(const String& text, const String& emotion, const String& lang) {
    const size_t textLen = static_cast<size_t>(text.length());
    const size_t samples = textLen < 8 ? 8 : (textLen > 36 ? 36 : textLen);
    for (size_t i = 0; i < samples; ++i) {
        const float t = samples <= 1 ? 0.0f : static_cast<float>(i) / static_cast<float>(samples - 1);
        const float envelope = 0.28f + 0.68f * std::fabs(std::sin(t * 10.0f));
        emitAmplitudeEnvelope(envelope);
        vTaskDelay(pdMS_TO_TICKS(12));
    }

    const bool spoken = attemptNeuralInference(text, emotion, lang);
    emitAmplitudeEnvelope(0.0f);
    return spoken;
}

std::vector<String> NeuralTtsEngine::listVoices() const {
    std::vector<String> voices;
    if (!SdManager::isMounted()) {
        return voices;
    }

    File root = SD.open(kVoicesDir);
    if (!root || !root.isDirectory()) {
        return voices;
    }

    File entry = root.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            const String fileName = entry.name();
            if (fileName.endsWith(".tflite")) {
                voices.push_back(fileName);
            }
        }
        entry = root.openNextFile();
    }

    return voices;
}

bool NeuralTtsEngine::setActiveVoice(const String& modelFileName) {
    const String normalized = normalizeVoiceName(modelFileName);
    if (normalized.length() == 0) {
        return false;
    }

    const std::vector<String> voices = listVoices();
    for (const String& voice : voices) {
        if (voice == normalized) {
            activeVoiceModel_ = voice;
            Serial.printf("[NeuralTts] Active voice model: %s\n", activeVoiceModel_.c_str());
            return true;
        }
    }
    return false;
}

String NeuralTtsEngine::activeVoice() const {
    return activeVoiceModel_;
}

void NeuralTtsEngine::setVoiceParams(float speed, float pitch, float clarity) {
    speed_ = speed < 0.5f ? 0.5f : (speed > 2.0f ? 2.0f : speed);
    pitch_ = pitch < 0.5f ? 0.5f : (pitch > 2.0f ? 2.0f : pitch);
    clarity_ = clarity < 0.5f ? 0.5f : (clarity > 2.0f ? 2.0f : clarity);
}

float NeuralTtsEngine::speed() const {
    return speed_;
}

float NeuralTtsEngine::pitch() const {
    return pitch_;
}

float NeuralTtsEngine::clarity() const {
    return clarity_;
}

bool NeuralTtsEngine::isBusy() const {
    return gNeuralTtsBusy;
}

void NeuralTtsEngine::setAmplitudeEnvelopeHandler(AmplitudeEnvelopeFn handler, void* context) {
    amplitudeEnvelope_ = handler;
    amplitudeEnvelopeContext_ = context;
}

String NeuralTtsEngine::normalizeVoiceName(const String& modelFileName) const {
    String name = modelFileName;
    name.trim();
    if (name.length() == 0) {
        return String();
    }
    if (!name.endsWith(".tflite")) {
        name += ".tflite";
    }
    return name;
}

bool NeuralTtsEngine::attemptNeuralInference(const String& text, const String& emotion, const String& lang) const {
    (void)text;
    (void)emotion;
    (void)lang;

    if (activeVoiceModel_.length() == 0) {
        return false;
    }

    if (!SdManager::isMounted()) {
        return false;
    }

    const String modelPath = String(kVoicesDir) + "/" + activeVoiceModel_;
    if (!SD.exists(modelPath)) {
        return false;
    }

    // Placeholder neural path: model discovery and hot-swap are active.
    Serial.printf("[NeuralTts] Neural model selected: %s (speed=%.2f pitch=%.2f clarity=%.2f)\n",
                  modelPath.c_str(),
                  static_cast<double>(speed_),
                  static_cast<double>(pitch_),
                  static_cast<double>(clarity_));
    Serial.println("[NeuralTts] Inference backend unavailable in this firmware build");
    return false;
}

void NeuralTtsEngine::emitAmplitudeEnvelope(float amplitude) const {
    if (amplitude < 0.0f) {
        amplitude = 0.0f;
    }
    if (amplitude > 1.0f) {
        amplitude = 1.0f;
    }
    if (amplitudeEnvelope_ != nullptr) {
        amplitudeEnvelope_(amplitude, amplitudeEnvelopeContext_);
    }
}

}  // namespace Flic
