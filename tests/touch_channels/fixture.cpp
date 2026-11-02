#include <iostream>
#include <stdexcept>
#include <string>
#include "Touch.cpp"

Reaction reaction = R_NONE;
bool pendingSleepVisual = false;
static int events = 0;
void handlePetEvent(uint32_t) { events++; }

static void require(bool condition, const std::string &message) {
  if (!condition) throw std::runtime_error(message);
}

static void fresh() {
  resetPetDetector();
  reaction = R_NONE;
  pendingSleepVisual = false;
  Adafruit_MPR121::mask = 0;
  events = 0;
  Serial.output.clear();
}

static void sample(uint16_t mask, uint32_t now) {
  Adafruit_MPR121::mask = mask;
  updateTouch(now);
}

static void qualifiedPair(uint16_t mask, const std::string &name) {
  fresh();
  sample(mask, 1000);
  sample(mask, 1019);
  require(events == 0, name + " triggered before 20 ms confirmation");
  sample(mask, 1020);
  require(events == 1, name + " did not qualify at 20 ms");
}

int main(int argc, char **argv) {
  try {
    require(argc == 2, "Choose a fixture case");
    std::string mode = argv[1];
    if (mode == "pairs") {
      qualifiedPair((1U << 1) | (1U << 2), "E1+E2");
      for (int first = 0; first <= 6; first++) {
        for (int second = first + 1; second <= 6; second++) {
          qualifiedPair((1U << first) | (1U << second),
                        "E" + std::to_string(first) + "+E" + std::to_string(second));
        }
      }
    } else if (mode == "single-unused") {
      for (int channel = 0; channel <= 11; channel++) {
        fresh();
        sample(1U << channel, 1000);
        sample(1U << channel, 1020);
        sample(1U << channel, 2000);
        require(events == 0, "Single channel must not trigger PET");
      }
      fresh();
      sample(0xF80, 1000);
      sample(0xF80, 1020);
      require(events == 0, "Unused E7..E11 must not trigger PET");
      fresh();
      sample((1U << 0) | (1U << 11), 1000);
      sample((1U << 0) | (1U << 11), 1020);
      require(events == 0, "E0 plus unused E11 must not qualify");
    } else if (mode == "hold-rearm") {
      qualifiedPair(0x7F, "All seven electrodes");
      sample(0x7F, 2500);
      require(events == 1, "Continuous hold retriggered PET");
      // Release after the refractory period; Issue #22 remains separate work.
      sample(1U << 3, 2510);
      sample(1U << 3, 2729);
      require(petGestureLocked, "Rearmed before the 220 ms release confirmation");
      sample(1U << 3, 2730);
      require(!petGestureLocked, "One residual electrode blocked rearm");
      sample((1U << 1) | (1U << 5), 2800);
      sample((1U << 1) | (1U << 5), 2820);
      require(events == 2, "New confirmed touch did not fire after rearm");
    } else if (mode == "sleep") {
      fresh();
      reaction = R_SLEEP;
      sample(0x7F, 1000);
      sample(0x7F, 1020);
      require(events == 0, "PET triggered during Sleep");
      reaction = R_NONE;
      pendingSleepVisual = true;
      sample(0x7F, 1100);
      sample(0x7F, 1120);
      require(events == 0, "PET triggered during pending Sleep");
      pendingSleepVisual = false;
      sample(0x7F, 1200);
      sample(0x7F, 1220);
      require(events == 1, "PET did not recover after Sleep suppression ended");
    } else if (mode == "diagnostics") {
      fresh();
      Adafruit_MPR121::mask = 0x7F;
      printMprDiagnostics();
      for (int channel = 0; channel <= 6; channel++) {
        require(Serial.output.find("E" + std::to_string(channel) + " baseline=") != std::string::npos,
                "Missing diagnostic electrode E" + std::to_string(channel));
      }
      require(Serial.output.find("E11 baseline=") == std::string::npos,
              "Unused E11 remains in diagnostics");
      require(Serial.output.find("touchedCount=7") != std::string::npos,
              "Diagnostics do not count all seven touches");
      qualifiedPair((1U << 1) | (1U << 5), "E1+E5");
      require(Serial.output.find("E1+E5") != std::string::npos,
              "PET event does not identify the actual electrodes");
    } else if (mode == "startup") {
      fresh();
      setupTouch();
      require(Wire.sda == 8 && Wire.scl == 9, "MPR electrode wiring changed ESP32 I2C GPIOs");
      require(Adafruit_MPR121::address == 0x5A && Adafruit_MPR121::bus == &Wire,
              "MPR address or bus changed");
      require(Adafruit_MPR121::touchThreshold == 4 && Adafruit_MPR121::releaseThreshold == 2 &&
              Adafruit_MPR121::autoconfig, "Experimental startup thresholds were not applied");
      Adafruit_MPR121::touchThreshold = 99;
      Adafruit_MPR121::releaseThreshold = 98;
      require(recalibrateMpr121(1000), "Manual recalibration failed");
      require(Adafruit_MPR121::touchThreshold == 4 && Adafruit_MPR121::releaseThreshold == 2 &&
              Adafruit_MPR121::autoconfig, "Manual recalibration lost the trial thresholds");
    } else {
      throw std::runtime_error("Unknown fixture case");
    }
    std::cout << mode << " passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
