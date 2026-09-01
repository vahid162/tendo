# AGENTS.md

## Purpose

This file defines the operating rules for any AI agent, coding assistant, or automated contributor working on the Tando firmware repository.

The goal is to keep development reproducible, versioned, reviewable, and safe for real hardware.

---

## 1. Repository Source of Truth

- The `main` branch is the source of truth for the latest stable development state.
- Always read the current repository files before making changes.
- Do not assume that a local, cached, previously generated, or conversational copy is newer than the repository.
- The primary firmware file is:

```text
tando_final_demo_15min.ino
```

- Documentation and version metadata must stay synchronized with the firmware.

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

Documentation must reflect the current firmware on `main`.

---

## 8. Release Workflow

A normal release workflow is:

```text
change code
   ↓
review
   ↓
test on hardware
   ↓
fix issues
   ↓
select next semantic version
   ↓
update firmware version
   ↓
update VERSION
   ↓
update CHANGELOG.md
   ↓
update README.md if required
   ↓
commit
   ↓
create Git tag
   ↓
create GitHub Release
```

Do not create a release before the intended firmware state is finalized.

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

## 12. Branching Rules

Keep branching simple.

### main

`main` contains the latest stable development state.

### feature branches

Use a feature branch for large or risky work.

Examples:

```text
feature/new-eye-renderer
feature/pet-state-machine
feature/audio-reactions
```

Small, low-risk fixes may be committed directly to `main` when appropriate.

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

The current PET interaction uses MPR121 electrodes E0, E1, and E2.

Core behavior:

- one electrode alone must not trigger PET
- any two distinct electrodes may form a valid PET interaction
- all three are also valid
- exact order is not required
- direct electrical contact with the electrode is not required; capacitive sensing through the enclosure is intended
- at least about 1 second of accumulated capacitive presence is required
- stale single-pad holds must expire
- PET must not trigger while the persistent SLEEP state is active
- a valid PET event must not disappear just because another short visual reaction is running

Current MPR121 thresholds are documented in the firmware and README and must be kept synchronized.

Do not tune MPR121 thresholds blindly. Prefer real baseline / filtered / delta measurements from the actual hardware.

---

## 20. Reaction State Rules

The interaction manager must prevent user events from silently disappearing.

General priority model:

```text
System: Completion / Stage Unlock
        ↓
Sleep
        ↓
Food
        ↓
Pet
        ↓
Unknown RFID
        ↓
Idle
```

Rules:

- SLEEP is persistent while the SLEEP tag is present.
- PET must not override SLEEP.
- ordinary short reactions may be queued/coalesced when appropriate.
- repeated pending events should not create an unbounded stale animation queue.
- system stage/completion events must not be overwritten by later stage transitions.
- Completion must clear stale pending unlock animations.

---

## 21. Demo and Progress Rules

The current demo is 15 minutes of active demo time:

```text
Stage 1 = 0-5 min
Stage 2 = 5-10 min
Stage 3 = 10-15 min
Completion = 15 min
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

---

## 22. NVS Rules

Persistent state includes demo timing and progress.

Avoid unnecessary flash writes.

When modifying NVS structure:

- update the state version
- provide safe migration or reset behavior
- do not reinterpret old stored fields silently
- document behavior changes

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

## 27. Release Decision Rule

Not every commit is a release.

Development may contain several commits before a release is created.

Only create a Git Tag and GitHub Release when the version is intentionally being published as a stable checkpoint.

If the user asks only to modify code, do not automatically create a release unless release creation is explicitly requested or clearly part of the requested workflow.

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
read repository
   ↓
understand current version
   ↓
review requested change
   ↓
classify change: PATCH / MINOR / MAJOR
   ↓
modify current source
   ↓
run static validation
   ↓
update documentation if needed
   ↓
commit
   ↓
hardware test by project owner
   ↓
fix if necessary
   ↓
when explicitly approved for release:
update version + changelog
create tag
create GitHub Release
```

The repository must remain understandable and recoverable after every change.
