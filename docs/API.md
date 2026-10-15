# Проверенные зависимости и API

Проверка началась с пустого репозитория: `mod.json`, CMake и установленного SDK/игры в нём не было. Поэтому адаптер создан под явную пару версий, а не под предполагаемую установленную игру.

| Зависимость | Закрепление |
| --- | --- |
| Geode SDK | tag v5.11.1, commit `413ac9b3936373e0983b3ebb8a056a3d39e8541c` |
| Geometry Dash | `GEODE_GD_VERSION=2.2081`, `GEODE_COMP_GD_VERSION=22081` официального SDK |
| Geode bindings | `f8aec5d1f3df76d840dfd2609f02cef30dc6efdf` |
| JSON | nlohmann/json v3.12.0, архив проверяется SHA-256 в CMake |
| Использованные инструменты сборки | Geode CLI 3.9.0, NDK r29 / Clang 21 |

Основной источник сигнатур: [GeometryDash.bro для 2.2081](https://github.com/geode-sdk/bindings/blob/f8aec5d1f3df76d840dfd2609f02cef30dc6efdf/bindings/2.2081/GeometryDash.bro). Методы и поля дополнительно прошли компиляцию настоящих generated headers и линковку с официальным Android64 SDK binary.

Используются реальные методы:

```cpp
GJBaseGameLayer::update(float);
GJBaseGameLayer::processCommands(float, bool, bool);
GJBaseGameLayer::handleButton(bool, int, bool);
PlayLayer::init(GJGameLevel*, bool, bool);
PlayLayer::resetLevel();
PlayLayer::resetLevelFromStart();
PlayLayer::removeAllCheckpoints();
PlayLayer::destroyPlayer(PlayerObject*, GameObject*);
PlayLayer::levelComplete();
PlayLayer::getCurrentPercent();
PlayerObject::releaseAllButtons();
GameToolbox::fast_srand(uint64_t);
```

Также используются `m_queuedButtons`, `m_queuedRecordedButtons`, `m_queuedReplayButtons`, `m_extraDelta`, `m_randomSeed`, `m_replayRandSeed`, `m_gameState`, `m_clickBetweenSteps`, `m_clickOnSteps` и поля PlayerObject/LevelSettingsObject из тех же bindings. Метод `getModifiedDelta` не заменяется, адреса и байт-патчи не применяются.

Geode UI и файловые API проверены по [исходникам SDK v5.11.1](https://github.com/geode-sdk/geode/tree/v5.11.1/loader/include/Geode): `Popup::init`, `NineSlice::create`, `Mod::getSaveDir`, `Loader::getAllMods`, `VersionInfo::toVString`, `sha256`, cocos `CCLabelTTF`. Русские надписи рисуются системным TTF-шрифтом, поскольку штатные bitmap fonts не содержат всю кириллицу.

Официальная версия игры и требования toolchain видны в [CMakeLists.txt SDK](https://github.com/geode-sdk/geode/blob/v5.11.1/CMakeLists.txt). Bindings загружаются отдельно с закреплённым commit, чтобы дальнейшее изменение `bindings/main` не меняло API уже выпущенного исходного проекта.
