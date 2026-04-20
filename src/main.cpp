#include <Arduino.h>
#include <driver/i2s.h>
#include <soc/i2s_reg.h>
#include <Adafruit_NeoPixel.h>

// ─── NeoPixel ────────────────────────────────────────────────
#define PIXEL_PIN           48
#define PIXEL_COUNT          1
#define PIXEL_BRIGHTNESS    50

Adafruit_NeoPixel pixel(PIXEL_COUNT, PIXEL_PIN, NEO_GRB + NEO_KHZ800);

// ─── SPH0645 pin mapping ─────────────────────────────────────
#define I2S_PORT        I2S_NUM_0
#define SPH0645_BCLK    16
#define SPH0645_LRCL     5
#define SPH0645_DOUT     6

// ─── Audio config ────────────────────────────────────────────
#define SAMPLE_RATE     16000
#define DMA_BUF_COUNT       4   // tăng để giảm dropout
#define DMA_BUF_LEN      1024   // tăng để giảm dropout

// ─── Threshold ───────────────────────────────────────────────
#define NOISE_THRESHOLD  2000
#define SILENCE_MS       1000

// ─── Chế độ hoạt động ────────────────────────────────────────
// true  = ghi âm: gửi binary PCM ra Serial (dùng với record.py)
// false = monitor: in RMS ra Serial Monitor
#define RECORDING_MODE  true

// ─── Buffer ──────────────────────────────────────────────────
int32_t  i2sBuffer[DMA_BUF_LEN];
int16_t  pcmBuffer[DMA_BUF_LEN];

uint32_t lastSoundTime = 0;
bool     ledOn         = false;

// ─────────────────────────────────────────────────────────────
void i2s_init() {
    i2s_config_t cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate          = SAMPLE_RATE,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = DMA_BUF_COUNT,
        .dma_buf_len          = DMA_BUF_LEN,
        .use_apll             = false,   // clock chính xác hơn
        .tx_desc_auto_clear   = false,
        .fixed_mclk           = 0
    };

    i2s_pin_config_t pins = {
        .mck_io_num   = I2S_PIN_NO_CHANGE,
        .bck_io_num   = SPH0645_BCLK,
        .ws_io_num    = SPH0645_LRCL,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num  = SPH0645_DOUT
    };

    ESP_ERROR_CHECK( i2s_driver_install(I2S_PORT, &cfg, 0, NULL) );
    ESP_ERROR_CHECK( i2s_set_pin(I2S_PORT, &pins) );

    REG_SET_BIT(I2S_RX_TIMING_REG(I2S_PORT), BIT(9));
    REG_SET_BIT(I2S_RX_CONF_REG(I2S_PORT),   I2S_RX_MSB_SHIFT);

    i2s_zero_dma_buffer(I2S_PORT);
}

// ─────────────────────────────────────────────────────────────
// SPH0645: 18-bit data nằm ở bit[31:14]
// >> 16 → lấy 16 bit trên = int16 PCM chuẩn, không bị clip
// ─────────────────────────────────────────────────────────────
inline int16_t toInt16(int32_t raw) {
    return (int16_t)(raw >> 16);
}

int32_t computeRMS(int16_t *buf, size_t count) {
    int64_t sum = 0;
    for (size_t i = 0; i < count; i++) {
        sum += (int64_t)buf[i] * buf[i];
    }
    return (int32_t)sqrt((double)sum / count);
}

void setPixelColor(uint8_t r, uint8_t g, uint8_t b) {
    pixel.setPixelColor(0, pixel.Color(r, g, b));
    pixel.show();
}

// ─────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);

    pixel.begin();
    pixel.setBrightness(PIXEL_BRIGHTNESS);
    setPixelColor(0, 0, 0);

    i2s_init();

    if (!RECORDING_MODE) {
        Serial.println("[BOOT] Ready — MONITOR mode");
    }
    // RECORDING_MODE: không in gì để Serial hoàn toàn sạch binary
}

// ─────────────────────────────────────────────────────────────
void loop() {
    size_t bytesRead = 0;

    esp_err_t err = i2s_read(
        I2S_PORT,
        i2sBuffer,
        sizeof(i2sBuffer),
        &bytesRead,
        pdMS_TO_TICKS(100)
    );

    if (err != ESP_OK || bytesRead == 0) return;

    size_t samplesRead = bytesRead / sizeof(int32_t);

    // Chuyển đổi toàn bộ buffer → int16 PCM
    for (size_t i = 0; i < samplesRead; i++) {
        pcmBuffer[i] = toInt16(i2sBuffer[i]);
    }

    if (RECORDING_MODE) {
        // ── Chế độ ghi âm: CHỈ gửi binary, không in text ──
        Serial.write((uint8_t*)pcmBuffer, samplesRead * sizeof(int16_t));

    } else {
        // ── Chế độ monitor: chỉ in RMS text, không gửi binary ──
        int32_t rms = computeRMS(pcmBuffer, samplesRead);
        Serial.printf("RMS: %ld\n", rms);

        uint32_t now = millis();

        if (rms > NOISE_THRESHOLD) {
            lastSoundTime = now;
            if (!ledOn) {
                ledOn = true;
                Serial.printf("[SOUND] RMS=%ld → LED ON\n", rms);
            }
            uint8_t intensity = (uint8_t)constrain(
                map(rms, NOISE_THRESHOLD, 10000, 0, 255), 0, 255
            );
            setPixelColor(intensity, 200 - intensity, 50);

        } else {
            if (ledOn && (now - lastSoundTime > SILENCE_MS)) {
                ledOn = false;
                setPixelColor(0, 0, 0);
                Serial.println("[SILENCE] LED OFF");
            }
        }
    }
}