#pragma once

#include <Arduino.h>

namespace Flic {
namespace CoreS3SdPins {

// M5Stack CoreS3 TF-card SPI pins per official CoreS3 hardware docs/pin map.
// TF CS   -> GPIO4
// TF MOSI -> GPIO35
// TF MISO -> GPIO37
// TF SCK  -> GPIO36
constexpr int kCs = 4;
constexpr int kMosi = 35;
constexpr int kMiso = 37;
constexpr int kSck = 36;

}  // namespace CoreS3SdPins
}  // namespace Flic
