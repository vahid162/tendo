# Changelog

All published Tando firmware versions are recorded here in newest-first order.

## v0.10.0-rc.2

Pre-release care-request visual and priority refinement after hardware scenario review.

### Changed
- Reduced Hunger Request quota from 15 to 10 completed 10-second prompts per Stage.
- Reduced PET Request quota from 15 to 10 completed 10-second prompts per Stage.
- Removed the PET Request hand/stroking icon completely.
- Replaced it with a pulsing vector heart plus two animated parenthesis/broadcast waves on each side: `(( heart ))`.
- Kept the existing mild affectionate eye bias during PET Request; the real PET reaction remains visually stronger.
- Serial `r` now previews the 10-second `((HEART))` PET Request alert.

### Fixed
- All real user/system reactions now have strict visual priority over both Care Request overlays.
- FOOD immediately hides an active PET Request; if PET is still unsatisfied, that interrupted request is retried after FOOD reaction.
- PET immediately hides an active Hunger Request; if FOOD is still unsatisfied, that interrupted request is retried after PET reaction.
- SLEEP and Unknown RFID also immediately hide active Care Requests and retry unsatisfied tracked requests later.
- Hunger/PET Request renderers now have explicit `reaction == R_NONE` guards so no Care Request can leak visually into an active real reaction.

### Preserved
- Hunger remains a drumstick overlay on both displays.
- Hunger and PET Request still never overlap each other.
- Shared care-request random timing remains first 8-20 s, later 8-18 s, retry 5-10 s, collision defer 3-8 s.
- First valid FOOD/PET still satisfies its matching Stage need and cancels remaining matching requests for that Stage.
- MPR121 PET detector, protected RFID UIDs, RFID engine, FOOD/PET/SLEEP/Unknown reaction implementations, 30-minute timing, Progress, LED behavior and NVS state version 4 are unchanged except for the intentional event-level Care Request interruption calls.

### Validation
- Static delimiter, duplicate-function, Arduino auto-prototype, version, protected UID, PET-detector, Care Request priority/renderer and documentation checks passed.
- Protected firmware blocks were compared against v0.10.0-rc.1 to detect unrelated changes.
- Arduino compilation and physical TFT validation are still required.

## v0.10.0-rc.1

Pre-release feature update adding Stage-aware PET/Affection Requests alongside Hunger.

### Added
- Added an independent PET Request scheduler with up to 15 completed requests per Stage, 10 seconds each, until PET is credited in that Stage.
- PET Request runs on wall-clock idle time before the first interaction and while Active Demo Time is paused, without starting/resuming the Demo clock.
- Added a soft affection-request visual: normal eyes remain present, gaze is gently biased upward/inward, eyelids soften slightly, and a small animated hand/stroking cue appears at the bottom of both displays.
- Added additive NVS persistence for PET Request Stage/count using `pStage` / `pCount`; NVS state version remains 4.
- Added Serial `r` to preview the 10-second PET Request without consuming Stage quota or Progress.
- Status output now reports PET Request Stage/count/active/satisfied/scheduler state.

### Changed
- Hunger and PET Request now share a compact care-request timing envelope so both 15×10-second need families can fit within a continuously-active 10-minute Stage when neither need is satisfied.
- Shared timing: first request 8-20 s, later requests 8-18 s, interruption retry 5-10 s, collision defer 3-8 s.
- Hunger and PET Request are mutually exclusive visuals. If both are due simultaneously, one gets the slot randomly and the other is deferred briefly.
- A valid PET immediately cancels the current/future PET Request prompts for that Stage before starting the existing PET reward reaction.

### Preserved
- The MPR121 PET detector itself is unchanged: live E0/E6/E11 2-of-3 or 3-of-3, 20 ms confirmation, and <2 re-arm behavior.
- Hunger still uses the same drumstick overlay on both displays and first valid FOOD cancels the rest of that Stage's Hunger prompts.
- Protected RFID UIDs, RFID engine, FOOD/SLEEP/Unknown behavior, 30-minute Demo timing, Progress, LED behavior and NVS state version 4 are unchanged.

