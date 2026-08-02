#pragma once
#include "u8g2_esp32_hal.h"
#include "esp_dsp.h"
#include "audio_tap.h"
#include "bintobar_generated.h"   // bring over unchanged, format-independent

enum ScreenMode { SCREEN_MAIN, SCREEN_FFT, SCREEN_WAVE, SCREEN_SETTINGS, SCREEN_COUNT };

class OledDisplay {
public:
    static OledDisplay& instance() {
        static OledDisplay d;
        return d;
    }

    void init() {
        u8g2_esp32_hal_t hal = U8G2_ESP32_HAL_DEFAULT;
        hal.bus.i2c.sda = (gpio_num_t)APP_DISPLAY_I2C_SDA_GPIO;
        hal.bus.i2c.scl = (gpio_num_t)APP_DISPLAY_I2C_SCL_GPIO;
        hal.reset = U8G2_ESP32_HAL_UNDEFINED;   // set to your RES gpio_num_t if your module has one
        u8g2_esp32_hal_init(hal);

        u8g2_Setup_ssd1306_i2c_128x64_noname_f(&m_u8g2, U8G2_R0, u8g2_esp32_i2c_byte_cb, u8g2_esp32_gpio_and_delay_cb);
        u8x8_SetI2CAddress(&m_u8g2.u8x8, 0x78);   // 0x7A if your module is at 0x3D
        u8g2_InitDisplay(&m_u8g2);
        u8g2_SetPowerSave(&m_u8g2, 0);

        dsps_fft2r_init_fc32(NULL, FFT_SAMPLES);
        for (int i = 0; i < FFT_SAMPLES; i++)
            m_hamming[i] = 0.54f - 0.46f * cosf(2.0f * (float)M_PI * i / (FFT_SAMPLES - 1));

        xTaskCreatePinnedToCore(taskEntry, "oled_display", 4096, this, 3, nullptr, 0);
        
    }

    void toggleSettings() { m_settingsOpen = !m_settingsOpen; }

    void setVolume(uint8_t v) { m_volume = v; };

    void cycleScreen() { m_screenMode = (ScreenMode)((m_screenMode + 1) % SCREEN_COUNT); }
    ScreenMode screenMode() const { return m_screenMode; }

private:
    static constexpr int FFT_SAMPLES   = 1024;
    static constexpr int NYQUIST_BINS  = FFT_SAMPLES / 2;
    static constexpr int NUM_BARS      = 16;
    static constexpr uint32_t SCROLL_PAUSE_MS = 3000;
    static constexpr int SCROLL_GAP    = 64;
    static constexpr int SCROLL_SPEED  = 20;   // px/sec
    static constexpr float WAVE_ZOOM   = 0.5f;

    u8g2_t m_u8g2;
    ScreenMode m_screenMode = SCREEN_MAIN;
    float m_hamming[FFT_SAMPLES];
    float m_fftBuf[FFT_SAMPLES * 2];   // interleaved re/im
    int m_barHeight[NUM_BARS]   = {};
    int m_barPeakHold[NUM_BARS] = {};
    uint32_t m_barPeakMs[NUM_BARS] = {};

    static inline uint32_t nowMs() { return (uint32_t)(esp_timer_get_time() / 1000ULL); }
    static inline int iclamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

    static void taskEntry(void *arg) { static_cast<OledDisplay*>(arg)->task(); }

    bool m_settingsOpen = false;
    bool m_volumeVisible = false;
    uint32_t m_volumeStartMs;
    uint8_t m_volume = 0;
    uint8_t lastVolume = 0;

    void task() {
        for (;;) {
            uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
            u8g2_ClearBuffer(&m_u8g2);
            switch (m_screenMode) {
                case SCREEN_FFT:  drawFft(); break;
                case SCREEN_WAVE: drawWaveform(); break;
                case SCREEN_SETTINGS: drawSettings(); break;
                case SCREEN_MAIN:
                    u8g2_SetFont(&m_u8g2, u8g2_font_tallpixelextended_tr);
                    u8g2_DrawUTF8(&m_u8g2, 0, 30, "OLED Test");
                    break;
                default: /* SCREEN_MAIN wired up in the AVRCP step */ break;
            }

            if (m_volume != lastVolume) {
                m_volumeStartMs = now;
                lastVolume = m_volume;
            }
            if (now - m_volumeStartMs <= 3000 && now > 5000) drawVolume();

            u8g2_SendBuffer(&m_u8g2);
            // vTaskDelay(pdMS_TO_TICKS(20));
            vTaskDelay(1);
        }
    }

    void drawVolume(){
        u8g2_SetDrawColor(&m_u8g2, 0);
        u8g2_DrawBox(&m_u8g2, 0, 48, 128, 16);
        u8g2_SetDrawColor(&m_u8g2, 1);
        u8g2_DrawFrame(&m_u8g2, 0, 49, 128, 7);
        u8g2_DrawBox(&m_u8g2, 0, 49, m_volume, 7);
        char buf[16];
        snprintf(buf, sizeof(buf), "Volume : %d", m_volume);
        u8g2_SetFont(&m_u8g2, u8g2_font_5x7_tf);
        u8g2_DrawStr(&m_u8g2, (128 - u8g2_GetStrWidth(&m_u8g2, buf))/2, 63, buf);
    }

    void drawSettings() {
        u8g2_SetFont(&m_u8g2, u8g2_font_tallpixelextended_tr);
        u8g2_DrawUTF8(&m_u8g2, 0, 32, "Settings (stub)");
    }

