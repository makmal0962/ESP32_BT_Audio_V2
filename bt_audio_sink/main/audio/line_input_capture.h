#pragma once
#include "esp_adc/adc_continuous.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "../display/audio_tap.h"

class LineInputCapture {
public:
    static void init() {
        adc_continuous_handle_cfg_t handleCfg = {
            .max_store_buf_size = 1024,
            .conv_frame_size = 256,
        };
        esp_err_t err = adc_continuous_new_handle(&handleCfg, &s_handle);
        if (err != ESP_OK) { ESP_LOGE("LineInput", "new_handle failed: %s", esp_err_to_name(err)); return; }

        adc_digi_pattern_config_t pattern = {};
        pattern.atten = ADC_ATTEN_DB_12;
        pattern.channel = ADC_CHANNEL_7;   // GPIO35
        pattern.unit = ADC_UNIT_1;
        pattern.bit_width = ADC_BITWIDTH_12;

        adc_continuous_config_t digCfg = {};
        digCfg.pattern_num = 1;
        digCfg.adc_pattern = &pattern;
        digCfg.sample_freq_hz = 44100;
        digCfg.conv_mode = ADC_CONV_SINGLE_UNIT_1;
        digCfg.format = ADC_DIGI_OUTPUT_FORMAT_TYPE1;   // if this errors, try TYPE2 instead

        err = adc_continuous_config(s_handle, &digCfg);
        if (err != ESP_OK) { ESP_LOGE("LineInput", "config failed: %s", esp_err_to_name(err)); return; }

        err = adc_continuous_start(s_handle);
        if (err != ESP_OK) { ESP_LOGE("LineInput", "start failed: %s", esp_err_to_name(err)); return; }

        xTaskCreatePinnedToCore(captureTask, "line_capture", 3072, nullptr, 4, nullptr, 1);
        ESP_LOGI("LineInput", "ADC continuous capture started on GPIO35");
    }

private:
    static inline adc_continuous_handle_t s_handle = nullptr;

    static void captureTask(void*) {
        adc_continuous_data_t samples[128];
        int32_t dcAvg = 0;
        int16_t lpPrev = 0;
        uint32_t logCounter = 0;

        for (;;) {
            uint32_t numSamples = 0;
            esp_err_t err = adc_continuous_read_parse(s_handle, samples, 128, &numSamples, pdMS_TO_TICKS(20));

            // if (++logCounter % 50 == 0) {   // roughly once a second at 20ms timeout
            //     ESP_LOGI("LineInput", "read_parse err=%s numSamples=%u firstRaw=%u",
            //             esp_err_to_name(err), (unsigned)numSamples,
            //             numSamples > 0 ? samples[0].raw_data : 0);
            // }

            if (err != ESP_OK) continue;

            for (uint32_t i = 0; i < numSamples; i++) {
                if (!samples[i].valid) continue;
                int16_t raw = ((int16_t)samples[i].raw_data - 2048) << 4;
                dcAvg += (raw - dcAvg) >> 7;
                int16_t dcRemoved = raw - (int16_t)dcAvg;
                int16_t filtered = (int16_t)(0.4f * dcRemoved + 0.6f * lpPrev);
                lpPrev = filtered;
                int32_t val = (int32_t)filtered * 3 / 2;
                val = val > INT16_MAX ? INT16_MAX : (val < INT16_MIN ? INT16_MIN : val);
                AudioTap::instance().onFrame((int32_t)val << 16, (int32_t)val << 16);
                // if (++logCounter % 4096 == 0) {   // roughly once a second at 44.1kHz
                //     ESP_LOGI("LineInput", "raw=%u processed_val=%ld", samples[i].raw_data, (long)val);
                // }
            }
        }
    }
};  