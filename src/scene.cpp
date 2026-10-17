#include "scene.hpp"
#include <Geode/binding/EffectGameObject.hpp>
#include <algorithm>
#include <cmath>
using namespace geode::prelude;
namespace v2 {
namespace {
using namespace rf;
std::optional<MapObject> mapObject(GameObject *object) {
    MapObject result;
    result.x = object->getPositionX();
    result.y = object->getPositionY();
    result.uncertain = object->m_groupCount > 0 || object->m_isGroupDisabled || object->m_isDisabled ||
                       object->m_isNoTouch || object->m_isPassable;
    switch (object->m_objectType) {
    case GameObjectType::Solid:
    case GameObjectType::Breakable:
        result.kind = MapKind::Solid;
        break;
    case GameObjectType::Slope:
        result.kind = object->m_slopeIsHazard ? MapKind::Hazard : MapKind::Solid;
        break;
    case GameObjectType::Hazard:
    case GameObjectType::AnimatedHazard:
        result.kind = MapKind::Hazard;
        break;
    case GameObjectType::YellowJumpRing:
    case GameObjectType::PinkJumpRing:
    case GameObjectType::RedJumpRing:
    case GameObjectType::GravityRing:
    case GameObjectType::GreenRing:
    case GameObjectType::DropRing:
    case GameObjectType::SpiderOrb:
        result.kind = MapKind::Orb;
        break;
    case GameObjectType::YellowJumpPad:
    case GameObjectType::PinkJumpPad:
    case GameObjectType::RedJumpPad:
    case GameObjectType::GravityPad:
    case GameObjectType::SpiderPad:
        result.kind = MapKind::Pad;
        break;
    case GameObjectType::CubePortal:
        result.kind = MapKind::Mode;
        result.form = Form::Cube;
        break;
    case GameObjectType::ShipPortal:
        result.kind = MapKind::Mode;
        result.form = Form::Ship;
        break;
    case GameObjectType::WavePortal:
        result.kind = MapKind::Mode;
        result.form = Form::Wave;
        break;
    case GameObjectType::BallPortal:
        result.kind = MapKind::Mode;
        result.form = Form::Ball;
        break;
    case GameObjectType::UfoPortal:
        result.kind = MapKind::Mode;
        result.form = Form::Ufo;
        break;
    case GameObjectType::RobotPortal:
        result.kind = MapKind::Mode;
        result.form = Form::Robot;
        break;
    case GameObjectType::SpiderPortal:
        result.kind = MapKind::Mode;
        result.form = Form::Spider;
        break;
    case GameObjectType::SwingPortal:
        result.kind = MapKind::Mode;
        result.form = Form::Swing;
        break;
    case GameObjectType::InverseGravityPortal:
    case GameObjectType::NormalGravityPortal:
    case GameObjectType::GravityTogglePortal:
        result.kind = MapKind::Gravity;
        break;
    case GameObjectType::RegularSizePortal:
    case GameObjectType::MiniSizePortal:
        result.kind = MapKind::Size;
        break;
    case GameObjectType::DualPortal:
    case GameObjectType::SoloPortal:
        result.kind = MapKind::Dual;
        break;
    case GameObjectType::DashRing:
    case GameObjectType::GravityDashRing:
    case GameObjectType::CustomRing:
    case GameObjectType::TeleportOrb:
    case GameObjectType::TeleportPortal:
        result.kind = MapKind::Complex;
        break;
    case GameObjectType::Modifier:
        if (object->m_objectID == 200 || object->m_objectID == 201 || object->m_objectID == 202 ||
            object->m_objectID == 203 || object->m_objectID == 1334) {
            result.kind = MapKind::Speed;
            break;
        }
        [[fallthrough]];
    default:
        if (object->m_isTrigger && !object->m_isColorTrigger) {
            result.kind = MapKind::Script;
            result.uncertain = true;
            break;
        }
        return std::nullopt;
    }
    if (!std::isfinite(result.x) || !std::isfinite(result.y))
        return std::nullopt;
    // Match live collision collection: a passable/no-touch decoration must
    // not close a corridor in the structural graph.
    if ((result.kind == MapKind::Solid || result.kind == MapKind::Hazard) &&
        (object->m_isNoTouch || object->m_isPassable))
        return std::nullopt;
    if (result.kind == MapKind::Solid || result.kind == MapKind::Hazard) {
        auto r = object->getObjectRect();
        result.bounds = MapRect{r.getMinX(), r.getMaxX(), r.getMinY(), r.getMaxY()};
    }
    return result;
}

std::optional<rf::Interaction> interaction(GameObject *o, PlayerObject *p, PlayLayer *layer) {
    if (o->hasBeenActivatedByPlayer(p) || p->m_ringRelatedSet.count(o->m_uniqueID))
        return std::nullopt;
    rf::Interaction i;
    auto t = o->m_objectType;
    auto r = o->getObjectRect();
    i.area = {r.getMinX(), r.getMaxX(), r.getMinY(), r.getMaxY()};
    if (auto effect = typeinfo_cast<EffectGameObject *>(o))
        i.reverse = effect->m_isReverse;
    switch (t) {
    case GameObjectType::YellowJumpRing:
        i.kind = rf::InteractionKind::YellowOrb;
        break;
    case GameObjectType::PinkJumpRing:
        i.kind = rf::InteractionKind::PinkOrb;
        break;
    case GameObjectType::RedJumpRing:
        i.kind = rf::InteractionKind::RedOrb;
        break;
    case GameObjectType::GravityRing:
        i.kind = rf::InteractionKind::BlueOrb;
        break;
    case GameObjectType::GreenRing:
        i.kind = rf::InteractionKind::GreenOrb;
        break;
    case GameObjectType::DropRing:
        i.kind = rf::InteractionKind::BlackOrb;
        break;
    case GameObjectType::YellowJumpPad:
    case GameObjectType::PinkJumpPad:
    case GameObjectType::RedJumpPad:
        i.kind = t == GameObjectType::YellowJumpPad ? rf::InteractionKind::YellowPad
                 : t == GameObjectType::PinkJumpPad ? rf::InteractionKind::PinkPad
                                                    : rf::InteractionKind::RedPad;
        i.value = 16 * layer->getBumpMod(p, static_cast<int>(t));
        break;
    case GameObjectType::GravityPad:
        i.kind = rf::InteractionKind::BluePad;
        break;
    case GameObjectType::SpiderOrb:
    case GameObjectType::SpiderPad:
        i.kind =
            t == GameObjectType::SpiderOrb ? rf::InteractionKind::SpiderOrb : rf::InteractionKind::SpiderPad;
        i.inverted = o->isFacingDown();
        break;
    case GameObjectType::InverseGravityPortal:
    case GameObjectType::NormalGravityPortal:
        i.kind = rf::InteractionKind::GravityPortal;
        i.inverted = t == GameObjectType::InverseGravityPortal;
        break;
    case GameObjectType::GravityTogglePortal:
        i.kind = rf::InteractionKind::GravityPortal;
        i.value = 1;
        break;
    case GameObjectType::CubePortal:
    case GameObjectType::ShipPortal:
    case GameObjectType::BallPortal:
    case GameObjectType::UfoPortal:
    case GameObjectType::WavePortal:
    case GameObjectType::RobotPortal:
    case GameObjectType::SpiderPortal:
    case GameObjectType::SwingPortal:
        i.kind = rf::InteractionKind::ModePortal;
        i.target = t == GameObjectType::CubePortal     ? rf::Form::Cube
                   : t == GameObjectType::ShipPortal   ? rf::Form::Ship
                   : t == GameObjectType::BallPortal   ? rf::Form::Ball
                   : t == GameObjectType::UfoPortal    ? rf::Form::Ufo
                   : t == GameObjectType::WavePortal   ? rf::Form::Wave
                   : t == GameObjectType::RobotPortal  ? rf::Form::Robot
                   : t == GameObjectType::SpiderPortal ? rf::Form::Spider
                                                       : rf::Form::Swing;
        break;
    case GameObjectType::RegularSizePortal:
    case GameObjectType::MiniSizePortal:
        i.kind = rf::InteractionKind::SizePortal;
        i.value = t == GameObjectType::MiniSizePortal ? .6 : 1;
        break;
    case GameObjectType::DualPortal:
    case GameObjectType::SoloPortal:
        i.kind = rf::InteractionKind::NativeBoundary;
        break;
    case GameObjectType::Modifier: {
        auto id = o->m_objectID;
        i.kind = rf::InteractionKind::SpeedPortal;
        i.value = id == 200    ? 4.1860014
                  : id == 201  ? 5.1930018
                  : id == 202  ? 6.4570022
                  : id == 203  ? 7.8000026
                  : id == 1334 ? 9.6000032
                               : 0;
        if (i.value <= 0)
            return std::nullopt;
        break;
    }
    default:
        return std::nullopt;
    }
    return i;
}
bool obstacle(GameObjectType t) {
    return t == GameObjectType::Solid || t == GameObjectType::Breakable || t == GameObjectType::Hazard ||
           t == GameObjectType::AnimatedHazard || t == GameObjectType::Slope;
}
} // namespace
void SceneReader::bind(PlayLayer *l) {
    if (l != m_layer) {
        cancel();
        m_map.reset();
        m_analysis = "Analyze не запускался";
    }
    m_layer = l;
    m_objects.clear();
    m_count = l && l->m_objects ? l->m_objects->count() : 0;
    if (l && l->m_objects)
        for (auto o : CCArrayExt<GameObject *>(l->m_objects)) {
            auto t = o->m_objectType;
            if (t != GameObjectType::Decoration)
                m_objects.emplace_back(o);
        }
    reset();
}
void SceneReader::reset() {
    m_motion.reset();
    m_scene = {};
}
Scene const &SceneReader::read(Frame const &f) {
    if (m_layer && m_layer->m_objects && m_layer->m_objects->count() != m_count)
        bind(m_layer);
    m_scene = {};
    double left = f.players[0].x - 600, right = f.players[0].x + 600;
    for (int i = 0; i < (f.dual ? 2 : 1); i++) {
        auto const &s = f.players[i];
        double reach = std::abs(s.vx) * std::max(90., s.planningHorizon + 8) + s.halfWidth + 80;
        left = std::min(left, s.x - reach);
        right = std::max(right, s.x + reach);
    }
    for (auto const &retained : m_objects) {
        auto o = retained.data();
        if (o->m_isDisabled || o->m_isGroupDisabled || o->m_isNoTouch)
            continue;
        auto r = o->getObjectRect();
        if (r.getMaxX() < left || r.getMinX() > right)
            continue;
        auto t = o->m_objectType;
        if (obstacle(t)) {
            if (o->m_isPassable)
                continue;
            rf::Obstacle a{r.getMinX(), r.getMaxX(), r.getMinY(), r.getMaxY()};
            a.solid = t == GameObjectType::Solid || t == GameObjectType::Breakable ||
                      (t == GameObjectType::Slope && !o->m_slopeIsHazard);
            if (o->m_objectRadius > 0) {
                auto radius = o->getObjectRadius();
                double x = r.getMidX(), y = r.getMidY();
                a = {x - radius, x + radius, y - radius, y + radius, a.solid, radius};
            } else if (t == GameObjectType::Slope) {
                double x = o->slopeWallLeft() ? a.left : a.right, y = o->slopeFloorTop() ? a.top : a.bottom;
                a.vertexCount = 3;
                a.vertices = {rf::Point{x, y}, rf::Point{x == a.left ? a.right : a.left, y},
                              rf::Point{x, y == a.bottom ? a.top : a.bottom}, rf::Point{}};
                a.ramp = a.solid;
            } else if (o->m_shouldUseOuterOb) {
                if (auto box = o->getOrientedBox()) {
                    rf::Point center{};
                    bool valid = true;
                    for (int n = 0; n < 4; n++) {
                        auto p = box->m_corners[n];
                        valid &= std::isfinite(p.x) && std::isfinite(p.y);
                        a.vertices[n] = {p.x, p.y};
                        center.x += p.x / 4.;
                        center.y += p.y / 4.;
                    }
                    if (valid && std::abs(center.x - r.getMidX()) < r.size.width + 2 &&
                        std::abs(center.y - r.getMidY()) < r.size.height + 2) {
                        std::sort(a.vertices.begin(), a.vertices.end(), [&](auto x, auto y) {
                            return std::atan2(x.y - center.y, x.x - center.x) <
                                   std::atan2(y.y - center.y, y.x - center.x);
                        });
                        double area = 0;
                        for (int n = 0; n < 4; n++) {
                            auto x = a.vertices[n], y = a.vertices[(n + 1) % 4];
                            area += x.x * y.y - x.y * y.x;
                        }
                        if (std::abs(area) > .001)
                            a.vertexCount = 4;
                    }
                }
            }
            a.id = reinterpret_cast<std::uintptr_t>(o);
            m_motion.update(a, f.outer / 4.);
            m_scene.objects.push_back(a);
        } else
            for (int p = 0; p < (f.dual ? 2 : 1); p++)
                if (auto i = interaction(o, p ? m_layer->m_player2 : m_layer->m_player1, m_layer)) {
                    i->area.id = reinterpret_cast<std::uintptr_t>(o);
                    m_motion.update(i->area, f.outer / 4.);
                    m_scene.interactions[p].push_back(*i);
                }
    }
    if (m_scene.objects.size() > 512) {
        std::sort(m_scene.objects.begin(), m_scene.objects.end(), [&](auto const &a, auto const &b) {
            auto distance = [&](auto const &o) {
                double value = std::abs((o.left + o.right) / 2 - f.players[0].x);
                if (f.dual)
                    value = std::min(value, std::abs((o.left + o.right) / 2 - f.players[1].x));
                return value;
            };
            return distance(a) < distance(b);
        });
        m_scene.objects.resize(512);
    }
    for (int p = 0; p < 2; p++) {
        auto &list = m_scene.interactions[p];
        std::stable_sort(list.begin(), list.end(), [&](auto const &a, auto const &b) {
            return std::abs((a.area.left + a.area.right) / 2 - f.players[p].x) <
                   std::abs((b.area.left + b.area.right) / 2 - f.players[p].x);
        });
        if (list.size() > 48)
            list.resize(48);
    }
    return m_scene;
}
std::string SceneReader::report() const {
    unsigned hazards = 0, solids = 0, complex = 0;
    for (auto const &r : m_objects) {
        auto t = r->m_objectType;
        if (obstacle(t)) {
            if (t == GameObjectType::Hazard || t == GameObjectType::AnimatedHazard)
                ++hazards;
            else
                ++solids;
        }
        if (t == GameObjectType::DashRing || t == GameObjectType::GravityDashRing ||
            t == GameObjectType::CustomRing || t == GameObjectType::TeleportOrb ||
            t == GameObjectType::TeleportPortal)
            ++complex;
    }
    return m_analysis + "\nОбъектов: " + std::to_string(m_count) +
           "\nОпасностей: " + std::to_string(hazards) + " · поверхностей: " + std::to_string(solids) +
           "\nСложных взаимодействий: " + std::to_string(complex) +
           (m_map ? "\nСвязок: " + std::to_string(m_map->chains) +
                        " · неопределённых объектов: " + std::to_string(m_map->uncertain)
                  : "") +
           "\n\nDash, custom/телепорты и произвольные триггеры не моделируются полностью. Прохождение "
           "подтверждается реальным повтором с начала.";
}
void SceneReader::analyze() {
    if (!m_layer || busy())
        return;
    if (m_count > 250000) {
        m_analysis = "Лимит анализа: 250000 объектов";
        return;
    }
    m_job = {};
    m_index = 0;
    m_map.reset();
    m_capturing = true;
    m_analysis = "Считывание объектов";
    m_job.scanned = m_count;
    static constexpr rf::Form forms[]{rf::Form::Cube, rf::Form::Ship,  rf::Form::Ball,   rf::Form::Ufo,
                                      rf::Form::Wave, rf::Form::Robot, rf::Form::Spider, rf::Form::Swing};
    auto mode = m_layer->m_levelSettings->m_startMode;
    m_job.startForm = mode >= 0 && mode < 8 ? forms[mode] : rf::Form::Cube;
    m_job.twoPlayer = m_layer->m_levelSettings->m_twoPlayerMode;
    m_job.startsDual = m_layer->m_levelSettings->m_startDual;
}
void SceneReader::pump() {
    if (m_capturing) {
        auto end = std::min(m_index + 256, m_objects.size());
        for (; m_index < end; m_index++)
            if (auto object = mapObject(m_objects[m_index].data()))
                m_job.objects.push_back(*object);
        m_analysis = "Analyze: " + std::to_string(m_index) + " / " + std::to_string(m_objects.size());
        if (m_index == m_objects.size()) {
            m_capturing = false;
            m_worker.start(std::move(m_job));
            m_analysis = "Analyze: расчёт коридоров в фоне";
        }
    }
    std::string error;
    if (auto result = m_worker.take(error)) {
        m_map = std::move(result);
        m_analysis = "АНАЛИЗ ГОТОВ · подсказки подключены к планировщику";
    }
    if (!error.empty())
        m_analysis = "Analyze: " + error;
}
void SceneReader::cancel() {
    bool wasBusy = busy();
    m_capturing = false;
    m_job = {};
    m_worker.cancel();
    if (wasBusy)
        m_analysis = "Анализ отменён";
}
void SceneReader::hint(rf::FormState &state) const {
    if (m_map) {
        auto h = m_map->hint(state);
        state.planningHorizon = h.horizon;
        state.routeTarget = h.targetY;
    }
}
} // namespace v2
