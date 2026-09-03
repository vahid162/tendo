# Changelog

All published Tando firmware versions are recorded here in newest-first order.

## v0.8.0-rc.2

Pre-release metadata correction for the 30-minute autonomous personality demo.

### Fixed
- Corrected the firmware header comment to say that visual progress is auto-filled at each 10-minute boundary, matching the actual 10/20/30-minute stage timing.

### Unchanged
- All v0.8.0-rc.1 runtime behavior is unchanged: 30-minute Active Demo Time, 3 x 10-minute stages, autonomous Look/Wink/Smile/Play/Hunger scheduler, PET/FOOD/SLEEP progress rules, RFID UIDs, MPR121 behavior, Sleep priority, LED behavior, and NVS state version 4.

### Validation
- Static delimiter, duplicate-function, version-consistency, timing-constant, and stale-timing-string checks passed.
- Compilation, flashing, and real hardware validation are still required.

## v0.8.0-rc.1

Pre-release feature update for the redesigned 30-minute Tando demo and autonomous eye personality.

### Added
- Added an autonomous personality scheduler that is independent of Progress and interaction LED feedback.
- Added five spontaneous eye behavior families: Look Around, one-eye Wink, Eye Smile, Play Invite, and Hunger.
- Added constrained-random timing with 8-20 s, 20-55 s, and 55-120 s delay classes and per-cycle timing-weight jitter.
- Added weighted behavior selection with per-cycle priority jitter and recent-history suppression across the last three autonomous events.
- Added Stage-dependent personality intensity so Stage 2 and Stage 3 use richer Smile/Glow/Play behavior while preserving the same eye identity.
- Added contextual Play Invite weighting that increases during longer quiet periods without using a deterministic timeout.
- Added 90-180 s randomized Hunger suppression after any recognized FOOD interaction.
- Added serial test commands for each new autonomous behavior: `l/w/e/g/h`.
- Added per-eye autonomous lid control so Wink is visually distinct from the normal two-eye Blink.

### Changed
- Demo Active Time increased from 15 minutes to 30 minutes.
- Each Stage increased from 5 minutes to 10 minutes: 0-10, 10-20, and 20-30 minutes.
- Time-gated progress guarantees now occur at 10 minutes (minimum 3/9), 20 minutes (minimum 6/9), and 30 minutes (9/9 Completion).
- User and system interactions now discard any active autonomous personality visual immediately; autonomous visuals are never queued behind user reactions.
- Auto Blink is suppressed while a major autonomous personality expression owns the eyes, then receives a fresh schedule afterward.
- NVS state version increased from 3 to 4 so old 15-minute timing/completion state is reset instead of being silently reinterpreted under the new 30-minute timing model.
- Startup banner now reports the 30-minute demo.

### Preserved
- PET remains on MPR121 E0/E6/E11 with 2-of-3 qualification, the same timing, thresholds, and residual-electrode protections.
- FOOD1, FOOD2, and SLEEP RFID UIDs are unchanged.
- PET / FOOD / SLEEP still provide at most one Progress credit each per Stage, for 9 total credits.
- Persistent SLEEP, Wake timing, System-over-Sleep priority, neutral reaction handoff, Progress Ring behavior, LED pulse semantics, and NVS checkpoint cadence remain intact.
- Autonomous personality events create no Progress, no interaction LED pulse, and do not count as user activity.

### Validation
- Static delimiter balance, duplicate-function scan, version consistency, timing-constant checks, autonomous-state references, and documentation synchronization were checked before commit.
- Compilation, flashing, and real hardware validation are still required.

## v0.7.2-rc.8

Pre-release eye-motion and reaction-state refinement based on static scenario analysis of rc.7.

### Fixed
- System Stage Unlock and Completion now preempt the persistent SLEEP visual instead of waiting indefinitely for the SLEEP tag to be removed.
- If the physical SLEEP tag remains present after a system event, SLEEP resumes after the system animation completes.
- Auto Blink is rescheduled after Wake and other completed reactions so an expired blink timer cannot fire immediately when the eyes reopen.
- Progress Ring color is tied to the active unlock reaction during `R_UNLOCK2` / `R_UNLOCK3`, preventing delayed Stage 2 unlocks from being shown with the Stage 3 color.
- Added a 240 ms neutral reaction handoff so strong Happy/Surprise/Glow blend values from one user reaction do not leak into the next queued reaction.