### Validation
- Static delimiter, duplicate-function, Arduino auto-prototype, version, protected UID, PET-detector, care-request scheduler and documentation checks passed.
- Protected firmware blocks were compared against v0.9.0-rc.3 to guard against unrelated changes.
- Arduino compilation and physical TFT validation are still required.

## v0.9.0-rc.3

Pre-release fix for Hunger requests not starting while Tando had no prior interaction.

### Fixed
- Removed the `demoStarted` gate from the Hunger scheduler. Hunger now schedules from Boot during Stage 1 even when status shows `Demo started: NO`.
- Hunger continues to use wall-clock time while Active Demo Time is paused for inactivity; autonomous Hunger does not start or resume the Demo clock.
- Fixed the exact hardware case reported after five idle minutes where `completed=0/15`, `foodSatisfied=NO`, but no automatic Hunger request appeared.

### Changed
- Removed the banana renderer.
- Both displays now show the same animated chicken drumstick in the lower display area during Hunger.
- Serial status now reports whether the Hunger scheduler is `ENABLED` or `STOPPED`.
- Serial `h` remains a 10-second preview and does not consume Stage quota.

### Preserved
- 15 completed Hunger requests per Stage, 10 seconds each.
- Random Hunger gaps remain 8-25 seconds before the first prompt and 12-28 seconds between completed prompts.
- First valid FOOD still immediately stops Hunger, gives the existing FOOD Stage credit, and cancels all remaining Hunger requests for that Stage.
- Eyes continue operating normally under the Hunger overlay.
- PET detector, protected RFID UIDs, RFID engine, Sleep/Unknown handlers, FOOD reaction, 30-minute Stage timing, Progress, LED behavior and NVS state version 4 are unchanged.

### Validation
- Static delimiter, duplicate-function, Arduino auto-prototype, protected UID, PET-rule, Hunger scheduler and documentation checks passed.
- Protected firmware blocks were compared against v0.9.0-rc.2 before merge.
- Arduino compilation and physical TFT validation are still required.

## v0.9.0-rc.2

Pre-release Hunger visual/scheduling refinement after physical TFT testing of rc.1.

### Changed
- Removed the full-screen yellow Hunger sticker implementation.
- Hunger is now an independent lower-screen overlay while the existing eye renderer and eye state machine continue to run normally.
- Left display shows an animated chicken drumstick at the bottom; right display shows an animated banana at the bottom.
- Increased Stage Hunger quota from 10 to 15 completed requests.
- Each request remains exactly 10 seconds.
- Changed random timing to 8-25 s before the first request and 12-28 s between completed requests; worst-case uninterrupted schedule is 567 seconds inside a 10-minute Stage.
- Sleep/System-priority interruption retries the Hunger request after 5-12 s without consuming quota.
- PET, normal autonomous eye behavior, Blink/Wink/Smile/Play and non-system reactions can coexist with the Hunger overlay.
- Serial `h` previews the chicken+banana overlay for 10 seconds without consuming Stage quota.

### Preserved
- First valid FOOD in a Stage immediately stops Hunger, applies the existing FOOD progress rule, and cancels all remaining Hunger requests in that Stage.
- New Stage resets Hunger count and re-enables the FOOD need.
- Additive NVS Hunger Stage/count persistence remains; NVS state version stays 4.
- PET detector, protected RFID UIDs, RFID engine, FOOD reaction, persistent Sleep/Wake, 30-minute timing, Stage gates, Progress and LED behavior are unchanged.

### Validation
- Static delimiter, duplicate-function, version, protected UID, PET-rule, Hunger overlay/scheduler and documentation checks passed.
- Compared protected firmware blocks against v0.9.0-rc.1/develop to guard against unrelated edits.
- Arduino compilation and real-hardware visual validation are still required.

## v0.9.0-rc.1

Pre-release feature update for Stage-aware Hunger requests and the user-provided hunger sticker sequence.

### Added
- Added a dedicated Hunger Request scheduler separate from generic autonomous personality.
- Each 10-minute Stage can show up to 10 completed Hunger prompts until FOOD is received in that Stage.
- Every Hunger prompt is exactly 10 seconds.
- Added random Hunger timing: 20-45 s before the first prompt, 30-50 s after completed prompts, and 6-15 s retries after non-FOOD interruption.
- Added additive NVS persistence for Hunger Stage/count without invalidating existing v4 Demo state.
- Added a vector Hunger sticker based on the supplied reference frames: yellow face, blue eyes, mouth/tongue, spoon and hands.
- Serial `h` now previews the 10-second Hunger sticker without consuming Stage quota.

