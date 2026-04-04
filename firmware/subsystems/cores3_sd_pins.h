#pragma once

#include <Arduino.h>

namespace Flic {
namespace CoreS3SdPins {

// M5Stack CoreS3 TF-card SPI pins per official CoreS3 docs.
// SCK  -> GPIO39
// MOSI -> GPIO38
// MISO -> GPIO40
// CS   -> GPIO41
constexpr int kSpiSck = 39;
constexpr int kSpiMosi = 38;
constexpr int kSpiMiso = 40;
constexpr int kSpiCs = 41;

}  // namespace CoreS3SdPins
}  // namespace Flic
