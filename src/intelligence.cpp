#include "intelligence.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace rf {
char const* formName(Form f){static char const* names[]{"Куб","Корабль","Волна","Шар","UFO","Робот","Паук","Swing"};return names[static_cast<int>(f)];}
char const* failureName(FailureKind k){static char const* names[]{"Причина не определена","Вероятно позднее нажатие","Вероятно раннее нажатие","Вероятно нужен зажим","Вероятно нужно отпускание","Вероятно неверный маршрут"};return names[static_cast<int>(k)];}
std::size_t Calibration::key(FormState const&s){return static_cast<int>(s.form)*4+(s.scale<.9?2:0)+(s.inverted?1:0);}
void Calibration::observe(PhysicsSample const& current){
    auto previous=m_previous;
    if(previous && current.time<=previous->time)return;
    m_previous=current;
    if(!previous)return;
    auto const&a=previous->state;auto const&b=current.state;
    double dt=current.time-previous->time;
    if(dt<.01||dt>1||current.disturbed||previous->disturbed||key(a)!=key(b)||
        std::abs(a.gravityMod-b.gravityMod)>.001||std::abs(a.vx-b.vx)>.1||
        std::abs(b.x-a.x-a.vx*dt)>std::max(3.,std::abs(a.vx*dt)*.6))return;
    auto update=[&](int slot,double measured,double expected){
        if(std::abs(expected)<1e-6)return;
        double ratio=measured/expected;
        if(!std::isfinite(ratio)||ratio<.65||ratio>1.35)return;
        auto&e=m_buckets[key(b)].values[slot];
        e.ratio=std::clamp(e.ratio+(ratio-e.ratio)/(std::min(e.samples,31u)+1),.65,1.35);
        e.samples=std::min(e.samples+1,100000u);
    };
    double sign=a.inverted?-1:1,acc=(b.vy-a.vy)*sign/dt;
    bool unchanged=previous->edge==current.edge;
    double g=a.gravity*a.gravityMod;
    if(a.form==Form::Wave){if(unchanged)update(6,(b.y-a.y)/(b.x-a.x),sign*std::copysign(1.,a.vx)*(a.held?1:-1)*(a.scale<.9?2:1));return;}
    if(!a.grounded&&!b.grounded&&unchanged){
        if(a.form==Form::Ship){bool falling=a.vy*sign<0;int slot=(a.held?0:2)+(falling?1:0);double mini=a.scale<.9?.85:1;
            double base=a.held?(falling?.4791:.3833):-(falling?.3066:.4599);
            if(std::abs(a.vy)<6&&std::abs(b.vy)<6)update(slot,acc,base*a.gravityMod/mini);
        }else if(a.form==Form::Ufo){double mini=a.scale<.9?.85:1;update(4,acc,-.958199*a.gravityMod*(a.vy*sign<0?.4:.6)/mini);
        }else if(a.form==Form::Swing){if(std::abs(a.vy)<6&&std::abs(b.vy)<6)update(4,acc,-.958199*a.gravityMod*(a.scale<.9?.6:.4));
        }else if(a.form!=Form::Robot||!a.held){update(4,acc,-g*((a.form==Form::Ball||a.form==Form::Spider)?.6:a.form==Form::Robot?.9:1));}
    }
    if(a.grounded&&!b.grounded&&b.held&&(b.form==Form::Cube||b.form==Form::Robot))
        update(5,b.vy*sign+(a.form==Form::Robot?0:g*dt),a.jump*(a.scale<.9?.8:1)*(a.form==Form::Robot?.5:1));
}
FormState Calibration::adjusted(FormState s)const{
    auto const&v=m_buckets[key(s)].values;
    auto ratio=[&](int i){return v[i].samples>=4?v[i].ratio:1.;};
    for(int i=0;i<4;i++)s.acceleration[i]=ratio(i);
    s.gravity*=ratio(4);s.jump*=ratio(5);s.waveSlope*=ratio(6);return s;
}
unsigned Calibration::samples()const{unsigned n=0;for(auto const&b:m_buckets)for(auto const&e:b.values)n+=e.samples;return n;}
nlohmann::json Calibration::toJson()const{auto j=nlohmann::json::array();for(auto const&b:m_buckets){auto row=nlohmann::json::array();for(auto const&e:b.values)row.push_back({e.ratio,e.samples});j.push_back(row);}return j;}
Calibration Calibration::fromJson(nlohmann::json const&j){
    Calibration c;if(!j.is_array()||j.size()!=32)throw std::runtime_error("Invalid calibration size");
    for(int i=0;i<32;i++){if(!j[i].is_array()||j[i].size()!=7)throw std::runtime_error("Invalid calibration row");for(int k=0;k<7;k++){
        auto const&r=j[i][k];if(!r.is_array()||r.size()!=2||!r[1].is_number_integer()||r[1].get<double>()<0||r[1].get<double>()>100000)throw std::runtime_error("Invalid calibration count");
        double value=r[0].get<double>();if(!std::isfinite(value)||value<.65||value>1.35)throw std::runtime_error("Invalid calibration ratio");c.m_buckets[i].values[k]={value,r[1].get<unsigned>()};}}
    return c;
}
void MotionTracker::update(Obstacle&o,double time){
    double x=(o.left+o.right)/2,y=(o.bottom+o.top)/2;
    if(!m_entries.contains(o.id)&&m_entries.size()>=8192){
        for(auto it=m_entries.begin();it!=m_entries.end();)if(time-it->second.time>16)it=m_entries.erase(it);else++it;
        if(m_entries.size()>=8192)return;
    }
    auto [it,inserted]=m_entries.try_emplace(o.id,Entry{x,y,time});auto&e=it->second;double dt=time-e.time;
    if(!inserted&&dt>.01&&dt<=2){double vx=(x-e.x)/dt,vy=(y-e.y)/dt;
        if(std::isfinite(vx)&&std::isfinite(vy)&&std::hypot(vx,vy)<80){e.vx=e.samples?e.vx*.5+vx*.5:vx;e.vy=e.samples?e.vy*.5+vy*.5:vy;e.samples=std::min(e.samples+1,100u);}else{e.vx=e.vy=0;e.samples=0;}
        e.x=x;e.y=y;e.time=time;
    }else if(dt>2){e={x,y,time};}
    if(e.samples>=2){o.vx=e.vx;o.vy=e.vy;o.motionHorizon=8;}
    if(m_entries.size()>8192){for(auto p=m_entries.begin();p!=m_entries.end();)if(time-p->second.time>16)p=m_entries.erase(p);else++p;}
}
void FailureMemory::record(FailureRecord const& f){
    if(!std::isfinite(f.x)||!std::isfinite(f.y)||!std::isfinite(f.vy))return;
    for(auto&r:m_records)if(r.form==f.form&&r.kind==f.kind&&r.held==f.held&&r.inverted==f.inverted&&r.mini==f.mini&&
        r.object==f.object&&std::abs(r.x-f.x)<20&&std::abs(r.y-f.y)<24){r.count=std::min(r.count+1,1000u);return;}
    if(m_records.size()>=256)m_records.erase(m_records.begin());m_records.push_back(f);m_indexDirty=true;
}
void FailureMemory::index() const {
    if(!m_indexDirty)return;
    m_byX.resize(m_records.size());for(std::size_t i=0;i<m_byX.size();i++)m_byX[i]=i;
    std::stable_sort(m_byX.begin(),m_byX.end(),[&](auto a,auto b){return m_records[a].x<m_records[b].x;});
    m_indexDirty=false;
}
double FailureMemory::penalty(FormState const&s,bool down)const{
    if(m_records.empty())return 0;index();double cost=0;
    auto first=std::lower_bound(m_byX.begin(),m_byX.end(),s.x-28,[&](auto i,double x){return m_records[i].x<x;});
    // Preserve insertion order of additions for identical floating point costs.
    std::array<std::size_t,256> hits;std::size_t count=0;
    for(auto p=first;p!=m_byX.end()&&m_records[*p].x<s.x+28;++p)hits[count++]=*p;
    std::sort(hits.begin(),hits.begin()+count);
    for(std::size_t i=0;i<count;i++){auto const&r=m_records[hits[i]];
        if(r.form==s.form&&r.inverted==s.inverted&&r.mini==(s.scale<.9)&&r.held==down&&std::abs(s.x-r.x)<28&&std::abs(s.y-r.y)<32&&std::abs(s.vy-r.vy)<5)
            cost+=std::min(r.count,10u)*.6*(1-std::abs(s.x-r.x)/28);
    }return std::min(cost,8.);
}
double FailureMemory::timingBias(FormState const&s)const{double b=0;for(auto const&r:m_records)if(r.form==s.form&&r.inverted==s.inverted&&std::abs(s.x-r.x)<120)
    b+=(r.kind==FailureKind::LatePress?.15:r.kind==FailureKind::EarlyPress?-.15:0)*std::min(r.count,8u);return std::clamp(b,-2.,2.);}
