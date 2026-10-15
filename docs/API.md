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
PlayLayer::get();
PlayLayer::setupHasCompleted();
PlayLayer::onEnterTransitionDidFinish();
PlayLayer::resetLevel();
PlayLayer::resetLevelFromStart();
PlayLayer::removeAllCheckpoints();
PlayLayer::destroyPlayer(PlayerObject*, GameObject*);
PlayLayer::levelComplete();
PlayLayer::getCurrentPercent();
PlayerObject::releaseAllButtons();
PlayerObject::getCurrentXVelocity();
GameObject::getObjectRect();
GJBaseGameLayer::getMinPortalY();
GJBaseGameLayer::getMaxPortalY();
GameToolbox::fast_srand(uint64_t);
```

Также используются `m_queuedButtons`, `m_queuedRecordedButtons`, `m_queuedReplayButtons`, `m_extraDelta`, `m_randomSeed`, `m_replayRandSeed`, `m_gameState`, `m_clickBetweenSteps`, `m_clickOnSteps` и поля PlayerObject/LevelSettingsObject из тех же bindings. Метод `getModifiedDelta` не заменяется, адреса и байт-патчи не применяются.

Geode UI и файловые API проверены по [исходникам SDK v5.11.1](https://github.com/geode-sdk/geode/tree/v5.11.1/loader/include/Geode): `Popup::init`, `NineSlice::create`, `Mod::getSaveDir`, `Loader::getAllMods`, `VersionInfo::toVString`, `sha256`, cocos `CCLabelTTF`. Русские надписи рисуются системным TTF-шрифтом, поскольку штатные bitmap fonts не содержат всю кириллицу.

`Popup::init` уже выравнивает `m_buttonMenu` через `CopySizeLayout`: его позицию нельзя обнулять при использовании координат окна. Постоянный HUD добавляется в настоящий `GJBaseGameLayer::m_uiLayer`. Активный уровень повторно берётся через `PlayLayer::get()` при открытии меню и запуске операций; `setupHasCompleted`, переход сцены и `update` страхуют привязку после асинхронной загрузки.

Контроллер полёта 1.1.0 читает `m_isShip`, `m_isDart`, `m_isUpsideDown`, `m_isGoingLeft`, `m_isSideways`, `m_vehicleSize`, `m_gravityMod`, `m_yVelocity`. Декорации и no-touch/passable/disabled/group-disabled объекты исключаются; невидимые коллизии учитываются. Считывание прямоугольников не заменяет штатные коллизии игры. Прогноз — локальная математика над числами, без вызовов `update` над копиями уровня и без восстановления игрового состояния.

Официальная версия игры и требования toolchain видны в [CMakeLists.txt SDK](https://github.com/geode-sdk/geode/blob/v5.11.1/CMakeLists.txt). Bindings загружаются отдельно с закреплённым commit, чтобы дальнейшее изменение `bindings/main` не меняло API уже выпущенного исходного проекта.

## Дополнения адаптера 1.6.0

В тех же закреплённых bindings проверены поля `m_isBall`, `m_isBird`, `m_isRobot`, `m_isSpider`, `m_isSwing`, `m_yStart`, `m_gravity`, `m_touchedPad`, `m_touchedRing`, `m_touchedGravityPortal`, `m_isOnSlope`, `m_touchingRings`, `m_touchedRings`, `m_uniqueID`, `m_objectID` и `EffectGameObject::m_isReverse`.

Для Windows используются реальные `GJBaseGameLayer::getBumpMod(PlayerObject*, int)` и `GameObject::isFacingDown()`. В ARM64-адаптере для этих двух функций применены локальные приближённые коэффициенты/направление по rotation, поскольку соответствующие определения отличаются по платформе. Положение, границы и обычный ввод по-прежнему получаются из настоящей игры. Предсказатель не вызывает `ringJump`, `spiderTestJump`, `flipGravity` и не присваивает состояние настоящего игрока.

Траектория рисуется настоящим cocos `CCDrawNode::create/clear/drawSegment/drawDot`; координаты проходят `CCNode::convertToWorldSpace/convertToNodeSpace` через родителя игрока и HUD. Калибровка измеряет числа между штатными шагами, не патчит физику и не заменяет native update.

## Дополнения 1.7.0

Оформление использует уже проверенные `CCDrawNode::drawPolygon/drawSegment/drawDot`, `CCMenuItemSpriteExtra::create` и реальное поле `m_scaleMultiplier`, `CCLabelTTF::getString/setString`. Для линии используются настоящие `CCNode::nodeToWorldTransform/worldToNodeTransform` и `CCPointApplyAffineTransform`; матрицы берутся один раз на обновление линии. Списки объектов удерживаются через Geode `Ref<GameObject>`, очищаются при выходе и пересобираются после reset/изменения размера native `m_objects`. Значения геометрии между разными outer updates не кэшируются.
