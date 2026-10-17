# Windows port validation

Target: GD 2.2081 / Geode 5.11.2 / Windows x64, Cube Learner 0.2.0.

Before the first Windows CI run:

- Required API audit: 34 hooks have explicit Windows addresses in pinned bindings.
- SDK-free core tests and file-replacement tests pass on Linux. Core tests use
  ASan/UBSan. The POSIX replacement path was tested here; the Windows-specific
  MoveFileExW branch is tested by the Windows CI job.
- The port's SDK-facing code is also syntax-checked against the available
  official generated bindings. A non-Windows syntax check does not validate
  Windows layout/ABI or replace the native build.
- Native Windows build/test results must be read from the linked GitHub Actions
  run. The artifact is uploaded only after tests, compilation, linking and
  `.geode` ZIP/metadata/x64 PE/font checks succeed.

Geometry Dash has not been run here. Build success does not establish in-game
hook installation, input timing, protection of real saves, device performance
or compatibility with other mods. Before using the bot for long training runs,
test on backed-up saves without other mods:

1. Pause button, Russian popup, status, attempt/best counters and overlay.
2. Normal cube inputs, real deaths, full restarts and learning near a frontier.
3. Disable during a hold and while paused: no stuck button; manual control returns.
4. New best percentage, partial orb thresholds, secret/user coins, challenges,
   achievements, daily rewards and completion: no honest result or reward saved.
5. Disable/restart/quit after collecting coins and jumping: persisted level jump
   counters and pending coins must equal their pre-session values.
6. Re-enter after a full exit: new manual attempts can save normally again.
7. Repeated JSON saves and Unicode save paths must replace prior training data.
8. Changed level data must reject the old fingerprint; other levels keep their
   own files.
9. Dual, other modes, dash and sideways must release bot input. Platformer,
   native replay, editor test and practice must reject activation.

Only the cube strategy is implemented. Other modes and dual are architecture
extension points. The predictor cannot solve arbitrary gaps, orb chains, moving
or trigger-controlled geometry. No level-completion guarantee is made.