double FailureMemory::heightBias(FormState const&s)const{double b=0;for(auto const&r:m_records)if(r.form==s.form&&r.inverted==s.inverted&&std::abs(s.x-r.x)<160)
    b+=(r.kind==FailureKind::NeedHold?1:r.kind==FailureKind::NeedRelease?-1:0)*.015*std::min(r.count,8u)*(s.inverted?-1:1);return std::clamp(b,-.2,.2);}
nlohmann::json FailureMemory::toJson()const{auto j=nlohmann::json::array();for(auto const&r:m_records)j.push_back({static_cast<int>(r.form),static_cast<int>(r.kind),r.x,r.y,r.vy,r.held,r.inverted,r.mini,r.count,r.object});return j;}
FailureMemory FailureMemory::fromJson(nlohmann::json const&j){
    FailureMemory m;if(!j.is_array()||j.size()>256)throw std::runtime_error("Invalid failure memory size");
    for(auto const&r:j){if(!r.is_array()||r.size()!=10)throw std::runtime_error("Invalid failure record");
        if(!r[0].is_number_integer()||!r[1].is_number_integer()||r[0].get<double>()<0||r[0].get<double>()>7||r[1].get<double>()<0||r[1].get<double>()>5)throw std::runtime_error("Invalid failure mode");
        int form=r[0].get<int>(),kind=r[1].get<int>();double count=r[8].get<double>();
        if(form<0||form>7||kind<0||kind>5||!r[8].is_number_integer()||count<1||count>1000||!r[9].is_number_integer()||r[9].get<double>()<0)throw std::runtime_error("Invalid failure fields");
        FailureRecord f{static_cast<Form>(form),static_cast<FailureKind>(kind),r[2].get<double>(),r[3].get<double>(),r[4].get<double>(),r[5].get<bool>(),r[6].get<bool>(),r[7].get<bool>(),r[8].get<unsigned>(),r[9].get<std::uint64_t>()};
        if(!std::isfinite(f.x)||!std::isfinite(f.y)||!std::isfinite(f.vy)||std::abs(f.x)>1e9||std::abs(f.y)>1e9||std::abs(f.vy)>1e6)throw std::runtime_error("Invalid failure coordinates");m.m_records.push_back(f);
    }return m;
}
}
