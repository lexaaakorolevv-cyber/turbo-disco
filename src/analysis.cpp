#include "analysis.hpp"
#include "core.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace rf {
namespace {
bool interaction(MapKind k){return k==MapKind::Orb||k==MapKind::Pad;}
using Interval=std::pair<double,double>;
using Bands=std::vector<Interval>;
void unite(Bands& bands){
    std::sort(bands.begin(),bands.end());Bands result;
    for(auto b:bands){if(result.empty()||b.first>result.back().second)result.push_back(b);else result.back().second=std::max(result.back().second,b.second);}
    bands=std::move(result);
}
// A bounded free-space connectivity graph, not a copy of GD physics. A route
// through its bands is only a soft target for the live collision planner.
std::optional<double> corridor(FormState const&s,std::span<MapRect const> geometry){
    if((s.form!=Form::Ship&&s.form!=Form::Wave)||geometry.empty()||geometry.size()>128||
        !std::isfinite(s.floor)||!std::isfinite(s.ceiling)||s.ceiling-s.floor>2000)return {};
    double rx=s.halfWidth+3,ry=s.halfHeight+3,low=s.floor+ry,high=s.ceiling-ry;
    if(high<=low)return {};
    constexpr unsigned N=32;std::array<Bands,N+1> free,viable;
    double dx=s.vx*98/N;
    double slope=s.form==Form::Wave?(s.scale<.9?2.:1.):8/std::abs(s.vx);
    double rise=std::abs(dx)*slope;
    for(unsigned i=0;i<=N;i++){
        double x=s.x+dx*i,previous=i?x-dx:x;Bands blocked;
        for(auto const&r:geometry)if(r.right+rx>=std::min(x,previous)&&r.left-rx<=std::max(x,previous)&&r.top+ry>low&&r.bottom-ry<high)
            blocked.emplace_back(std::max(low,r.bottom-ry),std::min(high,r.top+ry));
        unite(blocked);double cursor=low;
        for(auto b:blocked){if(b.first>cursor)free[i].emplace_back(cursor,b.first);cursor=std::max(cursor,b.second);}
        if(cursor<high)free[i].emplace_back(cursor,high);
        if(free[i].size()>32)return {};
    }
    viable[N]=free[N];
    for(unsigned i=N;i-->0;){
        for(auto a:free[i])for(auto b:viable[i+1]){
            double lo=std::max(a.first,b.first-rise),hi=std::min(a.second,b.second+rise);
            if(hi>=lo)viable[i].emplace_back(lo,hi);
        }
        unite(viable[i]);if(viable[i].empty())return {};
    }
    if(std::none_of(viable[0].begin(),viable[0].end(),[&](auto b){return s.y>=b.first&&s.y<=b.second;}))return {};
    Bands reachable{{s.y,s.y}};
    unsigned target=static_cast<unsigned>(std::round((s.form==Form::Wave?18.:32.)/98*N));
    for(unsigned i=1;i<=target;i++){
        Bands next;for(auto a:reachable)for(auto b:viable[i]){
            double lo=std::max(b.first,a.first-rise),hi=std::min(b.second,a.second+rise);
            if(hi>=lo)next.emplace_back(lo,hi);
        }
        unite(next);reachable=std::move(next);if(reachable.empty())return {};
    }
    double best=1e300,answer=s.y;
    for(auto b:reachable){double margin=std::min(8.,(b.second-b.first)/4),y=std::clamp(s.y,b.first+margin,b.second-margin);
        double cost=std::abs(y-s.y);if(cost<best){best=cost;answer=y;}}
    return answer;
}
char const* name(MapKind k){
    switch(k){
    case MapKind::Solid:return "Блоки/склоны";case MapKind::Hazard:return "Опасности";
    case MapKind::Orb:return "Орбы";case MapKind::Pad:return "Пады";case MapKind::Mode:return "Порталы форм";
    case MapKind::Gravity:return "Гравитация";case MapKind::Size:return "Размер";case MapKind::Speed:return "Скорость";
    case MapKind::Dual:return "Solo/dual";case MapKind::Complex:return "Dash/custom/телепорт";
    case MapKind::Script:return "Другие игровые триггеры";default:return "?";
    }
}
}
LevelAnalysis analyzeLevel(AnalysisJob job,std::stop_token stop){
    if(job.objects.size()>250000)throw std::runtime_error("Analysis object limit exceeded");
    if(static_cast<unsigned>(job.startForm)>=8)throw std::runtime_error("Invalid initial form");
    LevelAnalysis result;result.scanned=job.scanned;result.twoPlayer=job.twoPlayer;result.startsDual=job.startsDual;
    result.forms[static_cast<unsigned>(job.startForm)]=true;
    result.objects=std::move(job.objects);
    for(auto const&o:result.objects){
        if(!std::isfinite(o.x)||!std::isfinite(o.y)||static_cast<unsigned>(o.kind)>=result.counts.size()||static_cast<unsigned>(o.form)>=8)
            throw std::runtime_error("Invalid analysis geometry");
        ++result.counts[static_cast<unsigned>(o.kind)];result.uncertain+=o.uncertain;
        if(o.bounds){auto const&r=*o.bounds;
            if(!std::isfinite(r.left)||!std::isfinite(r.right)||!std::isfinite(r.bottom)||!std::isfinite(r.top)||r.right<r.left||r.top<r.bottom)
                throw std::runtime_error("Invalid analysis bounds");
            result.maxHalfWidth=std::max(result.maxHalfWidth,std::max(std::abs(o.x-r.left),std::abs(o.x-r.right)));
        }
        if(o.kind==MapKind::Mode)result.forms[static_cast<unsigned>(o.form)]=true;
        result.minX=std::min(result.minX,o.x);result.maxX=std::max(result.maxX,o.x);
    }
    if(stop.stop_requested())throw std::runtime_error("Analysis cancelled");
    std::stable_sort(result.objects.begin(),result.objects.end(),[](auto const&a,auto const&b){return a.x<b.x;});
    std::optional<MapObject> previous;
    for(auto const&o:result.objects){
        if(stop.stop_requested())throw std::runtime_error("Analysis cancelled");
        if(o.kind==MapKind::Mode||o.kind==MapKind::Speed||o.kind==MapKind::Dual||o.kind==MapKind::Complex||o.kind==MapKind::Script)previous.reset();
        if(!interaction(o.kind)||o.uncertain)continue;
        if(previous&&o.x-previous->x>1&&o.x-previous->x<=300&&std::abs(o.y-previous->y)<=240)++result.chains;
        previous=o;
    }
    std::ostringstream out;out<<"АНАЛИЗ ГОТОВ\n\nПроверено объектов: "<<result.scanned<<"\n";
    out<<(result.twoPlayer?"2-player: две независимые кнопки":result.startsDual?"Dual: общая кнопка":"Solo; переходы dual учитываются")<<"\nФормы: ";
    for(unsigned i=0;i<8;i++)if(result.forms[i])out<<formName(static_cast<Form>(i))<<"  ";
    out<<"\n\n";
    for(unsigned i=0;i<result.counts.size();i++)if(result.counts[i])out<<name(static_cast<MapKind>(i))<<": "<<result.counts[i]<<"\n";
    out<<"Кандидаты связок: "<<result.chains<<"\n\nКак используется\n";
    out<<"Перед разнесёнными связками орбов/падов и сериями препятствий поиск расширяет прогноз до ~1,6 с. "
       <<"Для корабля/волны карта связности коридоров помогает заранее выбрать ветку без видимого тупика. "
       <<"На простых участках остаётся обычный прогноз. Реальное состояние и столкновения всегда берутся из игры.\n\n";
    out<<"Карта переходов (примерное положение по X)\n";
    unsigned shown=0,total=0;
    for(auto const&o:result.objects){
        if(o.kind!=MapKind::Mode&&o.kind!=MapKind::Gravity&&o.kind!=MapKind::Speed&&o.kind!=MapKind::Dual&&o.kind!=MapKind::Size)continue;
        ++total;if(shown++>=48)continue;
        double percent=100*(o.x-result.minX)/std::max(1.,result.maxX-result.minX);
        out<<std::fixed<<std::setprecision(1)<<percent<<"%: "<<(o.kind==MapKind::Mode?formName(o.form):name(o.kind));
        if(o.uncertain)out<<" (условный)";out<<"\n";
    }
    if(!total)out<<"Переходов не найдено.\n";if(total>48)out<<"…ещё "<<total-48<<" переходов в JSON-отчёте.\n";
    out<<"\nОграничения\nЭто снимок текущих загруженных объектов, а не готовое прохождение. "
       <<"Портал может не сработать, если игрок пройдёт мимо. Проценты по X приблизительные. "
       <<"Будущие перемещения, переключения групп и скрипты не симулируются.\n";
    if(result.uncertain)out<<"Объектов с группами/неопределённым состоянием: "<<result.uncertain<<".\n";
    out<<"Dash/custom/телепорты и другие игровые триггеры ограничивают структурный прогноз. "
       <<"Успешный макрос всё равно проверяется полным повтором с начала.\n";
    result.report=out.str();return result;
}
AnalysisHint LevelAnalysis::hint(FormState const&s) const{
    AnalysisHint result;
    if(!std::isfinite(s.x)||!std::isfinite(s.y)||!std::isfinite(s.vx)||std::abs(s.vx)<.1)return result;
    double minX=std::min(s.x,s.x+s.vx*98),maxX=std::max(s.x,s.x+s.vx*98);
    double a=minX-s.halfWidth-maxHalfWidth,b=maxX+s.halfWidth+maxHalfWidth;
    auto first=std::lower_bound(objects.begin(),objects.end(),a,[](auto const&o,double x){return o.x<x;});
    double boundary=99,lastInteraction=0,lastObstacle=0,lastHazard=-1e300;unsigned visited=0;
    std::vector<MapRect> geometry;
    for(auto it=first;it!=objects.end()&&it->x<=b;++it){
        if(++visited>4096)return {};
        auto const&o=*it;
        if(o.bounds&&!o.uncertain&&(o.kind==MapKind::Solid||o.kind==MapKind::Hazard)&&o.bounds->right>=minX-s.halfWidth&&o.bounds->left<=maxX+s.halfWidth){
            if(geometry.size()<129)geometry.push_back(*o.bounds);
        }
        double t=(o.x-s.x)/s.vx;
        if(t<0||t>98)continue;
        if(o.kind==MapKind::Script)return {};
        if(std::abs(o.y-s.y)>std::max(240.,s.halfHeight*4))continue;
        if(o.kind==MapKind::Complex)return {};
        if(o.kind==MapKind::Mode||o.kind==MapKind::Speed||o.kind==MapKind::Dual)boundary=std::min(boundary,t);
        if(o.uncertain)continue;
        if(interaction(o.kind)){++result.interactions;lastInteraction=std::max(lastInteraction,t);}
        if(o.kind==MapKind::Hazard){if(std::abs(lastHazard-o.x)>30){lastHazard=o.x;++result.obstacles;lastObstacle=std::max(lastObstacle,t);}}
    }
    // Do not plan across a native form/speed/dual handoff or infer a trigger.
    if(boundary<98)return {};
    bool chain=result.interactions>=2&&lastInteraction>42;
    bool series=result.obstacles>=3&&lastObstacle>52;
    if(chain||series)result.horizon=98;
    if(result.horizon>42)result.targetY=corridor(s,geometry);
    return result;
}
nlohmann::json LevelAnalysis::toJson() const{
    nlohmann::json events=nlohmann::json::array();
    for(auto const&o:objects)if(o.kind==MapKind::Mode||o.kind==MapKind::Gravity||o.kind==MapKind::Size||o.kind==MapKind::Speed||o.kind==MapKind::Dual)
        events.push_back({{"x",o.x},{"y",o.y},{"kind",name(o.kind)},{"form",o.kind==MapKind::Mode?nlohmann::json(formName(o.form)):nlohmann::json(nullptr)},{"uncertain",o.uncertain}});
    return {{"schema",1},{"scanned",scanned},{"counts",counts},{"uncertain",uncertain},{"chains",chains},
        {"two_player",twoPlayer},{"starts_dual",startsDual},{"transitions",events},{"report",report},
        {"scope","current loaded geometry; structural hints only; native gameplay and full replay required"}};
}
AnalysisWorker::~AnalysisWorker(){m_thread.request_stop();if(m_thread.joinable())m_thread.join();}
bool AnalysisWorker::start(AnalysisJob job){
    if(m_busy.exchange(true))return false;
    if(m_thread.joinable())m_thread.join();
    {std::lock_guard lock(m_mutex);m_result.reset();m_error.clear();}
    m_thread=std::jthread([this,job=std::move(job)](std::stop_token stop)mutable{
        try{
            auto path=job.path;auto environment=job.environment;
            auto result=analyzeLevel(std::move(job),stop);
            if(!stop.stop_requested()){
                if(!path.empty()){auto json=result.toJson();json["environment"]=std::move(environment);saveJsonFile(path,json);}
                std::lock_guard lock(m_mutex);if(!stop.stop_requested())m_result=std::move(result);
            }
        }catch(std::exception const&e){std::lock_guard lock(m_mutex);if(!stop.stop_requested())m_error=e.what();}
        m_busy=false;
    });return true;
}
std::optional<LevelAnalysis> AnalysisWorker::take(std::string& error){
    std::lock_guard lock(m_mutex);error=std::move(m_error);m_error.clear();auto result=std::move(m_result);m_result.reset();return result;
}
}
