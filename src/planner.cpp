#include "planner.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace rf {
namespace {
bool groundForm(Form f){return f==Form::Cube||f==Form::Robot||f==Form::Ball||f==Form::Spider;}
double sign(FormState const&s){return s.inverted?-1:1;}
bool snapSpider(FormState& s,std::span<Obstacle const> objects,double time){
    double target=s.inverted?s.floor+s.halfHeight:s.ceiling-s.halfHeight;
    bool found=std::isfinite(target)&&std::abs(target-s.y)<2000;
    for(auto const&base:objects){if(!base.solid)continue;auto o=obstacleAt(base,time);
        if(s.x+s.halfWidth<=o.left||s.x-s.halfWidth>=o.right)continue;
        double y=s.inverted?o.top+s.halfHeight:o.bottom-s.halfHeight;
        if((s.inverted&&y<s.y&&(!found||y>target))||(!s.inverted&&y>s.y&&(!found||y<target))){target=y;found=true;}
    }
    if(!found)return false;
    s.y=target;s.inverted=!s.inverted;s.vy=0;s.grounded=true;return true;
}
bool isOrb(InteractionKind k){return k<=InteractionKind::BlackOrb||k==InteractionKind::SpiderOrb;}
void activate(FormState&s,Interaction const&o,std::span<Obstacle const>objects,double time){
    double oldSign=sign(s),mini=s.scale<.9?.8:1;
    switch(o.kind){
        case InteractionKind::ModePortal:s.form=o.target;s.grounded=false;s.robotTime=0;break;
        case InteractionKind::GravityPortal:s.inverted=o.value==1?!s.inverted:o.inverted;s.grounded=false;break;
        case InteractionKind::SizePortal:{double target=o.value<.9?.6:1;double ratio=target/s.scale;s.halfWidth*=ratio;s.halfHeight*=ratio;s.scale=target;break;}
        case InteractionKind::SpeedPortal:s.vx=std::copysign(o.value,s.vx);break;
        case InteractionKind::SpiderOrb:case InteractionKind::SpiderPad:
            s.inverted=o.inverted; snapSpider(s,objects,time);break;
        case InteractionKind::YellowPad:case InteractionKind::PinkPad:case InteractionKind::RedPad:
            s.vy=oldSign*(o.value>0?o.value:16)*mini*((s.form==Form::Ball||s.form==Form::Spider||s.form==Form::Swing)?.6:1);s.grounded=false;break;
        case InteractionKind::BluePad:s.inverted=!s.inverted;s.vy=sign(s)*s.jump*.8*mini;s.grounded=false;break;
        case InteractionKind::BlackOrb:s.vy=-oldSign*(groundForm(s.form)?15:14);s.grounded=false;break;
        default:{double factor=1;
            if(o.kind==InteractionKind::PinkOrb)factor=s.form==Form::Ship?.37:s.form==Form::Ufo?.42:s.form==Form::Ball?.77:.72;
            if(o.kind==InteractionKind::RedOrb)factor=s.form==Form::Ship?(s.scale<.9?1.4:1):s.form==Form::Ufo?(s.scale<.9?1.36:1.02):s.form==Form::Robot?1.28:(s.form==Form::Ball||s.form==Form::Spider)?1.34:1.38;
            if(o.kind==InteractionKind::BlueOrb)factor=.8;
            if(o.kind==InteractionKind::GreenOrb){s.inverted=!s.inverted;oldSign=sign(s);if(s.form==Form::Ship)factor=.7;}
            s.vy=oldSign*s.jump*factor*mini;
            if(s.form==Form::Ball||s.form==Form::Spider)s.vy*=.7;
            if(s.form==Form::Swing)s.vy*=.6;
            if(o.kind==InteractionKind::BlueOrb)s.inverted=!s.inverted;
            s.grounded=false;break;
        }
    }
    if(o.reverse)s.vx=-s.vx;
}
}
bool advanceForm(FormState&s,bool down,double dt,std::span<Obstacle const>objects,
    std::span<Interaction const>interactions,std::uint64_t&used,double time){
    if(!std::isfinite(dt)||dt<=0||dt>2)return false;
    bool rising=down&&!s.held;
    if(rising&&s.pressRemaining>1e-8)return false;
    s.pressRemaining=std::max(0.,(rising?s.pressInterval:s.pressRemaining)-dt);
    double oldX=s.x,oldY=s.y,sgn=sign(s),g=s.gravity*s.gravityMod,mini=s.scale<.9?.8:1;
    bool teleported=false;
    if(s.form==Form::Ball&&s.grounded&&rising){s.inverted=!s.inverted;s.vy=sgn*s.jump*mini*.6;s.grounded=false;}
    else if(s.form==Form::Spider&&s.grounded&&rising){if(!snapSpider(s,objects,time))return false;teleported=true;}
    else if((s.form==Form::Cube||s.form==Form::Robot)&&s.grounded&&down){s.vy=sgn*s.jump*mini*(s.form==Form::Robot?.5:1);s.grounded=false;s.robotTime=0;}
    else if(s.form==Form::Ufo&&rising){double v=(s.scale<.9?8:7)*(s.scale<.9?.85:1);if(s.vy*sgn<v)s.vy=sgn*v;s.grounded=false;}
    else if(s.form==Form::Swing&&rising){s.inverted=!s.inverted;s.vy*=.8;}
    sgn=sign(s);
    if(s.form==Form::Wave)s.vy=std::abs(s.vx)*sgn*(down?1:-1)*(s.scale<.9?2:1)*s.waveSlope;
    else if(s.form==Form::Ship){bool falling=s.vy*sgn<0;int branch=(down?0:2)+(falling?1:0);double a=down?(falling?.4791:.3833):-(falling?.3066:.4599);
        s.vy+=sgn*a*s.gravityMod/(s.scale<.9?.85:1)*s.acceleration[branch]*dt;
        s.vy=std::clamp(s.vy,sgn>0?-6.4/(s.scale<.9?.85:1):-8/(s.scale<.9?.85:1),sgn>0?8/(s.scale<.9?.85:1):6.4/(s.scale<.9?.85:1));
    }else if(s.form==Form::Ufo){s.vy-=sgn*g*(s.vy*sgn<0?.4:.6)/(s.scale<.9?.85:1)*dt;s.vy=sgn*std::clamp(s.vy*sgn,-6.4/(s.scale<.9?.85:1),8/(s.scale<.9?.85:1));}
    else if(s.form==Form::Swing){s.vy-=sgn*g*(s.scale<.9?.6:.4)*dt;s.vy=std::clamp(s.vy,-8.,8.);}
    else if(!s.grounded){bool boosting=s.form==Form::Robot&&down&&s.robotTime<15&&s.vy*sgn>0;
        if(!boosting)s.vy-=sgn*g*(s.form==Form::Robot?.9:(s.form==Form::Ball||s.form==Form::Spider)?.6:1)*dt;
        if(s.form==Form::Robot){s.robotTime+=dt;if(!down)s.robotTime=16;}
        s.vy=sgn*std::max(s.vy*sgn,-15.);
    }
    s.x+=s.vx*dt;s.y+=s.vy*dt;s.held=down;
    for(std::size_t i=0;i<interactions.size()&&i<64;++i){auto mask=std::uint64_t(1)<<i;auto const&o=interactions[i];
        if((used&mask)||!forecastOverlap(oldX,oldY,s.x,s.y,s.halfWidth,s.halfHeight,o.area,time,time+dt))continue;
        if(isOrb(o.kind)&&!down)continue;
        activate(s,o,objects,time+dt);used|=mask;
        if(o.kind==InteractionKind::SpiderOrb||o.kind==InteractionKind::SpiderPad)teleported=true;
    }
    if(groundForm(s.form)){
        bool landed=false;double target=s.inverted?-std::numeric_limits<double>::infinity():std::numeric_limits<double>::infinity();
        if(!s.inverted&&oldY-s.halfHeight>=s.floor-.1&&s.y-s.halfHeight<=s.floor){target=s.floor+s.halfHeight;landed=true;}
        if(s.inverted&&oldY+s.halfHeight<=s.ceiling+.1&&s.y+s.halfHeight>=s.ceiling){target=s.ceiling-s.halfHeight;landed=true;}
        for(auto const&base:objects){if(!base.solid)continue;auto o=obstacleAt(base,time+dt);
            if(s.x+s.halfWidth<=o.left||s.x-s.halfWidth>=o.right)continue;
            if(!s.inverted&&s.vy<=0&&oldY-s.halfHeight>=o.top-.1&&s.y-s.halfHeight<=o.top){target=landed?std::max(target,o.top+s.halfHeight):o.top+s.halfHeight;landed=true;}
            if(s.inverted&&s.vy>=0&&oldY+s.halfHeight<=o.bottom+.1&&s.y+s.halfHeight>=o.bottom){target=landed?std::min(target,o.bottom-s.halfHeight):o.bottom-s.halfHeight;landed=true;}
        }
        if(teleported){landed=true;target=s.y;}
        if(landed){s.y=target;s.vy=0;s.grounded=true;}else s.grounded=false;
    }else if(s.y-s.halfHeight<s.floor||s.y+s.halfHeight>s.ceiling)return false;
    for(auto const&o:objects){
        // A spider traverses space instantaneously; only its destination is a
        // candidate collision. The real native spider jump is never simulated.
        double x1=teleported?s.x:oldX,y1=teleported?s.y:oldY;
        if(forecastOverlap(x1,y1,s.x,s.y,s.halfWidth-.05,s.halfHeight-.05,o,time,time+dt))return false;
    }
    return std::isfinite(s.x)&&std::isfinite(s.y)&&std::isfinite(s.vy);
}
FormDecision chooseFormInput(FormState const&initial,std::span<Obstacle const>objects,
    std::span<Interaction const>interactions,FailureMemory const*failures,double bias){
    if(!std::isfinite(initial.x)||!std::isfinite(initial.y)||!std::isfinite(initial.vy)||!std::isfinite(initial.vx)||
        !std::isfinite(initial.floor)||!std::isfinite(initial.ceiling)||!std::isfinite(initial.gravity)||!std::isfinite(initial.scale)||initial.gravity<=0||initial.scale<=0||static_cast<int>(initial.form)<0||static_cast<int>(initial.form)>7)return {};
    struct Node{FormState s;std::uint64_t used=0;double cost=0;bool first=false;int trace=0;};
    struct Trace{Point p;int parent;};
    std::vector<Trace> tree{{{initial.x,initial.y},-1}};tree.reserve(6200);
    std::vector<Node> beam{{initial}},next;beam.reserve(64);next.reserve(128);
    FormDecision result;int bestTrace=0;double time=0;
    double centre=std::isfinite(initial.ceiling)&&initial.ceiling-initial.floor<1500?(initial.floor+initial.ceiling)/2:initial.y;
    for(int depth=0;depth<56;++depth){double dt=depth<8?.25:depth<24?.5:1.;next.clear();
        for(auto const&n:beam)for(bool down:{false,true}){
            auto child=n;if(depth==0)child.first=down;
            if(!advanceForm(child.s,down,dt,objects,interactions,child.used,time))continue;
            double error=std::max(0.,std::abs(child.s.y-centre-bias*25)-8);
            child.cost+=error*error*.0005*dt+(child.s.held!=n.s.held?.35:0);
            if(failures)child.cost+=failures->penalty(child.s,down)*dt;
            tree.push_back({{child.s.x,child.s.y},n.trace});child.trace=static_cast<int>(tree.size()-1);next.push_back(child);
        }
        if(next.empty()){result.forecast.collision=Point{beam.front().s.x+beam.front().s.vx*dt,beam.front().s.y+beam.front().s.vy*dt};break;}
        std::stable_sort(next.begin(),next.end(),[](auto const&a,auto const&b){return a.cost<b.cost;});beam.clear();
        for(auto const&n:next){bool duplicate=std::any_of(beam.begin(),beam.end(),[&](auto const&b){return n.first==b.first&&n.used==b.used&&n.s.form==b.s.form&&n.s.inverted==b.s.inverted&&n.s.held==b.s.held&&n.s.grounded==b.s.grounded&&std::abs(n.s.y-b.s.y)<2&&std::abs(n.s.vy-b.s.vy)<.5&&std::abs(n.s.pressRemaining-b.s.pressRemaining)<.5;});
            if(!duplicate)beam.push_back(n);if(beam.size()==64)break;}
        time+=dt;result.down=beam.front().first;result.survival=time;bestTrace=beam.front().trace;
    }
    for(int i=bestTrace;i>=0;i=tree[i].parent)result.forecast.path.push_back(tree[i].p);
    std::reverse(result.forecast.path.begin(),result.forecast.path.end());result.forecast.down=result.down;result.forecast.survival=result.survival;return result;
}
}
