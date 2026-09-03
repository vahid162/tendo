# AGENTS.md

## Purpose

This file defines the operating rules for any AI agent, coding assistant, or automated contributor working on the Tando firmware repository.

The goal is to keep development reproducible, versioned, reviewable, and safe for real hardware.

---

## 1. Repository Source of Truth

Tando has two authoritative release channels:

- `main` is the source of truth for the latest **Stable** release.
- `develop` is the source of truth for the current **Pre-release development** state.
- Normal firmware/documentation development starts from the current `develop` branch unless the project owner explicitly requests stable promotion or repository repair.
- When reviewing or changing behavior, always read files from the branch being targeted; do not mix current `develop` behavior with older `main` behavior.
- Always read the current repository files before making changes.
- Do not assume that a local, cached, previously generated, or conversational copy is newer than the repository.
- The primary firmware file is:

```text
tando_final_demo_15min.ino
```

- Documentation and version metadata must stay synchronized with the firmware for the same target branch.

---

## 2. Released Versions Must Never Be Overwritten

- Every released firmware version must remain permanently recoverable.
- Never rewrite, move, delete, or reuse an existing release tag.
- Never replace the contents of an existing GitHub Release.
- If a released version contains a bug, create a new version instead of modifying the old release.
- Git history, tags, release notes, and release assets must preserve the exact historical state of every published version.

Example:

```text
v0.7.0  released
v0.7.1  bug fix
v0.8.0  new feature
v1.0.0  first production release
```

Do not turn `v0.7.0` into a different build after it has been released.

---

## 3. Versioning Standard

Tando uses Semantic Versioning:

```text
vMAJOR.MINOR.PATCH
```

### MAJOR

Increase MAJOR when the firmware introduces a breaking or product-level architecture change.

Example:

```text
v1.4.2 -> v2.0.0
```

### MINOR

Increase MINOR when adding a new backward-compatible feature, interaction, state, subsystem, or substantial behavior.

Example:

```text
v0.7.3 -> v0.8.0
```

Typical MINOR changes:

- new interaction type
- new animation mode
- new sensor behavior
- new progress behavior
- new user-facing state
- new hardware feature

### PATCH

Increase PATCH when fixing bugs or making backward-compatible refinements.

Example:

```text
v0.7.0 -> v0.7.1
```

Typical PATCH changes:

- PET detection bug fix
- RFID reliability fix
- animation timing correction
- debounce adjustment
- state-machine fix
- performance correction
- documentation-only correction that belongs to the same release line

---

## 4. Pre-1.0 Development

While Tando is still in prototype/demo development, use:

```text
v0.x.y
```

The first production-ready firmware should be released as:

```text
v1.0.0
```

Do not use informal release names such as:

```text
v7
v8
final-final
latest2
fixed3
```

Use Semantic Versioning only.

---

## 5. Required Version Files

The repository should maintain these version-related files:

```text
README.md
CHANGELOG.md
VERSION
tando_final_demo_15min.ino
```

### VERSION

`VERSION` must contain only the current version number.

Example:

```text
0.7.1
```

### Firmware Version Constant

The firmware must expose its version inside the source code.

Preferred format:

```cpp
#define TANDO_VERSION_MAJOR 0
#define TANDO_VERSION_MINOR 7
#define TANDO_VERSION_PATCH 1

#define TANDO_VERSION "0.7.1"
```

At boot, the firmware should print the version to Serial:

```text
TANDO FIRMWARE v0.7.1
```

This is required so the firmware running on a physical board can be identified during debugging.

---

## 6. CHANGELOG Rules

Every released version must have an entry in `CHANGELOG.md`.

Use newest-first ordering.

Example:

```markdown
# Changelog

## v0.7.1
- Fixed PET re-arm behavior.
- Fixed queued reaction handling.
- Improved RFID responsiveness during PET animations.

## v0.7.0
- Added the v7 interaction manager.
- Added robust 2-of-3 capacitive PET detection.
- Added persistent sleep behavior.
```

Release notes must describe what actually changed.

Do not copy generic notes from older versions.

---

## 7. README Rules

Update `README.md` whenever a change affects:

- hardware
- pin mapping
- RFID UIDs
- touch behavior
- PET rules
- sleep behavior
- progress behavior
- timing
- serial commands
- dependencies
- LED wiring
- supported firmware version
- installation or flashing instructions

Documentation must reflect the current firmware on the same branch being changed: `develop` for Pre-release development and `main` for Stable releases.

---

## 8. Release Workflow

Tando uses a two-stage release workflow.

### Development / Pre-release

Normal development is finalized on `develop` as a new `X.Y.Z-rc.N` Pre-release:

