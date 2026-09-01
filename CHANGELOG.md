# Changelog

All published Tando firmware versions are recorded here in newest-first order.

## v0.7.2-rc.2

Pre-release fix for repeat PET detection after residual capacitive touch.

### Fixed
- Prevented a residual MPR121 electrode from immediately starting a phantom PET session after a successful PET.
- A residual electrode is now start-blocked until it physically releases.
- A genuinely new electrode can start the next PET even while one old electrode remains capacitively active.
- The residual electrode may still serve as the second PET zone after a fresh gesture starts, so repeated E0+E1 petting remains possible.
- Stale-session full-release logic now ignores previously blocked residual channels, preventing a stuck channel from deadlocking PET.
- Added PET lock/full-release/start-block state to the `t` diagnostic output.

### Unchanged
- PET minimum capacitive presence remains about 1 second.
- MPR121 thresholds remain Touch=6 / Release=3.

### Validation
- Source/state-machine logic and static delimiter checks were reviewed.
- Hardware validation is required to confirm repeat PET behavior on the target enclosure.

## v0.7.2-rc.1

Pre-release baseline for the new `develop` release channel.

### Changed
- Established the permanent Stable / Pre-release branch workflow.
- Added automated immutable GitHub Release publishing for `main` and `develop`.
- No intentional runtime behavior change from v0.7.1 other than the firmware version identifier.

### Validation
- Intended for review and hardware validation before promotion to the next Stable release.

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

