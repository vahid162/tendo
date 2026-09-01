# Changelog

All published Tando firmware versions are recorded here in newest-first order.

## v0.7.2-rc.4

Pre-release diagnostic/startup refinement for persistent MPR121 touch states.

### Changed
- Initializes RC522 and NVS before MPR121 so capacitive calibration occurs after the rest of the board has reached its normal powered state.
- Adds an 800 ms board-settle interval before MPR121 initialization.
- Starts MPR121 with autoconfiguration disabled, immediately returns it to Stop Mode, then applies the final sensing configuration while stopped.
- Enables autoconfiguration only for the final Stop -> Run transition.
- Runs only the three physical PET electrodes E0/E1/E2 instead of leaving all 12 MPR121 electrodes enabled.
- Adds a short 250 ms post-start settling interval before normal interaction polling.
- Adds the live MPR121 ECR value and active-electrode count to the `t` diagnostic output.

### Unchanged
- MPR121 thresholds remain Touch=6 / Release=3.
- PET remains a 2-of-3 electrode gesture with about 1 second of accumulated fresh capacitive presence.
- Residual-electrode PET re-arm behavior from v0.7.2-rc.2 is retained.

### Validation
- Static source/delimiter checks passed.
- Runtime ECR is expected to read `0x83` for E0/E1/E2-only operation.
- Hardware validation is required to determine whether persistent YES states are caused by startup/calibration versus enclosure/electrode coupling.

## v0.7.2-rc.3

Pre-release fix for MPR121 startup configuration and calibration order.

### Fixed
- Passes the final Touch=6 / Release=3 thresholds directly to `Adafruit_MPR121::begin()`.
- Enables MPR121 autoconfiguration during `begin()` instead of enabling it only after the sensor has already entered Run Mode.
- Removes the redundant post-`begin()` threshold/autoconfig writes and their extra Stop/Run transitions.
- Corrected the startup comment to reflect the current >=1 second PET rule.

### Unchanged
- PET remains a 2-of-3 electrode gesture.
- PET minimum capacitive presence remains about 1 second.
- MPR121 thresholds remain Touch=6 / Release=3.
- Residual-electrode re-arm behavior from v0.7.2-rc.2 is retained.

### Validation
- Verified the call matches the current Adafruit MPR121 `begin(address, Wire, touchThreshold, releaseThreshold, autoconfig)` API.
- Static source/delimiter checks passed.
- Hardware validation is required to determine whether the always-YES startup electrode issue is resolved.

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