```text
start from current develop
   ↓
make the smallest requested change
   ↓
review + static validation
   ↓
bump Pre-release version
   ↓
sync firmware VERSION / VERSION / CHANGELOG / README / AGENTS as required
   ↓
merge/push develop
   ↓
immutable Git tag + GitHub Pre-release
   ↓
bench / compile / flash / real hardware validation
```

A Pre-release may intentionally be published **before** hardware validation so the exact candidate build can be tested and traced. Never describe that candidate as hardware-validated unless the test actually occurred.

### Stable promotion

Only a candidate that has received the required project-owner/hardware approval should be promoted to `main`:

```text
approved develop candidate
   ↓
set stable X.Y.Z (remove -rc.N)
   ↓
sync release metadata/docs
   ↓
merge/push main
   ↓
immutable Git tag + Stable GitHub Release
```

Do not create or move Stable history merely to make development convenient.

---

## 9. Git Tags

Release tags must exactly match the semantic version:

```text
v0.7.0
v0.7.1
v0.8.0
v1.0.0
```

Rules:

- one tag per released version
- never reuse a tag
- never move a released tag
- never delete a released tag unless explicitly authorized by the project owner for repository repair
- tags must point to the exact commit used for the release

---

## 10. GitHub Releases

Every stable published version should have a GitHub Release.

Recommended title:

```text
Tando Firmware v0.7.1
```

Each release should include:

- version number
- concise release summary
- bug fixes
- new features
- known limitations, if any
- hardware-impacting changes, if any
- migration notes, if required

Recommended release assets, when available:

```text
tando-v0.7.1.ino
tando-v0.7.1.bin
tando-v0.7.1.zip
```

Do not publish generated binaries unless they were built from the exact tagged source.

---

## 11. GitHub Packages

GitHub Packages is not the default distribution mechanism for the current Tando firmware.

Use GitHub Releases for firmware snapshots and downloadable builds.

GitHub Packages should only be introduced later if Tando becomes a reusable package, library, container, or installable dependency.

Do not add package-registry complexity without a real need.

---

## 12. Branching and Release Channels

Tando always maintains two release channels.

### main — Stable

`main` is the stable release channel.

- Every finalized change pushed to `main` MUST use a new stable Semantic Version in the form `X.Y.Z`.
- The firmware version constant, `VERSION`, `CHANGELOG.md`, and any affected README documentation MUST be updated in the same release state.
- A successful push of a new version to `main` MUST result in an immutable Git tag `vX.Y.Z` and a normal GitHub Release.
- Never push a firmware-affecting or project-rule change to `main` without a version bump.

### develop — Pre-release

`develop` is the pre-release channel.

- Every finalized AI-generated development change pushed to `develop` MUST use a new pre-release version in the form `X.Y.Z-rc.N`.
- Each new pre-release must increment `N` or move to a newer base version.
- A successful push of a new pre-release version to `develop` MUST result in an immutable Git tag and a GitHub Release marked as Pre-release.
- Pre-release builds are for review, bench testing, and hardware validation before promotion to Stable.

### feature branches

Use a feature branch for large or risky work.

Examples:

```text
feature/new-eye-renderer
feature/pet-state-machine
feature/audio-reactions
```

Small, low-risk fixes may use a short-lived fix/docs branch or be applied through the normal `develop` workflow. **Do not send normal development fixes directly to `main`.** `main` is reserved for explicit Stable promotion or authorized repository repair.

Do not maintain unnecessary long-lived branches.

---

## 13. AI Agent Change Rules

Before editing code, an AI agent must:

1. Read the current firmware from the repository.
2. Read `README.md`.
3. Read `CHANGELOG.md` and `VERSION` if they exist.
4. Identify the current version.
5. Determine whether the requested change is a PATCH, MINOR, or MAJOR change.
6. Preserve all unrelated working behavior.

The agent must not silently replace the current repository source with an older conversational or generated copy.

---

## 14. Do Not Claim Hardware Validation Without Hardware Validation

An AI agent must distinguish between:

- source review
- static validation
- compilation
- flashing
- real hardware testing

Never claim:

```text
compiled successfully
works on ESP32-S3
hardware tested
RFID confirmed
MPR121 calibrated
```

unless that step was actually performed.

Safe wording:

```text
Source structure was reviewed.
Static checks passed.
Hardware validation is still required.
```

---

## 15. Firmware Safety Rules

Tando controls real hardware.

An AI agent must not guess electrical limits.

When hardware changes are involved:

- verify voltage
- verify expected current
- verify GPIO capability
- verify required resistor or driver
- verify common ground requirements
- do not assume a previous LED, motor, speaker, or sensor circuit still applies

