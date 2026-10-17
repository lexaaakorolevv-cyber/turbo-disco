# Windows API audit

Target: GD 2.2081, Geode 5.11.2, Windows x64, mod 0.2.0.

- SDK commit: `3909856e8714b503945d5cea12ba1bddf5d5b1b0`.
- Bindings: `7407717c307d1cd94461786210b330ab868f4260`.
- [GD 2.2081 declarations](https://github.com/geode-sdk/bindings/blob/7407717c307d1cd94461786210b330ab868f4260/bindings/2.2081/GeometryDash.bro).
- [Windows inline PlayLayer methods](https://github.com/geode-sdk/bindings/blob/7407717c307d1cd94461786210b330ab868f4260/bindings/2.2081/inline/PlayLayer.cpp).
- [Windows inline GameStatsManager methods](https://github.com/geode-sdk/bindings/blob/7407717c307d1cd94461786210b330ab868f4260/bindings/2.2081/inline/GameStatsManager.cpp).

The original Android design's `PlayLayer::incrementJumps` and
`GameStatsManager::storePendingUserCoin` have `win inline`, so this port does
not pretend that they are hookable. `WinProgressGuard` preserves persistent
level results and the pre-session pending-coin dictionary. It restores these
after commands/input/pickup/frame processing, before `GJGameLevel::encodeWithCoder`
and `GameStatsManager::encodeDataTo`, and during original exit handling.
The guard holds the level and dictionary via Geode Ref to keep them valid
until teardown ends. It never restores a player or a physics snapshot.

Additional real Windows-address hooks:

- `GJGameLevel::encodeWithCoder(DS_Dictionary*)`: `win 0x16ef00`.
- `GameStatsManager::encodeDataTo(DS_Dictionary*)`: `win 0x1f21a0`.
- `GJBaseGameLayer::pickupItem(EffectGameObject*)`: `win 0x2161b0`.

All 34 required hooks are checked by `scripts/audit-bindings.py` for explicit
Windows addresses, then checked at activation for actual registration and
`Hook::isEnabled()`. The source check is not an installation or gameplay test.

Normal completion is intercepted before stock result/reward handling. Standard
score, percentage, stats, coin, challenge, currency and achievement mutations
are denied while tainted. All stock GD `ProcessHttpRequest` requests are denied
until the assisted scene has completely exited. Existing requests are not
cancelled. Other mods can bypass these paths and require separate compatibility
testing.

Geode Loader Safe Mode disables mods, not game progress:
[MenuLayer.cpp](https://github.com/geode-sdk/geode/blob/v5.11.2/loader/src/hooks/MenuLayer.cpp#L144-L155).
No public gameplay-safe-mode API was found. No permanent test/practice flags,
memory patches, noclip or collision/physics changes are used.

Geode's disable action takes effect after a restart; the bot observes a pending
disable, releases input and retains protection for the current assisted scene.
Turning the bot off does not make the same attempt an honest result.

Actual before/after game saves, partial rewards, pending coin persistence,
completion, pause, death, restart, disable and scene exit must be tested in GD.
