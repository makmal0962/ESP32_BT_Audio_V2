#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <cstdint>

// Mono snapshot buffer fed from AudioPipeline's Q1.31 stereo stream.
// Single producer (audio_render task), single consumer (display task).
class AudioTap {
public:
    static constexpr int BUF_LEN = 1024;

    static AudioTap& instance() {
        static AudioTap tap;
        return tap;
    }

    bool init() {
        m_mutex = xSemaphoreCreateMutex();
        return m_mutex != nullptr;
    }

    void IRAM_ATTR onFrame(int32_t left, int32_t right) {
        int16_t mono = (int16_t)(((left >> 16) + (right >> 16)) / 2);
        if (xSemaphoreTake(m_mutex, 0)) {
            m_ring[m_pos % BUF_LEN] = mono;
            m_pos++;
            xSemaphoreGive(m_mutex);
        }
    }

    uint32_t pos() const { return m_pos; }

    void snapshot(int16_t *out, int count, uint32_t fromPos) {
        xSemaphoreTake(m_mutex, portMAX_DELAY);
        for (int i = 0; i < count; i++)
            out[i] = m_ring[(fromPos + i) % BUF_LEN];
        xSemaphoreGive(m_mutex);
    }

private:
    AudioTap() = default;
    int16_t m_ring[BUF_LEN] = {};
    uint32_t m_pos = 0;
    SemaphoreHandle_t m_mutex = nullptr;
};