For the current low-current reaction LED, the intended simplified wiring is:

```text
GPIO21 -> suitable series resistor -> LED -> GND
```

The resistor value must be selected for the actual LED voltage/current specification.

---

## 16. Current Core Hardware

The current project is based on:

- ESP32-S3
- 2 × GC9A01 round TFT displays
- MPR121 capacitive touch controller
- RC522 RFID reader
- reaction LED on GPIO21
- ESP32 Preferences / NVS

Do not change pin assignments or hardware assumptions without updating documentation.

---

## 17. Current Pin Map

### Displays

```text
TFT SCLK  = GPIO4
TFT MOSI  = GPIO5

LEFT DC   = GPIO6
LEFT CS   = GPIO7
LEFT RST  = GPIO15

RIGHT DC  = GPIO18
RIGHT CS  = GPIO16
RIGHT RST = GPIO17
```

### MPR121

```text
SDA = GPIO8
SCL = GPIO9
```

### RC522

```text
SCK  = GPIO10
MISO = GPIO11
MOSI = GPIO12
SS   = GPIO13
RST  = GPIO14
```

### Reaction LED

```text
PWM = GPIO21
```

---

## 18. Current RFID Map

```text
FOOD 1 = 96 2B CD AB
FOOD 2 = F6 33 11 AA
SLEEP  = C6 34 BD AA
```

Do not change these values unless explicitly requested.

---

## 19. Current PET Rules

The current PET hardware A/B configuration uses widely separated MPR121 electrodes E0, E6, and E11.

Core behavior:

- one electrode alone must not trigger PET
- any two or all three of E0/E6/E11 currently reported `YES` by MPR121 must qualify as PET after only the short 20 ms stability confirmation
- PET qualification is based on the live current 2-of-3 touch count, not accumulated one-second session time or historical pad order
- after one PET trigger, the same continuous >=2-pad hold is latched to prevent rapid retriggering
- re-arm occurs after the live touch count stays below two for 220 ms; one residual/stuck `YES` electrode must never deadlock PET
- do not reintroduce `fullRelease`, stale-session, or residual `startBlockedMask` gating into PET qualification unless explicitly requested and validated on hardware
- direct electrical contact with the electrode is not required; capacitive sensing through the enclosure is intended
- PET must not trigger while the persistent SLEEP state is active
- a valid PET event must not disappear just because another short visual reaction is running

Current MPR121 thresholds are documented in the firmware and README and must be kept synchronized.
- Current hardware evidence favors the rc.3-style MPR121 startup path; do not reintroduce the rc.4 delayed startup / `ECR=0x83` experiment or rc.5 touched-filter writes without new measurements. Recalibration is manual-only for now so a real hand cannot accidentally become part of an automatic baseline reset.
- PET electrode mapping is currently E0/E6/E11 as a hardware A/B experiment. Keep firmware masks, diagnostics, README, and physical wiring synchronized when changing electrode channels.

Do not tune MPR121 thresholds blindly. Prefer real baseline / filtered / delta measurements from the actual hardware.

---


## 19A. Stage-Aware Care Request Scheduler

Hunger and PET Request are independent Need families but share one scheduling contract.

Current contract:

- Hunger: at most 10 completed 10-second automatic requests per Stage until FOOD is credited
- PET Request: at most 10 completed 10-second automatic requests per Stage until PET is credited
- automatic Care Request scheduling uses a Stage-local **wall-clock timeline**, not Active Demo Time
- automatic Care Requests continue while the Active Demo clock is paused for inactivity
- each Stage has ten one-minute Care slots
- each slot has an Early random window at 5-15 s and a Late random window at 35-45 s
- Hunger/PET alternate which family owns Early vs Late by Stage/slot
- if a natural slot becomes stale during the current boot because of a long reaction/sleep/runtime delay, skip it rather than bunching stale requests
- the Stage-local Care wall-clock anchor is runtime-only; after a reboot it restarts from the new boot/session and does not reconstruct pre-reboot wall-clock position from NVS
- NVS restores the persisted Care Stage/count, but it does not persist the current wall-clock slot timestamp
- an interrupted tracked automatic request retries after the blocking reaction with 5-10 seconds wall-clock delay
- after a naturally completed Care Request, enforce at least 5 seconds wall-clock visual cooldown
- Hunger and PET Request must never render simultaneously
- first valid FOOD immediately clears current/future automatic Hunger for that Stage
- first valid PET immediately clears current/future automatic PET Request for that Stage
- interrupted automatic requests do not consume completed-request quota
- Hunger visual remains the animated chicken drumstick on both displays
- PET Request visual remains soft affectionate eye bias plus pulsing `(( heart ))`

