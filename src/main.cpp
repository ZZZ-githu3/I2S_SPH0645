#include <Arduino.h>
#include <driver/i2s.h>
#include <soc/i2s_reg.h>        // ← thêm header đúng cho S3
#include <Adafruit_NeoPixel.h>

// ─── NeoPixel ────────────────────────────────────────────────
#define PIXEL_PIN         48
#define PIXEL_COUNT       1
#define PIXEL_BRIGHTNESS  50

Adafruit_NeoPixel pixel(PIXEL_COUNT, PIXEL_PIN, NEO_GRB + NEO_KHZ800);

// ─── I2S / SPH0645 ───────────────────────────────────────────
#define I2S_PORT         I2S_NUM_0
#define I2S_BCK_PIN      16    // BCLK (Bit Clock)
#define I2S_WS_PIN       5    // LRCL (Word Select)
#define I2S_DIN_PIN      6    // DOUT (Data In)

#define SAMPLE_RATE      16000
#define DMA_BUF_COUNT    4
#define DMA_BUF_LEN      256

// ─── Ngưỡng phát hiện âm thanh ───────────────────────────────
#define NOISE_THRESHOLD  8000  // Giá trị RMS tối thiểu để coi là "có âm thanh". Cần calibrate bằng cách xem output của Serial.printf("RMS: %ld\n", rms); khi có tiếng ồn và khi yên tĩnh.
#define SILENCE_MS       1000  // Thời gian (ms) để tắt LED sau khi không còn âm thanh

// ─── Buffer ──────────────────────────────────────────────────
int32_t i2sBuffer[DMA_BUF_LEN];

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
        .use_apll             = false,
        .tx_desc_auto_clear   = false,
        .fixed_mclk           = 0
    };

    i2s_pin_config_t pins = {
        .mck_io_num   = I2S_PIN_NO_CHANGE,
        .bck_io_num   = I2S_BCK_PIN,
        .ws_io_num    = I2S_WS_PIN,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num  = I2S_DIN_PIN
    };

    ESP_ERROR_CHECK( i2s_driver_install(I2S_PORT, &cfg, 0, NULL) );
    ESP_ERROR_CHECK( i2s_set_pin(I2S_PORT, &pins) );

    // ── Workaround cho ESP32-S3: dùng đúng tên register của S3 ──
    // I2S_RX_TIMING_REG: bit 0 = I2S_RX_BCK_IN_INV (invert BCK cho RX)
    REG_SET_BIT(I2S_RX_TIMING_REG(I2S_PORT), BIT(0));

    // I2S_RX_CONF_REG: bit I2S_RX_MSB_SHIFT_S = Phillips I2S mode
    REG_SET_BIT(I2S_RX_CONF_REG(I2S_PORT), BIT(I2S_RX_MSB_SHIFT_S));

    i2s_zero_dma_buffer(I2S_PORT);

    Serial.printf("[I2S] Init OK — BCK:%d WS:%d DIN:%d @ %dHz\n",
                  I2S_BCK_PIN, I2S_WS_PIN, I2S_DIN_PIN, SAMPLE_RATE);
}

// ─────────────────────────────────────────────────────────────
int32_t computeRMS(int32_t *buf, size_t count) {
    int64_t sum = 0;
    for (size_t i = 0; i < count; i++) {
        int32_t sample = buf[i] >> 14;
        sum += (int64_t)sample * sample;
    }
    return (int32_t)sqrt((double)sum / count);
}

// ─────────────────────────────────────────────────────────────
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
    Serial.println("[BOOT] Ready. Listening...");
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

    Serial.write((uint8_t*)i2sBuffer, bytesRead);

    size_t samplesRead = bytesRead / sizeof(int32_t);
    int32_t rms = computeRMS(i2sBuffer, samplesRead);

    // Bỏ comment dòng dưới để calibrate NOISE_THRESHOLD:
    Serial.printf("RMS: %ld\n", rms);

    uint32_t now = millis();

    if (rms > NOISE_THRESHOLD) {
        lastSoundTime = now;
        if (!ledOn) {
            ledOn = true;
            Serial.printf("[SOUND] RMS=%ld → LED ON\n", rms);
        }
        uint8_t intensity = (uint8_t)constrain(
            map(rms, NOISE_THRESHOLD, 50000, 0, 255), 0, 255
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