### Changed
- Removed Hunger from the generic Look/Wink/Smile/Play weighted pool so unscheduled extra Hunger events cannot appear.
- A valid FOOD interaction immediately dismisses Hunger and disables all remaining Hunger prompts in the current Stage.
- Entering a new Stage resets the Hunger prompt count and re-enables the FOOD need.
- A Hunger prompt interrupted before 10 seconds by a non-FOOD interaction is not counted and is retried later.

### Explicitly Not Added
- No cloud/puff graphic.
- No hamburger graphic.
- No pizza graphic.
- No other separate food icon.

### Preserved
- Direct live PET qualification remains the v0.8.0-rc.4 rule: current 2-of-3 or 3-of-3 YES on E0/E6/E11, 20 ms confirmation, and re-arm after touch count stays below two.
- RFID UIDs, FOOD reaction, persistent Sleep/Wake, Progress rules, 30-minute timing, Stage Unlock/Completion priority, LED behavior and non-Hunger autonomous behaviors are unchanged.
- NVS state version remains 4 because the new Hunger keys are backward-compatible additive fields.

### Validation
- Static delimiter, duplicate-function, version, protected UID, PET-rule, Hunger-scheduler and documentation consistency checks passed.
- Arduino compilation and real-hardware visual validation are still required in the physical development environment.

## v0.8.0-rc.4

Pre-release PET reliability fix based on hardware diagnostics after repeated petting.

### Fixed
- Replaced the PET session/full-release state machine with direct live 2-of-3 qualification.
- PET now triggers whenever at least two of MPR121 E0/E6/E11 are currently `YES` for one additional 20 ms confirmation sample.
- Removed the 1-second accumulated PET-presence requirement, stale one-pad session timeout, full-release gate, and residual start-block mask from PET qualification.
- Fixed the deadlock where `petRequireFullRelease=YES` plus one lingering touched electrode could prevent a later valid 2-of-3 or 3-of-3 touch from ever reaching the PET reaction.
- After a PET trigger, the same continuous >=2-pad hold is latched; re-arm occurs after the live touched count stays below two for 220 ms. One residual/stuck electrode is therefore tolerated.

### Preserved
- PET electrodes remain E0/E6/E11 and MPR121 thresholds remain 6/3.
- PET stays disabled during persistent Sleep.
- Repeated PET interactions still show the PET reaction and LED pulse even when that Stage's PET progress credit was already earned.
- 30-minute demo timing, Autonomous Personality, RFID UIDs, Progress rules, LED behavior and NVS state version 4 are unchanged.

### Validation
- Static delimiter, duplicate-function, version, PET-rule and documentation consistency checks passed.
- The Arduino build is not available in the current environment; compilation, flashing and real hardware validation of rc.4 are still required.

## v0.8.0-rc.3

Pre-release compile-compatibility fix for Arduino sketch preprocessing.

### Fixed
- Fixed Arduino IDE compilation errors where auto-generated `.ino` prototypes referenced `AutonomousState` before the enum declaration.
- Changed only the autonomous helper function signature boundaries from `AutonomousState` to `uint8_t`, with explicit casts where enum storage is required.
- Kept the internal `AutonomousState` enum and all autonomous runtime behavior unchanged.

### Unchanged
- 30-minute Active Demo timing and 10/20/30-minute Stage gates.
- Autonomous Look / Wink / Eye Smile / Play Invite / Hunger behavior and constrained-random scheduler.
- PET E0/E6/E11 rules, MPR121 thresholds, RFID UIDs, persistent Sleep, Progress, LED behavior and NVS state version 4.

### Validation
- Reproduced the root cause from the Arduino compiler diagnostics supplied from the physical development workflow.
- Verified no function signature in the sketch still uses `AutonomousState`, eliminating this auto-prototype dependency.
- Static delimiter, duplicate-function, version-consistency and documentation checks passed.
- Compilation of rc.3 itself still requires confirmation in Arduino IDE; flashing and hardware validation are still required.

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