    void drawScrollUTF8(const char *text, int y, uint32_t scrollStart, int tw) {
        if (tw <= 128) { u8g2_DrawUTF8(&m_u8g2, 0, y, text); return; }

        uint32_t elapsed = nowMs() - scrollStart;
        if (elapsed < SCROLL_PAUSE_MS) { u8g2_DrawUTF8(&m_u8g2, 0, y, text); return; }

        uint32_t tScroll = elapsed - SCROLL_PAUSE_MS;
        int period = tw + SCROLL_GAP;
        uint32_t scrollDuration = (period * 1000) / SCROLL_SPEED;
        uint32_t cycleTime = scrollDuration + SCROLL_PAUSE_MS;
        uint32_t timeInCycle = tScroll % cycleTime;

        if (timeInCycle < scrollDuration) {
            uint32_t offset = (timeInCycle * SCROLL_SPEED) / 1000;
            u8g2_DrawUTF8(&m_u8g2, -(int)offset, y, text);
            u8g2_DrawUTF8(&m_u8g2, -(int)offset + period, y, text);
        } else {
            u8g2_DrawUTF8(&m_u8g2, 0, y, text);
        }
    }

    void drawFft() {
        u8g2_SetFont(&m_u8g2, u8g2_font_5x7_tf);
        u8g2_DrawStr(&m_u8g2, 0, 7, "FFT");
        int16_t snap[FFT_SAMPLES];
        AudioTap::instance().snapshot(snap, FFT_SAMPLES, AudioTap::instance().pos() - FFT_SAMPLES);
        for (int i = 0; i < FFT_SAMPLES; i++) {
            m_fftBuf[2 * i]     = (float)snap[i] * m_hamming[i];
            m_fftBuf[2 * i + 1] = 0.0f;
        }

        dsps_fft2r_fc32(m_fftBuf, FFT_SAMPLES);
        dsps_bit_rev_fc32(m_fftBuf, FFT_SAMPLES);

        int barBinCount[NUM_BARS] = {};
        float barMag[NUM_BARS]    = {};
        for (int k = 1; k < NYQUIST_BINS; k++) {
            int b = (int8_t)binToBar[k - 1];
            float re = m_fftBuf[2 * k], im = m_fftBuf[2 * k + 1];
            barMag[b] += sqrtf(re * re + im * im);
            barBinCount[b] += 1;
        }

        const float DB_FLOOR = -60.0f;
        const float FFT_REF  = 32768.0f * FFT_SAMPLES / 4.0f;
        for (int b = 0; b < NUM_BARS; b++) {
            if (barBinCount[b] > 0) {
                barMag[b] /= barBinCount[b];
                barMag[b] = barMag[b] > 0 ? 20.0f * log10f(barMag[b] / FFT_REF) : DB_FLOOR;
                barMag[b] = barMag[b] < DB_FLOOR ? DB_FLOOR : barMag[b];
            } else {
                barMag[b] = DB_FLOOR;
            }
        }

        const int BAR_AREA = 54;
        const int BAR_WIDTH = 128 / NUM_BARS;
        const int START_X = (128 - NUM_BARS * BAR_WIDTH) / 2;
        const int BAR_DECAY = 4, PEAK_DECAY = 2;
        const uint32_t PEAK_HOLD_MS = 750;
        uint32_t now = nowMs();

        for (int b = 0; b < NUM_BARS; b++) {
            float norm = iclamp((int)(((barMag[b] - DB_FLOOR) / -DB_FLOOR) * BAR_AREA), 0, BAR_AREA);
            int target = (int)norm;

            if (target >= m_barHeight[b]) m_barHeight[b] = target;
            else m_barHeight[b] = iclamp(m_barHeight[b] - BAR_DECAY, 0, BAR_AREA);

            if (target >= m_barPeakHold[b]) { m_barPeakHold[b] = target; m_barPeakMs[b] = now; }
            else if (now - m_barPeakMs[b] > PEAK_HOLD_MS)
                m_barPeakHold[b] = iclamp(m_barPeakHold[b] - PEAK_DECAY, 0, BAR_AREA);

            int h = m_barHeight[b], peak = m_barPeakHold[b];
            if (h > 0)    u8g2_DrawBox(&m_u8g2, START_X + b * BAR_WIDTH, 63 - h, BAR_WIDTH - 1, h);
            if (peak > h) u8g2_DrawHLine(&m_u8g2, START_X + b * BAR_WIDTH, 63 - peak, BAR_WIDTH - 1);
        }
    }

    void drawWaveform() {
        u8g2_SetFont(&m_u8g2, u8g2_font_5x7_tf);
        u8g2_DrawStr(&m_u8g2, 0, 7, "WAVE");

        int16_t snap[128];
        int step = (int)((AudioTap::BUF_LEN / 128) * WAVE_ZOOM);
        if (step < 1) step = 1;
        for (int i = 0; i < 128; i++) {
            int16_t v;
            AudioTap::instance().snapshot(&v, 1, AudioTap::instance().pos() + (uint32_t)(i * step));
            snap[i] = v;
        }

        const int TOP = 9, BOTTOM = 63;
        const int MID = (TOP + BOTTOM) / 2, HALF = (BOTTOM - TOP) / 2;
        const float REF = 32768.0f;

        for (int x = 0; x < 128; x++) {
            int y = MID - (int)((snap[x] / REF) * HALF);
            y = iclamp(y, TOP, BOTTOM);
            u8g2_DrawPixel(&m_u8g2, x, y);
        }
    }
};