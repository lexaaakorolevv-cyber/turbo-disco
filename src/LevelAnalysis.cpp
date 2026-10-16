#include "Runtime.hpp"
#include <algorithm>
#include <cmath>
using namespace geode::prelude;
namespace rf {
namespace {
std::optional<MapObject> mapObject(GameObject* object){
    MapObject result;result.x=object->getPositionX();result.y=object->getPositionY();
    result.uncertain=object->m_groupCount>0||object->m_isGroupDisabled||object->m_isDisabled;
    switch(object->m_objectType){
    case GameObjectType::Solid:case GameObjectType::Breakable:result.kind=MapKind::Solid;break;
    case GameObjectType::Slope:result.kind=object->m_slopeIsHazard?MapKind::Hazard:MapKind::Solid;break;
    case GameObjectType::Hazard:case GameObjectType::AnimatedHazard:result.kind=MapKind::Hazard;break;
    case GameObjectType::YellowJumpRing:case GameObjectType::PinkJumpRing:case GameObjectType::RedJumpRing:
    case GameObjectType::GravityRing:case GameObjectType::GreenRing:case GameObjectType::DropRing:case GameObjectType::SpiderOrb:
        result.kind=MapKind::Orb;break;
    case GameObjectType::YellowJumpPad:case GameObjectType::PinkJumpPad:case GameObjectType::RedJumpPad:
    case GameObjectType::GravityPad:case GameObjectType::SpiderPad:result.kind=MapKind::Pad;break;
    case GameObjectType::CubePortal:result.kind=MapKind::Mode;result.form=Form::Cube;break;
    case GameObjectType::ShipPortal:result.kind=MapKind::Mode;result.form=Form::Ship;break;
    case GameObjectType::WavePortal:result.kind=MapKind::Mode;result.form=Form::Wave;break;
    case GameObjectType::BallPortal:result.kind=MapKind::Mode;result.form=Form::Ball;break;
    case GameObjectType::UfoPortal:result.kind=MapKind::Mode;result.form=Form::Ufo;break;
    case GameObjectType::RobotPortal:result.kind=MapKind::Mode;result.form=Form::Robot;break;
    case GameObjectType::SpiderPortal:result.kind=MapKind::Mode;result.form=Form::Spider;break;
    case GameObjectType::SwingPortal:result.kind=MapKind::Mode;result.form=Form::Swing;break;
    case GameObjectType::InverseGravityPortal:case GameObjectType::NormalGravityPortal:case GameObjectType::GravityTogglePortal:
        result.kind=MapKind::Gravity;break;
    case GameObjectType::RegularSizePortal:case GameObjectType::MiniSizePortal:result.kind=MapKind::Size;break;
    case GameObjectType::DualPortal:case GameObjectType::SoloPortal:result.kind=MapKind::Dual;break;
    case GameObjectType::DashRing:case GameObjectType::GravityDashRing:case GameObjectType::CustomRing:
    case GameObjectType::TeleportOrb:case GameObjectType::TeleportPortal:result.kind=MapKind::Complex;break;
    case GameObjectType::Modifier:
        if(object->m_objectID==200||object->m_objectID==201||object->m_objectID==202||object->m_objectID==203||object->m_objectID==1334){result.kind=MapKind::Speed;break;}
        [[fallthrough]];
    default:
        if(object->m_isTrigger&&!object->m_isColorTrigger){result.kind=MapKind::Script;result.uncertain=true;break;}
        return std::nullopt;
    }
    if(!std::isfinite(result.x)||!std::isfinite(result.y))return std::nullopt;
    if(result.kind==MapKind::Solid||result.kind==MapKind::Hazard){auto r=object->getObjectRect();result.bounds=MapRect{r.getMinX(),r.getMaxX(),r.getMinY(),r.getMaxY()};}
    return result;
}
Form initialForm(int native){
    switch(native){case 1:return Form::Ship;case 2:return Form::Ball;case 3:return Form::Ufo;case 4:return Form::Wave;
        case 5:return Form::Robot;case 6:return Form::Spider;case 7:return Form::Swing;default:return Form::Cube;}
}
}
void Runtime::startAnalysis(){
    if(active()){m_analysisMessage="Сначала остановите поиск/запись/повтор, затем нажмите «Повторить».";return;}
    if(analysisBusy())return;
    if(!syncActiveLayer()){m_analysisMessage=readinessError();return;}
    if(m_layer->m_isPlatformer||m_layer->m_levelSettings->m_platformerMode){m_analysisMessage="Platformer не поддерживается анализатором.";return;}
    if(m_layer->m_isPracticeMode||m_layer->m_startPosObject||m_layer->m_levelSettings->m_startsWithStartPos){m_analysisMessage="Выйдите из practice/Start Position перед Analyze.";return;}
    if(!m_menuOpen){m_analysisMessage="Analyze доступен через паузу → BOT.";return;}
    if(m_analysisWorker.busy()){m_analysisMessage="Предыдущий анализ останавливается. Повторите через секунду.";return;}
    if(!m_layer->m_objects){m_analysisMessage="Объекты уровня ещё не загружены.";return;}
    m_analysisTotal=m_layer->m_objects->count();
    if(m_analysisTotal>250000){m_analysisMessage="Лимит анализа: 250000 объектов. Поиск доступен без анализа.";return;}
    m_analysis.reset();m_analysisJob={};m_analysisIndex=0;
    m_analysisJob.startForm=initialForm(m_layer->m_levelSettings->m_startMode);
    m_analysisJob.twoPlayer=twoPlayerEnabled();m_analysisJob.startsDual=m_layer->m_levelSettings->m_startDual;
    m_analysisJob.environment=environment();
    auto const& level=m_analysisJob.environment.at("level");
    m_analysisJob.path=Mod::get()->getSaveDir()/"macros"/fmt::format("{}_{}.analysis.json",level.at("id").get<int>(),level.at("data_sha256").get<std::string>().substr(0,16));
    m_analysisCapturing=true;m_analysisMessage="Сканирование загруженных объектов…";
}
void Runtime::updateAnalysis(){
    if(!analysisBusy())return;
    if(!m_menuOpen||!levelReady()){cancelAnalysis();return;}
    if(m_analysisCapturing){
        if(!m_layer->m_objects||m_layer->m_objects->count()!=m_analysisTotal){cancelAnalysis();m_analysisMessage="Список объектов изменился. Запустите анализ ещё раз.";return;}
        // Only snapshot a small chunk on the game thread. Full sorting,
        // structure analysis, report formatting and JSON run on copied data.
        auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(2);
        unsigned end=std::min(m_analysisTotal,m_analysisIndex+512);
        for(;m_analysisIndex<end;++m_analysisIndex){
            auto object=static_cast<GameObject*>(m_layer->m_objects->objectAtIndex(m_analysisIndex));
            if(auto value=mapObject(object))m_analysisJob.objects.push_back(*value);
            if((m_analysisIndex&31)==31&&std::chrono::steady_clock::now()>=deadline){++m_analysisIndex;break;}
        }
        if(m_analysisIndex<m_analysisTotal)return;
        m_analysisJob.scanned=m_analysisTotal;m_analysisCapturing=false;
        if(m_analysisWorker.start(std::move(m_analysisJob))){m_analysisAwaiting=true;m_analysisMessage="Строю карту и сохраняю JSON-отчёт…";}
        else m_analysisMessage="Работник анализа занят. Повторите анализ.";
    }
    if(m_analysisAwaiting&&!m_analysisWorker.busy()){
        std::string error;auto result=m_analysisWorker.take(error);m_analysisAwaiting=false;
        if(result){m_analysis=std::move(result);m_analysisMessage="Analyze готов. Карта применяется к следующему поиску.";m_status=m_analysisMessage;}
        else m_analysisMessage=error.empty()?"Анализ отменён.":"Ошибка анализа: "+error;
    }
}
void Runtime::cancelAnalysis(){
    bool running=analysisBusy();m_analysisCapturing=m_analysisAwaiting=false;m_analysisWorker.cancel();m_analysisJob={};
    if(running)m_analysisMessage="Анализ остановлен. Можно запустить заново.";
}
std::string Runtime::analysisText() const{
    if(m_analysisCapturing)return fmt::format("Analyze: {:.0f}%\nОбъекты: {} / {}\n\nСканирование идёт порциями. Можно остановить или закрыть окно.",100.*m_analysisIndex/std::max(1u,m_analysisTotal),m_analysisIndex,m_analysisTotal);
    if(m_analysisAwaiting)return m_analysisMessage;
    if(m_analysis)return m_analysisMessage+"\n\n"+m_analysis->report+"\nJSON: <Geode save dir>/macros/<level>_<hash>.analysis.json\n";
    return m_analysisMessage;
}
}
