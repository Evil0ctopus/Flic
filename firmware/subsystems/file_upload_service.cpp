#include "file_upload_service.h"
#include "sd_manager.h"
#include <Arduino.h>
#include <SD.h>
#include <vector>
#include <cstring>

namespace Flic {
namespace FileUploadService {

static String currentPath;
static File currentFile;
static size_t expectedSize = 0;
static size_t receivedSize = 0;
static uint32_t expectedCrc = 0;
static std::vector<uint8_t> fileBuffer;
static bool uploadActive = false;

uint32_t crc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
            else crc >>= 1;
        }
    }
    return crc ^ 0xFFFFFFFF;
}

void resetUpload() {
    if (currentFile) {
        currentFile.close();
    }
    currentPath = "";
    expectedSize = 0;
    receivedSize = 0;
    expectedCrc = 0;
    fileBuffer.clear();
    uploadActive = false;
}

bool ensureDir(const String& path) {
    int idx = 1;
    while (true) {
        int next = path.indexOf('/', idx);
        if (next < 0) break;
        String dir = path.substring(0, next);
        if (!SD.exists(dir)) {
            if (!SD.mkdir(dir)) return false;
        }
        idx = next + 1;
    }
    return true;
}

void handleLine(const String& line) {
    if (line == "BEGIN_UPLOAD") {
        resetUpload();
        Serial.println("OK:READY");
        uploadActive = true;
        return;
    }
    if (!uploadActive) {
        Serial.println("ERR:No active upload");
        return;
    }
    if (line.startsWith("PATH:")) {
        currentPath = line.substring(5);
        if (!currentPath.startsWith("/")) currentPath = "/" + currentPath;
        if (!ensureDir(currentPath)) {
            Serial.println("ERR:Dir create failed");
            resetUpload();
            return;
        }
        if (SD.exists(currentPath)) SD.remove(currentPath);
        currentFile = SD.open(currentPath, FILE_WRITE);
        if (!currentFile) {
            Serial.println("ERR:File open failed");
            resetUpload();
            return;
        }
        Serial.println("OK:PATH");
        return;
    }
    if (line.startsWith("SIZE:")) {
        expectedSize = line.substring(5).toInt();
        fileBuffer.reserve(expectedSize);
        Serial.println("OK:SIZE");
        return;
    }
    if (line.startsWith("CRC:")) {
        expectedCrc = (uint32_t)strtoul(line.substring(4).c_str(), nullptr, 16);
        Serial.println("OK:CRC");
        return;
    }
    if (line.startsWith("CHUNK:")) {
        // Should not get here, binary handled elsewhere
        return;
    }
    if (line == "END_UPLOAD") {
        if (receivedSize != expectedSize) {
            Serial.println("ERR:Size mismatch");
            resetUpload();
            return;
        }
        currentFile.flush();
        currentFile.close();
        uint32_t crc = crc32(fileBuffer.data(), fileBuffer.size());
        if (crc != expectedCrc) {
            Serial.println("ERR:CRC mismatch");
            resetUpload();
            return;
        }
        Serial.println("OK:WRITE_COMPLETE");
        Serial.println("OK:CRC_MATCH");
        Serial.println("OK:DONE");
        resetUpload();
        return;
    }
    Serial.println("ERR:Unknown command");
}

void handleChunk(const uint8_t* data, size_t len, size_t chunkNum, size_t totalChunks) {
    if (!uploadActive || !currentFile) {
        Serial.println("ERR:No active upload");
        return;
    }
    if (!SdManager::isMounted()) {
        Serial.println("ERR:SD not mounted");
        resetUpload();
        return;
    }
    if (receivedSize + len > expectedSize) {
        Serial.println("ERR:Overflow");
        resetUpload();
        return;
    }
    fileBuffer.insert(fileBuffer.end(), data, data + len);
    currentFile.write(data, len);
    currentFile.flush();
    receivedSize += len;
    Serial.printf("OK:CHUNK %u/%u\n", (unsigned)chunkNum, (unsigned)totalChunks);
}

void pollSerial() {
    static String line;
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (line.length() > 0) {
                handleLine(line);
                line = "";
            }
        } else {
            line += c;
        }
    }
}

void pollBinaryChunk() {
    // This function should be called when expecting a CHUNK
    // Not implemented: actual binary chunk read, as this depends on serial protocol
}

void begin() {
    Serial.println("[FileUploadService] Ready");
    resetUpload();
}

} // namespace FileUploadService
} // namespace Flic
