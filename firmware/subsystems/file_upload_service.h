
#pragma once
#include <Arduino.h>

namespace Flic {
namespace FileUploadService {
void begin();
void pollSerial();
void handleChunk(const uint8_t* data, size_t len, size_t chunkNum, size_t totalChunks);
}
}
