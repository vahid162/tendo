#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <driver/spi_master.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <string.h>

// Retain only the transport recovery flag through a software restart. Demo/NVS
// state is untouched. A normal power-on clears this flag in begin().
static RTC_NOINIT_ATTR uint32_t tandoDmaFallbackMagic;

// The Arduino SPI driver still initializes both panels. Once that is complete,
// this simulation-only transport owns the bus and sends whole DMA chunks.
class TandoDmaDisplay {
 public:
  static constexpr size_t MaxTransfer = 4096;

  TandoDmaDisplay(int sclk, int mosi, int leftCs, int rightCs,
                  int leftDc, int rightDc)
      : sclk_(sclk), mosi_(mosi), leftCs_(leftCs), rightCs_(rightCs),
        leftDc_(leftDc), rightDc_(rightDc) {}

  bool active() const { return active_; }

  bool begin(SPIClass &legacy) {
    if (active_) return true;
    if (esp_reset_reason() != ESP_RST_SW) tandoDmaFallbackMagic = 0;
    if (tandoDmaFallbackMagic == FallbackMagic) {
      Serial.println("SIM: DMA recovery uses legacy display transport");
      return false;
    }
    staging_ = static_cast<uint8_t *>(
        heap_caps_malloc(MaxTransfer, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (!staging_) {
      Serial.println("SIM: DMA buffer unavailable; using legacy display transport");
      return false;
    }

    // Arduino HSPI is SPI3 on ESP32-S3. Do not leave two drivers attached to it.
    legacy.end();
    spi_bus_config_t bus = {};
    bus.mosi_io_num = mosi_;
    bus.miso_io_num = -1;
    bus.sclk_io_num = sclk_;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.data4_io_num = -1;
    bus.data5_io_num = -1;
    bus.data6_io_num = -1;
    bus.data7_io_num = -1;
    bus.max_transfer_sz = MaxTransfer;
    esp_err_t error = spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO);
    bool busInstalled = error == ESP_OK;
    if (busInstalled) {
      spi_device_interface_config_t device = {};
      device.clock_speed_hz = 40000000;
      device.mode = 0;
      device.spics_io_num = -1;  // Both panels retain their existing manual CS.
      device.flags = SPI_DEVICE_HALFDUPLEX;
      device.queue_size = 1;
      error = spi_bus_add_device(SPI3_HOST, &device, &device_);
    }
    if (error != ESP_OK) {
      if (busInstalled) {
        esp_err_t released = spi_bus_free(SPI3_HOST);
        if (released != ESP_OK) return restartLegacy(released);
      }
      heap_caps_free(staging_);
      staging_ = nullptr;
      legacy.begin(sclk_, -1, mosi_, -1);
      Serial.printf("SIM: DMA initialization failed (%d); using legacy display transport\r\n", error);
      return false;
    }
    active_ = true;
    digitalWrite(leftCs_, HIGH);
    digitalWrite(rightCs_, HIGH);
    Serial.println("SIM: display transport=DMA chunks=4096 SPI=40000000");
    return true;
  }

  // source points to the top-left pixel of the region, stride is in pixels,
  // and screenX/screenY already include the canvas offset on the physical LCD.
  bool writeRegion(bool left, const uint16_t *source, uint16_t stride,
                   uint16_t screenX, uint16_t screenY,
                   uint16_t width, uint16_t height) {
    if (!active_ || !source || !width || !height || stride < width ||
        uint32_t(screenX) + width > 240 || uint32_t(screenY) + height > 240) {
      return false;
    }
    // This is the bus's sole device and sole caller. Manual CS remains stable
    // between synchronous transfers; no bus lock or CS_KEEP_ACTIVE is needed.
    int cs = left ? leftCs_ : rightCs_;
    int dc = left ? leftDc_ : rightDc_;
    digitalWrite(leftCs_, HIGH);
    digitalWrite(rightCs_, HIGH);
    digitalWrite(cs, LOW);

    uint16_t endX = screenX + width - 1;
    uint16_t endY = screenY + height - 1;
    uint8_t columns[] = {uint8_t(screenX >> 8), uint8_t(screenX),
                         uint8_t(endX >> 8), uint8_t(endX)};
    uint8_t rows[] = {uint8_t(screenY >> 8), uint8_t(screenY),
                      uint8_t(endY >> 8), uint8_t(endY)};
    if (!command(dc, 0x2a, columns, sizeof(columns)) ||
        !command(dc, 0x2b, rows, sizeof(rows)) ||
        !command(dc, 0x2c, nullptr, 0)) return false;

    // Fuse strided-row packing and RGB565 byte order conversion. No frame/cache
    // buffer is modified, and staging is reused only after its DMA completes.
    uint16_t row = 0, column = 0;
    while (row < height) {
      size_t bytes = 0;
      while (row < height && bytes < MaxTransfer) {
        const uint16_t *pixels = source + size_t(row) * stride + column;
        size_t count = width - column;
        size_t room = (MaxTransfer - bytes) / 2;
        if (count > room) count = room;
        size_t i = 0;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        if ((reinterpret_cast<uintptr_t>(pixels) & 3) == 0 && (bytes & 3) == 0) {
          for (; i + 1 < count; i += 2) {
            uint32_t pair;
            memcpy(&pair, pixels + i, sizeof(pair));
            pair = ((pair & 0x00ff00ffu) << 8) | ((pair & 0xff00ff00u) >> 8);
            memcpy(staging_ + bytes, &pair, sizeof(pair));
            bytes += sizeof(pair);
          }
        }
#endif
        for (; i < count; ++i) {
          uint16_t pixel = pixels[i];
          staging_[bytes++] = uint8_t(pixel >> 8);
          staging_[bytes++] = uint8_t(pixel);
        }
        column += count;
        if (column == width) { column = 0; ++row; }
      }
      if (!transmit(staging_, bytes)) return false;
    }
    digitalWrite(cs, HIGH);
    return true;
  }

 private:
  static constexpr uint32_t FallbackMagic = 0x54444d41;
  int sclk_, mosi_, leftCs_, rightCs_, leftDc_, rightDc_;
  bool active_ = false;
  uint8_t *staging_ = nullptr;
  spi_device_handle_t device_ = nullptr;
  spi_transaction_t transaction_ = {};

  static TickType_t waitTicks() {
    TickType_t ticks = pdMS_TO_TICKS(1000);
    return ticks ? ticks : 1;
  }

  bool restartLegacy(esp_err_t error) {
    // A timed-out DMA may still own transaction_/staging_. Keep both alive and
    // untouched until the restart; freeing/reusing them would corrupt memory.
    tandoDmaFallbackMagic = FallbackMagic;
    active_ = false;
    Serial.printf("SIM: DMA transfer failed (%d); restarting with legacy transport\r\n", error);
    esp_restart();
    return false;
  }

  bool transmit(const uint8_t *data, size_t bytes) {
    memset(&transaction_, 0, sizeof(transaction_));
    transaction_.length = bytes * 8;
    if (bytes <= sizeof(transaction_.tx_data)) {
      transaction_.flags = SPI_TRANS_USE_TXDATA;
      memcpy(transaction_.tx_data, data, bytes);
    } else {
      transaction_.tx_buffer = data;
    }
    esp_err_t error = spi_device_queue_trans(device_, &transaction_, waitTicks());
    if (error != ESP_OK) return restartLegacy(error);
    spi_transaction_t *completed = nullptr;
    error = spi_device_get_trans_result(device_, &completed, waitTicks());
    if (error != ESP_OK || completed != &transaction_) {
      return restartLegacy(error == ESP_OK ? ESP_ERR_INVALID_STATE : error);
    }
    return true;
  }

  bool command(int dc, uint8_t value, const uint8_t *arguments, size_t count) {
    digitalWrite(dc, LOW);
    if (!transmit(&value, 1)) return false;
    digitalWrite(dc, HIGH);
    return !count || transmit(arguments, count);
  }
};
