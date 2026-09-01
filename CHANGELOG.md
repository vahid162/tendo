# Changelog

All published Tando firmware versions are recorded here in newest-first order.

## v0.7.1

Stable release baseline for the current Tando firmware.

### Changed
- Reduced capacitive PET activation time from about 2 seconds to about 1 second.
- Clarified that PET sensing is capacitive and does not require direct electrical contact with the electrode.
- Kept the current MPR121 touch/release thresholds at 6/3 pending enclosure-level hardware calibration.
- Added deterministic reaction queuing so PET/FOOD feedback is not silently lost while another short reaction is running.
- Prevented PET from triggering during persistent SLEEP.
- Fixed Stage 2 / Stage 3 pending unlock overwrite behavior.
- Kept RFID polling responsive during short visual reactions.
- Simplified the reaction LED hardware assumption to a low-current LED on GPIO21 through a suitable series resistor.
- Set the practical display frame target to about 29 FPS for two 200x200 RGB565 SPI transfers.

### Validation
- Source structure and static delimiter checks were reviewed.
- Hardware validation is still required on the target ESP32-S3 assembly.