Manual previews are deliberately independent test paths:
- Serial `h` must show the Hunger overlay for 10 seconds even if FOOD is already satisfied, the automatic quota is exhausted, or the Demo clock is paused
- Serial `r` must show the PET Request overlay for 10 seconds even if PET is already satisfied, quota is exhausted, or the Demo clock is paused
- a real or queued reaction may interrupt a manual preview immediately
- manual previews never consume Stage quota, Progress, or satisfy/unsatisfy a Need

### Reaction visual exclusivity

Every active real/system reaction owns the display:

- PET / FOOD / SLEEP / Unknown / Stage Unlock / Completion must hide both Care Request overlays
- generic autonomous Look/Wink/Smile/Play must not continue during a reaction
- clear any in-progress ambient Blink at reaction start; only a reaction-specific Blink (currently FOOD) may occur
- Progress Ring must be hidden while `reaction != R_NONE`
- keep explicit `reaction == R_NONE` guards in both Care Request renderers

### Sleep visual lock

Persistent `R_SLEEP` is an absolute visual lock:

- PET/FOOD/Unknown remain blocked while Sleep is active
- Care Requests and autonomous personality remain blocked
- Stage Unlock and Completion remain pending; they must NOT preempt Sleep
- after SLEEP tag removal and Wake completion, service pending System reactions normally
- do not reintroduce Sleep preemption unless explicitly requested

---

## 20. Reaction State Rules

The interaction manager must prevent user events from silently disappearing and must keep active visual reactions exclusive.

General service/visual model:

```text
Persistent Sleep (while active) = absolute visual lock
        ↓ after Wake
Pending System: Completion / Stage Unlock
        ↓
Sleep request
        ↓
Food
        ↓
Pet
        ↓
Unknown RFID
        ↓
Care Request
        ↓
Autonomous Personality
        ↓
Micro Idle
```

Rules:

- SLEEP is persistent while the SLEEP tag is present.
- PET, FOOD, Unknown RFID, Care Requests, and autonomous personality must not visually override persistent SLEEP.
- Stage Unlock and Completion may become pending while SLEEP is active, but they **must not preempt the SLEEP visual**.
- After the SLEEP tag is removed and Wake finishes, pending System events are serviced normally.
- every active real/system reaction owns the display; Care Request overlays, generic autonomous visuals, Progress Ring, and ambient Blink must not leak into it
- ordinary short reactions may be queued/coalesced when appropriate.
- repeated pending events should not create an unbounded stale animation queue.
- queued user reactions should pass through a brief neutral visual handoff so strong blend state from the prior reaction does not leak into the next one.
- auto-blink timing must be re-armed after Wake or other long reactions so a stale blink deadline does not fire immediately on return to Idle.
- system stage/completion events must not be overwritten by later stage transitions.
- Completion must clear stale pending unlock animations.

---

## 21. Demo and Progress Rules

The current demo is 30 minutes of active demo time:

```text
Stage 1 = 0-10 min
Stage 2 = 10-20 min
Stage 3 = 20-30 min
Completion = 30 min
```

Each stage can earn at most one credit for:

- PET
- FOOD
- SLEEP

Maximum:

```text
3 credits per stage
9 credits total
```

Repeated care interactions still produce reactions but do not add duplicate progress credit in the same stage.

Power-off time is not counted as active demo time.

Current generic autonomous personality rules:

- generic autonomous personality events are eye-only and must not add Progress or pulse the interaction LED
- generic supported families are Look Around, Wink, Eye Smile, and Play Invite
- Hunger is not part of the generic weighted pool; it follows the Stage-aware care-request contract in section 19A
- timing and selection of the generic four states must use constrained randomness rather than a fixed sequence or fixed interval
- recently selected generic states should receive a temporary weight penalty rather than being absolutely forbidden
- user/system interactions must discard the current generic autonomous visual immediately; generic visuals must not be queued for later replay
- generic autonomous events must not resume or extend Active Demo Time
- normal two-eye Blink and deliberate one-eye Wink must remain distinct behaviors

---

## 22. NVS Rules

Persistent state includes demo timing, progress, and additive Care Request Stage/count fields.

Avoid unnecessary flash writes.

When modifying NVS state:

- if an existing stored field changes meaning, layout, interpretation, or compatibility, bump the NVS state version and provide a safe migration/reset path
- an additive key with a safe default may keep the existing NVS state version only when old data remains unambiguous and backward-compatible
- verify defaults for missing keys explicitly
- do not reinterpret old stored fields silently
- document persistence/migration behavior changes
- do not claim a wall-clock value survives reboot unless that exact timing value is persisted; the current Care wall-clock anchor is runtime-only