### Refined
- Normal Blink now uses top-lid-dominant closure instead of collapsing the eye equally from top and bottom.
- Unknown RFID changed from a high-frequency opposing-eye shake to a slower shared curious sway.
- Unknown RFID reaction duration increased from 950 ms to 1400 ms to match the slower motion.
- FOOD chewing bounce slowed from 0.020 rad/ms at 2 px amplitude to 0.009 rad/ms at 1.8 px amplitude.
- Stage 2 idle target cadence changed from 700-1600 ms to 850-1750 ms.
- Stage 3 idle target cadence changed from 550-1350 ms to 750-1600 ms.
- Corrected the render-loop comment to match the existing ~29 FPS frame limit.

### Unchanged
- PET remains on MPR121 E0/E6/E11 with 2-of-3 qualification.
- PET timing and MPR121 thresholds remain unchanged.
- RFID UIDs, pin mapping, NVS format, progress-credit rules and 15-minute Demo timing are unchanged.

### Validation
- Source-level state transitions and timing paths were reviewed.
- Static delimiter, duplicate-function and version-consistency checks passed before merge.
- Compilation, flashing and real hardware eye-motion validation are still required.

## v0.7.2-rc.7

Pre-release hardware A/B test that moves the three PET zones from adjacent MPR121 channels E0/E1/E2 to widely separated channels E0/E6/E11.

### Changed
- PET zone A remains on MPR121 E0.
- PET zone B moves from E1 to E6.
- PET zone C moves from E2 to E11.
- All 2-of-3 gesture logic, residual-electrode protection, timing, thresholds and manual recalibration behavior are preserved.
- `t` diagnostics now read and print the actual PET channels E0/E6/E11 instead of E0/E1/E2.
- PET serial logs now identify E0/E6/E11 correctly.

### Unchanged
- MPR121 startup remains on the known-better rc.3-style path.
- Touch / release thresholds remain 6 / 3.
- PET requires any two distinct configured electrodes and about 1 second of accumulated fresh capacitive presence.
- No automatic baseline recovery or automatic recalibration was added.

### Validation
- Verified the PET mask is the 12-bit combination of MPR121 bits 0, 6 and 11.
- Verified diagnostic reads use channels 0, 6 and 11 directly.
- Static source/delimiter checks passed.
- Hardware validation is required to determine whether separating the MPR121 channels improves drift/coupling behavior.

## v0.7.2-rc.6

Pre-release rollback to the known-better rc.3 MPR121 sensing behavior, with conservative manual recalibration only.

### Changed
- Removed the rc.5 `NHDT/NCLT/FDLT = 4/4/4` touched-baseline filter experiment because hardware testing showed it could suppress normal capacitive touch detection.
- Restored the MPR121 startup/sensing behavior to the rc.3 path: `mpr.begin(..., Touch=6, Release=3, autoconfig=true)` with no extra touched-filter writes.
- Kept Serial command `c`, but it now performs only an explicit manual recalibration using the same rc.3 configuration.
- Manual recalibration waits 600 ms before reset and 300 ms afterward, clears PET transient state, and never runs automatically.
- `t` still reports ECR and `NHDT/NCLT/FDLT` values for observation only.

### Unchanged
- PET remains 2-of-3 with about 1 second of accumulated fresh capacitive presence.
- Residual-electrode PET re-arm behavior is retained.
- MPR121 thresholds remain Touch=6 / Release=3.

### Validation
- Verified no rc.4 `ECR=0x83` experiment remains.
- Verified no rc.5 touched-filter writes remain.
- Static source/delimiter checks passed.
- Hardware validation is still required for persistent YES behavior after touch.

## v0.7.2-rc.5

Pre-release rollback/refinement for persistent MPR121 touched states.

### Changed
- Reverted the rc.4 MPR121 startup experiment back to the better-performing rc.3 initialization path.
- Removed the rc.4 delayed peripheral ordering and E0/E1/E2-only `ECR=0x83` run mode; the Adafruit library's normal run configuration is restored.
- Added controlled touched-state baseline recovery via MPR121 registers `0x33..0x35` (`NHDT/NCLT/FDLT = 4/4/4`) so a channel that remains reported as touched can slowly rejoin its filtered baseline instead of staying latched indefinitely.
- Added Serial command `c` to force an MPR121 recalibration while the user's hand is away.
- Extended `t` diagnostics to show ECR and the live touched-baseline-filter register values.

### Unchanged
- MPR121 touch/release thresholds remain 6/3.
- PET remains 2-of-3 with about 1 second of accumulated fresh capacitive presence.
- Residual-electrode PET re-arm behavior is retained.

### Validation
- The rc.4-specific three-electrode startup path was removed.
- Static source/delimiter checks passed.
- This is intentionally an experimental hardware-validation build; the 4/4/4 touched-filter timing may need further tuning from real enclosure measurements.

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

