#include "Runtime.hpp"
#include <Geode/binding/EffectGameObject.hpp>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;
namespace rf {
void Runtime::rebuildSceneCache(){
    m_obstacleObjects.clear();m_interactionObjects.clear();
    m_sceneObjectCount=m_layer->m_objects?m_layer->m_objects->count():0;
    if(m_layer->m_objects)for(auto object:CCArrayExt<GameObject*>(m_layer->m_objects)){
        switch(object->m_objectType){
        case GameObjectType::Solid:case GameObjectType::Breakable:case GameObjectType::Hazard:
        case GameObjectType::AnimatedHazard:case GameObjectType::Slope:m_obstacleObjects.emplace_back(object);break;
        case GameObjectType::YellowJumpRing:case GameObjectType::PinkJumpRing:case GameObjectType::RedJumpRing:
        case GameObjectType::GravityRing:case GameObjectType::GreenRing:case GameObjectType::DropRing:
        case GameObjectType::YellowJumpPad:case GameObjectType::PinkJumpPad:case GameObjectType::RedJumpPad:
        case GameObjectType::GravityPad:case GameObjectType::SpiderOrb:case GameObjectType::SpiderPad:
        case GameObjectType::InverseGravityPortal:case GameObjectType::NormalGravityPortal:case GameObjectType::GravityTogglePortal:
        case GameObjectType::CubePortal:case GameObjectType::ShipPortal:case GameObjectType::BallPortal:
        case GameObjectType::UfoPortal:case GameObjectType::WavePortal:case GameObjectType::RobotPortal:
        case GameObjectType::SpiderPortal:case GameObjectType::SwingPortal:case GameObjectType::RegularSizePortal:
        case GameObjectType::MiniSizePortal:case GameObjectType::DashRing:case GameObjectType::GravityDashRing:
        case GameObjectType::CustomRing:case GameObjectType::TeleportOrb:case GameObjectType::TeleportPortal:
            m_interactionObjects.emplace_back(object);break;
        case GameObjectType::Modifier:{auto id=object->m_objectID;if(id==200||id==201||id==202||id==203||id==1334)m_interactionObjects.emplace_back(object);break;}
        default:break;
        }
    }
    m_guidanceClock.reset();
}
void Runtime::collectObstacles(double minX, double maxX, double minY, double maxY) {
    m_flightObstacles.clear();
    for (auto const& retained : m_obstacleObjects) {
        auto object=retained.data();
        auto type = object->m_objectType;
        if (type != GameObjectType::Solid && type != GameObjectType::Breakable && type != GameObjectType::Hazard &&
            type != GameObjectType::AnimatedHazard && type != GameObjectType::Slope) continue;
        if (object->m_isDisabled || object->m_isGroupDisabled || object->m_isNoTouch || object->m_isPassable) continue;
        auto rect = object->getObjectRect();
        FlightObstacle obstacle{rect.getMinX(), rect.getMaxX(), rect.getMinY(), rect.getMaxY()};
        obstacle.solid = type == GameObjectType::Solid || type == GameObjectType::Breakable ||
            (type == GameObjectType::Slope && !object->m_slopeIsHazard);
        if (object->m_objectRadius > 0) {
            double radius = object->getObjectRadius();
            double cx = (obstacle.left + obstacle.right) / 2, cy = (obstacle.bottom + obstacle.top) / 2;
            obstacle = {cx - radius, cx + radius, cy - radius, cy + radius, obstacle.solid, radius};
        } else if (type == GameObjectType::Slope) {
            // Native helpers identify the two flat sides of the right triangle.
            double x = object->slopeWallLeft() ? obstacle.left : obstacle.right;
            double y = object->slopeFloorTop() ? obstacle.top : obstacle.bottom;
            obstacle.vertexCount = 3;
            obstacle.vertices = {Point{x, y}, Point{x == obstacle.left ? obstacle.right : obstacle.left, y},
                Point{x, y == obstacle.bottom ? obstacle.top : obstacle.bottom}, Point{}};
        } else if (object->m_shouldUseOuterOb) {
            // The real native oriented box supplies transformed world corners.
            if (auto box = object->getOrientedBox()) {
                Point center{};
                bool valid = true;
                for (int i = 0; i < 4; ++i) {
                    auto p = box->m_corners[i];
                    valid = valid && std::isfinite(p.x) && std::isfinite(p.y);
                    obstacle.vertices[i] = {p.x, p.y}; center.x += p.x / 4.; center.y += p.y / 4.;
                }
                // If another mod changes corner semantics, retain the native
                // rectangle instead of using unrecognised local coordinates.
                if (valid && std::abs(center.x - rect.getMidX()) < rect.size.width + 2 &&
                    std::abs(center.y - rect.getMidY()) < rect.size.height + 2) {
                    std::sort(obstacle.vertices.begin(), obstacle.vertices.end(), [&](Point a, Point b) {
                        return std::atan2(a.y - center.y, a.x - center.x) < std::atan2(b.y - center.y, b.x - center.x);
                    });
                    double area = 0;
                    for (int i = 0; i < 4; ++i) {
                        auto a = obstacle.vertices[i], b = obstacle.vertices[(i + 1) % 4];
                        area += a.x * b.y - a.y * b.x;
                    }
                    if (std::abs(area) > .001) {
                        obstacle.vertexCount = 4;
                        obstacle.left = obstacle.right = obstacle.vertices[0].x;
                        obstacle.bottom = obstacle.top = obstacle.vertices[0].y;
                        for (auto p : obstacle.vertices) {
                            obstacle.left = std::min(obstacle.left, p.x); obstacle.right = std::max(obstacle.right, p.x);
                            obstacle.bottom = std::min(obstacle.bottom, p.y); obstacle.top = std::max(obstacle.top, p.y);
                        }
                    }
                }
            }
        }
        if (!std::isfinite(obstacle.left) || !std::isfinite(obstacle.right) || !std::isfinite(obstacle.bottom) ||
            !std::isfinite(obstacle.top) || obstacle.right < minX - 100 || obstacle.left > maxX + 100 ||
            obstacle.top < minY - 100 || obstacle.bottom > maxY + 100) continue;
        obstacle.id = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(object));
        m_motion.update(obstacle,m_outerUpdates / 4.);
        m_flightObstacles.push_back(obstacle);
    }
    if (m_flightObstacles.size() > 512) {
        double x = m_layer->m_player1->getPositionX();
        std::stable_sort(m_flightObstacles.begin(), m_flightObstacles.end(), [&](auto const& a, auto const& b) {
            return std::abs((a.left + a.right) / 2 - x) < std::abs((b.left + b.right) / 2 - x);
        });
        m_flightObstacles.resize(512);
    }
}