---

## 23. Performance Rules

Do not describe a target frame rate as achieved unless it is realistic for:

- two RGB565 frame transfers
- SPI bandwidth
- rendering cost
- sensor polling
- RFID polling

Do not sacrifice input responsiveness for animation smoothness.

Input systems should continue to receive adequate polling time during visual reactions.

---

## 24. Code Change Discipline

When changing firmware:

- modify the smallest necessary subsystem
- preserve working behavior outside the requested scope
- avoid large rewrites without a clear architectural reason
- avoid duplicate state variables
- avoid blocking delays inside interaction logic
- prefer non-blocking `millis()`-based state machines
- remember that Arduino preprocesses `.ino` files and auto-generates function prototypes; a helper function signature must not depend on a custom enum/struct type that may be declared later than the generated prototype insertion point
- for sketch-local helper boundaries, either guarantee the custom type is visible before generated prototypes (for example via a proper header/explicit safe declaration) or use a primitive boundary type such as `uint8_t` with explicit conversion to the internal enum
- keep Serial diagnostics useful
- keep comments accurate
- remove obsolete comments when hardware or behavior changes

---

## 25. Static Validation Before Commit

Before committing firmware changes, perform all feasible static checks.

At minimum verify:

- balanced `{}`
- balanced `()`
- balanced `[]`
- no accidental duplicate function definitions
- no stale references to removed state variables
- version strings are consistent
- README / CHANGELOG / VERSION are synchronized when required

Static checks do not replace compilation or hardware testing.

---

## 26. Commit Rules

Commit messages should describe the change, not the activity.

Good:

```text
fix: prevent PET events during persistent sleep
feat: add queued reaction manager
docs: document v0.8.0 hardware changes
```

Avoid:

```text
update
changes
final
fix stuff
new code
```

---

## 27. Mandatory Versioned Release Rule

For Tando, every finalized change delivered to a release branch is versioned and published.

- Finalized changes on `develop` MUST create a new Pre-release.
- Finalized changes on `main` MUST create a new Stable release.
- Even documentation/rule-only Pre-release corrections must keep `VERSION` and the firmware `TANDO_VERSION` synchronized because the release workflow validates them against each other; a version-only firmware edit must not be represented as a behavior change.
- The AI must not leave a completed repository change on either release branch without updating the version metadata and release history.
- Intermediate work may exist on feature branches without a release.
- Released tags and GitHub Releases are immutable historical records and must never be overwritten or reused.

Normal flow:

```text
feature/work
   ↓
develop + X.Y.Z-rc.N
   ↓
Git tag + GitHub Pre-release
   ↓
hardware/review validation
   ↓
main + X.Y.Z
   ↓
Git tag + GitHub Stable Release
```


---

## 28. Historical Traceability

For every released version, it must be possible to answer:

- Which exact source code was released?
- Which commit did the release use?
- Which tag points to it?
- What changed from the previous release?
- Which firmware version appears in Serial output?
- Which hardware assumptions applied to that version?

If any of these cannot be answered, the release process is incomplete.

---

## 29. Default AI Agent Workflow

For normal Tando work:

```text
read repository + AGENTS.md
   ↓
start from develop for normal development
   ↓
classify next base version: PATCH / MINOR / MAJOR
   ↓
modify current source
   ↓
run static validation
   ↓
set X.Y.Z-rc.N in firmware + VERSION
   ↓
update CHANGELOG.md + README.md if required
   ↓
commit/push develop
   ↓
automatic immutable Git tag + GitHub Pre-release
   ↓
hardware/review validation
   ↓
promote approved code to main
   ↓
remove -rc.N and set stable X.Y.Z
   ↓
update changelog/release notes
   ↓
commit/push main
   ↓
automatic immutable Git tag + GitHub Stable Release
```


The repository must remain understandable and recoverable after every change.

---

## 30. Automated Release Enforcement

The repository release workflow is authoritative for release automation.

Expected behavior:

- `main` accepts stable versions matching `X.Y.Z`.
- `develop` accepts pre-release versions matching `X.Y.Z-rc.N`.
- If the target Git tag already exists, the workflow must fail instead of overwriting it.
- The workflow must create the tag from the exact pushed commit.
- The GitHub Release must use that same tag.
- `main` releases must not be marked as Pre-release.
- `develop` releases must be marked as Pre-release.
- The firmware source file should be attached to the Release when automation is available.
- A release automation failure means the release process is incomplete and must be fixed before the change is considered delivered.
