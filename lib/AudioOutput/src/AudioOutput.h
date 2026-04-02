#pragma once

#include <Arduino.h>

class AudioOutput {
public:
    virtual ~AudioOutput() = default;

    virtual bool begin() = 0;
    virtual bool ConsumeSample(int16_t sample[2]) = 0;
    virtual void flush() {}
    virtual bool stop() = 0;

    void SetRate(uint32_t rate) { hertz = rate; }
    void SetChannels(uint8_t channelsValue) { channels = channelsValue; }

protected:
    uint32_t hertz = 22050;
    uint8_t channels = 1;
};