FormState Runtime::readState(PlayerObject* p) const {
    FormState s;
    s.form=p->m_isShip?Form::Ship:p->m_isDart?Form::Wave:p->m_isBird?Form::Ufo:p->m_isBall?Form::Ball:
        p->m_isRobot?Form::Robot:p->m_isSpider?Form::Spider:p->m_isSwing?Form::Swing:Form::Cube;
    s.x=p->getPositionX();s.y=p->getPositionY();s.vy=p->m_yVelocity;
    s.vx=std::abs(p->getCurrentXVelocity())*(p->m_isGoingLeft?-1:1);
    auto rect=p->getObjectRect();s.halfWidth=std::clamp(static_cast<double>(rect.size.width)/2,2.,50.);
    s.halfHeight=std::clamp(static_cast<double>(rect.size.height)/2,2.,50.);
    s.scale=p->m_vehicleSize;s.inverted=p->m_isUpsideDown;s.grounded=p->m_isOnGround;s.held=m_held;
    s.gravity=std::abs(p->m_gravity);s.gravityMod=std::clamp(static_cast<double>(std::abs(p->m_gravityMod)),.05,10.);
    s.jump=std::abs(p->m_yStart);s.floor=m_spawnFloor;s.ceiling=1e6;
    if(s.form!=Form::Cube&&s.form!=Form::Robot){
        double bottom=m_layer->getMinPortalY(),top=m_layer->getMaxPortalY();
        if(std::isfinite(bottom)&&std::isfinite(top)&&top-bottom>2*s.halfHeight+8&&s.y>=bottom-30&&s.y<=top+30){s.floor=bottom;s.ceiling=top;}
        else if(s.form==Form::Ship||s.form==Form::Wave||s.form==Form::Ufo||s.form==Form::Swing){s.floor=s.y-150;s.ceiling=s.y+150;}
    }
    s.pressRemaining=m_inputPacer.pressRemainingFrames(m_outerUpdates)/4.;
    s.pressInterval=m_inputPacer.pressIntervalFrames()/4.;s.robotTime=m_robotTime;return s;
}
void Runtime::updateIntelligence(PlayerObject* p){
    auto s=readState(p);double now=m_outerUpdates/4.;
    if(m_observationTime>=0&&now>m_observationTime){
        if(s.form==Form::Robot&&!s.grounded&&s.held&&s.vy*(s.inverted?-1:1)>0)m_robotTime+=now-m_observationTime;
        else if(s.grounded)m_robotTime=0;else if(!s.held)m_robotTime=16;
    }
    m_observationTime=now;s.robotTime=m_robotTime;
    m_calibration.observe({s,now,m_inputEdges,p->m_isDashing||p->m_touchedPad||p->m_touchedRing||p->m_touchedGravityPortal||p->m_isOnSlope});
    m_observation=s;
}
void Runtime::collectInteractions(double minX,double maxX,double minY,double maxY){
    m_interactions.clear();bool complex=false;
    for(auto const& retained:m_interactionObjects){
        auto object=retained.data();
        if(object->m_isDisabled||object->m_isGroupDisabled||object->m_isNoTouch||object->m_isActivated)continue;
        auto type=object->m_objectType;
        if(type==GameObjectType::Solid||type==GameObjectType::Hazard||type==GameObjectType::Decoration||type==GameObjectType::AnimatedHazard)continue;
        auto r=object->getObjectRect();
        if(r.getMaxX()<minX||r.getMinX()>maxX||r.getMaxY()<minY||r.getMinY()>maxY)continue;
        if(m_layer->m_player1->m_touchedRings.count(object->m_uniqueID))continue;
        Interaction i;i.area={r.getMinX(),r.getMaxX(),r.getMinY(),r.getMaxY()};
        i.area.id=static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(object));m_motion.update(i.area,m_outerUpdates/4.);
        if(auto effect=typeinfo_cast<EffectGameObject*>(object))i.reverse=effect->m_isReverse;
        bool include=true;
        switch(type){
            case GameObjectType::YellowJumpRing:i.kind=InteractionKind::YellowOrb;break;
            case GameObjectType::PinkJumpRing:i.kind=InteractionKind::PinkOrb;break;
            case GameObjectType::RedJumpRing:i.kind=InteractionKind::RedOrb;break;
            case GameObjectType::GravityRing:i.kind=InteractionKind::BlueOrb;break;
            case GameObjectType::GreenRing:i.kind=InteractionKind::GreenOrb;break;
            case GameObjectType::DropRing:i.kind=InteractionKind::BlackOrb;break;
            case GameObjectType::YellowJumpPad:i.kind=InteractionKind::YellowPad;break;
            case GameObjectType::PinkJumpPad:i.kind=InteractionKind::PinkPad;break;
            case GameObjectType::RedJumpPad:i.kind=InteractionKind::RedPad;break;
            case GameObjectType::GravityPad:i.kind=InteractionKind::BluePad;break;
            case GameObjectType::SpiderOrb:case GameObjectType::SpiderPad:
                i.kind=type==GameObjectType::SpiderOrb?InteractionKind::SpiderOrb:InteractionKind::SpiderPad;
#ifdef GEODE_IS_WINDOWS
                i.inverted=object->isFacingDown();
#else
                i.inverted=std::cos(object->getRotation()*3.141592653589793/180.)<0;
#endif
                break;
            case GameObjectType::InverseGravityPortal:case GameObjectType::NormalGravityPortal:
                i.kind=InteractionKind::GravityPortal;i.inverted=type==GameObjectType::InverseGravityPortal;break;
            case GameObjectType::GravityTogglePortal:i.kind=InteractionKind::GravityPortal;i.value=1;break;
            case GameObjectType::CubePortal:case GameObjectType::ShipPortal:case GameObjectType::BallPortal:
            case GameObjectType::UfoPortal:case GameObjectType::WavePortal:case GameObjectType::RobotPortal:
            case GameObjectType::SpiderPortal:case GameObjectType::SwingPortal:
                i.kind=InteractionKind::ModePortal;
                i.target=type==GameObjectType::CubePortal?Form::Cube:type==GameObjectType::ShipPortal?Form::Ship:
                    type==GameObjectType::BallPortal?Form::Ball:type==GameObjectType::UfoPortal?Form::Ufo:type==GameObjectType::WavePortal?Form::Wave:
                    type==GameObjectType::RobotPortal?Form::Robot:type==GameObjectType::SpiderPortal?Form::Spider:Form::Swing;break;
            case GameObjectType::RegularSizePortal:case GameObjectType::MiniSizePortal:
                i.kind=InteractionKind::SizePortal;i.value=type==GameObjectType::MiniSizePortal?.6:1;break;
            case GameObjectType::Modifier:{
                auto id=object->m_objectID;
                if(id==200||id==201||id==202||id==203||id==1334){i.kind=InteractionKind::SpeedPortal;
                    i.value=id==200?4.1860014:id==201?5.1930018:id==202?6.4570022:id==203?7.8000026:9.6000032;
                }else include=false;break;
            }
            case GameObjectType::DashRing:case GameObjectType::GravityDashRing:case GameObjectType::CustomRing:
            case GameObjectType::TeleportOrb:case GameObjectType::TeleportPortal:complex=true;include=false;break;
            default:include=false;break;
        }
        if(!include)continue;
        if(i.kind==InteractionKind::YellowPad||i.kind==InteractionKind::PinkPad||i.kind==InteractionKind::RedPad){
#ifdef GEODE_IS_WINDOWS
            i.value=16*m_layer->getBumpMod(m_layer->m_player1,static_cast<int>(type));
#else
            auto f=m_observation.form;double mod=1;
            if(i.kind==InteractionKind::PinkPad)mod=f==Form::Ship?.35:f==Form::Ufo?.4:(f==Form::Ball||f==Form::Spider)?.7:.65;
            if(i.kind==InteractionKind::RedPad)mod=f==Form::Ship?(m_observation.scale<.9?.95:.63):f==Form::Ufo?(m_observation.scale<.9?.98:.6):1.25;
            i.value=mod*16;
#endif
        }
        m_interactions.push_back(i);
    }
    std::stable_sort(m_interactions.begin(),m_interactions.end(),[&](auto const&a,auto const&b){return std::abs((a.area.left+a.area.right)/2-m_observation.x)<std::abs((b.area.left+b.area.right)/2-m_observation.x);});
    if(m_interactions.size()>48)m_interactions.resize(48);
    m_guidanceNote=complex?"Dash/custom/телепорт: ограниченный прогноз":"Прогноз приближённый";
}
void Runtime::guideFlight(PlayerObject* player){
    if(player->m_isDashing){m_guidanceClock.reset();m_guidanceNote="Native dash: сохраняю текущий ввод";m_forecast={};return;}
    if(m_tick<m_flightFrom){m_forecast={};return;}
    auto count=m_layer->m_objects?m_layer->m_objects->count():0;
    if(count!=m_sceneObjectCount)rebuildSceneCache();
    std::uint64_t events=player->m_touchedRings.size()*16ULL+player->m_touchedPad*1ULL+
        player->m_touchedRing*2ULL+player->m_touchedGravityPortal*4ULL+player->m_isOnSlope*8ULL;
    if(m_smoothSearch&&!m_guidanceClock.needsPlan(m_outerUpdates,m_observation,events,m_inputEdges)){
        m_searchDesired=m_cachedDesired;return;
    }
    m_guidedFlight=true;
    auto s=m_calibration.adjusted(m_observation);
    double reach=std::abs(s.vx)*76+s.halfWidth;
    double minX=s.x+std::min(0.,s.vx<0?-reach:reach)-s.halfWidth,maxX=s.x+std::max(0.,s.vx<0?-reach:reach)+s.halfWidth;
    collectObstacles(minX,maxX,std::min(s.floor,s.y-500),std::min(s.ceiling,s.y+700));
    collectInteractions(minX,maxX,s.y-450,s.y+450);
    auto policy=m_search?m_search->policy():0;
    bool interactive=std::any_of(m_interactions.begin(),m_interactions.end(),[&](auto const&i){return std::abs((i.area.left+i.area.right)/2-s.x)<std::abs(s.vx)*42+s.halfWidth;});
    if(interactive||(s.form!=Form::Cube&&s.form!=Form::Ship&&s.form!=Form::Wave)){
        auto decision=chooseFormInput(s,m_flightObstacles,m_interactions,&m_failures,(static_cast<int>(policy%7)-3)*.07,m_showPreview);
        m_searchDesired=decision.down;if(m_showPreview)m_forecast=std::move(decision.forecast);
    }else if(s.form==Form::Cube){
        GroundObservation g{s.x,s.y,s.vy,s.vx,s.halfWidth,s.halfHeight,player->m_isUpsideDown?1e6:m_spawnFloor,
            s.jump*(s.scale<.9?.8:1),s.gravity*s.gravityMod,m_inputPacer.remainingFrames(m_outerUpdates)/4.,s.grounded,s.inverted,s.held,s.pressInterval,s.pressRemaining};
        static constexpr double timing[]{0,-.25,.25,-.5,.5,-1,1,-1.5,1.5,-2,2};
        m_searchDesired=chooseGroundInput(g,m_flightObstacles,timing[policy%11]+m_failures.timingBias(s),m_showPreview?&m_forecast:nullptr).down;
    }else{
        FlightObservation f;f.mode=s.form==Form::Wave?FlightMode::Wave:FlightMode::Ship;
        f.x=s.x;f.y=s.y;f.vy=s.vy;f.vx=s.vx;f.halfWidth=s.halfWidth;f.halfHeight=s.halfHeight;
        f.bottom=s.floor;f.top=s.ceiling;f.scale=s.scale;f.gravity=s.gravityMod;f.inverted=s.inverted;f.held=s.held;
        f.pressInterval=s.pressInterval;f.pressRemaining=s.pressRemaining;f.accelerationScales=s.acceleration;f.slopeScale=s.waveSlope;
        auto tuning=flightTuning(policy+1,m_seed);tuning.heightBias+=m_failures.heightBias(s);
        m_searchDesired=chooseFlightInput(f,m_flightObstacles,tuning,m_showPreview?&m_forecast:nullptr,&m_failures);
    }
    // Unsupported scripted/dash rings still activate only through ordinary
    // native input. Try an alternative policy; never call ringJump directly.
    if(player->m_touchingRings&&player->m_touchingRings->count()&&!m_held&&policy%4==1){
        for(auto object:CCArrayExt<GameObject*>(player->m_touchingRings)){
            auto t=object->m_objectType;
            if(t==GameObjectType::DashRing||t==GameObjectType::GravityDashRing||t==GameObjectType::CustomRing||t==GameObjectType::TeleportOrb)m_searchDesired=true;
        }
    }
    m_cachedDesired=m_searchDesired;
}
void Runtime::rememberFailure(GameObject* object){
    auto s=readState(m_layer->m_player1);m_observation=s;
    FailureRecord f;f.form=s.form;f.x=s.x;f.y=s.y;f.vy=s.vy;f.held=m_held;f.inverted=s.inverted;f.mini=s.scale<.9;
    f.object=object?static_cast<std::uint64_t>(object->m_objectID):0;
    if(s.form==Form::Cube||s.form==Form::Robot)f.kind=s.grounded||s.vy*(s.inverted?-1:1)>=0?FailureKind::LatePress:FailureKind::EarlyPress;
    else if(s.form==Form::Ship||s.form==Form::Wave||s.form==Form::Ufo){
        double hazardY=object?object->getObjectRect().getMidY():(s.y<(s.floor+s.ceiling)/2?s.floor:s.ceiling);
        bool needUp=s.y>hazardY,down=needUp!=s.inverted;f.kind=down?FailureKind::NeedHold:FailureKind::NeedRelease;
    }else f.kind=FailureKind::WrongRoute;
    m_failures.record(f);m_lastFailure=failureName(f.kind);m_status="Поиск: "+m_lastFailure;
}
void Runtime::takeControl(){
    if(m_mode!=Mode::Searching||m_restart){m_status="Помощь доступна во время активной попытки поиска";return;}
    m_mode=Mode::Assisting;m_pendingHuman=false;m_forecast={};m_status="Управление у игрока. Кнопка HELP возвращает боту";
}
void Runtime::returnToBot(){
    if(m_mode!=Mode::Assisting){m_status="Сейчас управление у бота";return;}
    m_plan=prefix(m_trial.actions,m_tick);m_cursor=m_plan.size();m_flightFrom=m_tick;
    m_guidanceClock.reset();
    m_searchDesired=m_held;m_mode=Mode::Searching;m_pendingHuman=false;m_autosaveDirty=true;m_autosaveUrgent=true;
    m_status="Участок помощи записан. Бот продолжает";
}
}
