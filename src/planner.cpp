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
        case InteractionKind::NativeBoundary:s.boundary=true;break;
        case InteractionKind::ModePortal:s.form=o.target;s.grounded=false;s.robotTime=0;s.boundary=true;break;
        case InteractionKind::GravityPortal:s.inverted=o.value==1?!s.inverted:o.inverted;s.grounded=false;break;
        case InteractionKind::SizePortal:{double target=o.value<.9?.6:1;double ratio=target/s.scale;s.halfWidth*=ratio;s.halfHeight*=ratio;s.scale=target;break;}
        case InteractionKind::SpeedPortal:s.vx=std::copysign(o.value,s.vx);s.boundary=true;break;
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
    if(!down||rising)s.ringArmed=true;
    if(rising&&s.pressRemaining>1e-8)return false;
    s.pressRemaining=std::max(0.,(rising?s.pressInterval:s.pressRemaining)-dt);
    double oldX=s.x,oldY=s.y,sgn=sign(s),g=s.gravity*s.gravityMod,mini=s.scale<.9?.8:1;
    bool teleported=false,wasGrounded=s.grounded;
    if(s.form==Form::Ball&&s.grounded&&rising){s.inverted=!s.inverted;s.vy=sgn*s.jump*mini*.6;s.grounded=false;}
    else if(s.form==Form::Spider&&s.grounded&&rising){if(!snapSpider(s,objects,time))return false;teleported=true;}
    else if((s.form==Form::Cube||s.form==Form::Robot)&&s.grounded&&down){s.vy=sgn*s.jump*mini*(s.form==Form::Robot?.5:1);s.grounded=false;s.robotTime=0;}
    else if(s.form==Form::Ufo&&rising){double v=(s.scale<.9?8:7)*(s.scale<.9?.85:1)*s.controlScale;if(s.vy*sgn<v)s.vy=sgn*v;s.grounded=false;}
    else if(s.form==Form::Swing&&rising){s.inverted=!s.inverted;s.vy*=std::clamp(.8*s.controlScale,0.,1.);}
    sgn=sign(s);double startVy=s.vy;
    if(s.form==Form::Wave)s.vy=std::abs(s.vx)*sgn*(down?1:-1)*(s.scale<.9?2:1)*s.waveSlope;
    else if(s.form==Form::Ship){bool falling=s.vy*sgn<0;int branch=(down?0:2)+(falling?1:0);double a=down?(falling?.4791:.3833):-(falling?.3066:.4599);
        s.vy+=sgn*a*s.gravityMod/(s.scale<.9?.85:1)*s.acceleration[branch]*dt;
        s.vy=std::clamp(s.vy,sgn>0?-6.4/(s.scale<.9?.85:1):-8/(s.scale<.9?.85:1),sgn>0?8/(s.scale<.9?.85:1):6.4/(s.scale<.9?.85:1));
    }else if(s.form==Form::Ufo){s.vy-=sgn*g*(s.vy*sgn<0?.4:.6)/(s.scale<.9?.85:1)*dt;s.vy=sgn*std::clamp(s.vy*sgn,-6.4/(s.scale<.9?.85:1),8/(s.scale<.9?.85:1));}
    else if(s.form==Form::Swing){s.vy-=sgn*g*(s.scale<.9?.6:.4)*dt;s.vy=std::clamp(s.vy,-8.,8.);}
    else if(!s.grounded){bool boosting=s.form==Form::Robot&&down&&s.robotTime<s.robotHold&&s.vy*sgn>0;
        if(!boosting)s.vy-=sgn*g*(s.form==Form::Robot?.9:(s.form==Form::Ball||s.form==Form::Spider)?.6:1)*dt;
        if(s.form==Form::Robot){s.robotTime+=dt;if(!down)s.robotTime=16;}
        s.vy=sgn*std::max(s.vy*sgn,-15.);
    }
    s.x+=s.vx*dt;
    // Integrate distant steps as an aggregate of the near .25-unit steps.
    // A single long semi-implicit step otherwise invents early landings.
    s.y+=s.form==Form::Wave?s.vy*dt:(startVy+s.vy)*dt/2+(s.vy-startVy)*std::min(dt,.25)/2;
    s.held=down;
    for(std::size_t i=0;i<interactions.size()&&i<64;++i){auto const&o=interactions[i];auto mask=std::uint64_t(1)<<(o.slot<64?o.slot:i);
        if((used&mask)||!forecastOverlap(oldX,oldY,s.x,s.y,s.halfWidth,s.halfHeight,o.area,time,time+dt))continue;
        if(isOrb(o.kind)&&(!down||!s.ringArmed))continue;
        activate(s,o,objects,time+dt);used|=mask;
        if(isOrb(o.kind))s.ringArmed=false;
        if(o.kind==InteractionKind::SpiderOrb||o.kind==InteractionKind::SpiderPad)teleported=true;
    }
    if(groundForm(s.form)){
        bool landed=false;double target=s.inverted?-std::numeric_limits<double>::infinity():std::numeric_limits<double>::infinity();
        if(!s.inverted&&s.vy<=0&&oldY-s.halfHeight>=s.floor-.1&&s.y-s.halfHeight<=s.floor){target=s.floor+s.halfHeight;landed=true;}
        if(s.inverted&&s.vy>=0&&oldY+s.halfHeight<=s.ceiling+.1&&s.y+s.halfHeight>=s.ceiling){target=s.ceiling-s.halfHeight;landed=true;}
        for(auto const&base:objects){if(!base.solid)continue;auto o=obstacleAt(base,time+dt);
            if(s.x+s.halfWidth<=o.left||s.x-s.halfWidth>=o.right)continue;
            double top=o.top,bottom=o.bottom;
            if(o.ramp){auto span=verticalSpan(o,s.x,s.halfWidth);if(span){bottom=span->first;top=span->second;}}
            bool followsRamp=false;
            if(o.ramp&&wasGrounded){auto old=obstacleAt(base,time);auto span=verticalSpan(old,oldX,s.halfWidth);
                followsRamp=span&&std::abs((s.inverted?oldY+s.halfHeight:oldY-s.halfHeight)-(s.inverted?span->first:span->second))<2;}
            if(!s.inverted&&s.vy<=0&&(oldY-s.halfHeight>=top-.1||followsRamp)&&(s.y-s.halfHeight<=top||followsRamp)){target=landed?std::max(target,top+s.halfHeight):top+s.halfHeight;landed=true;}
            if(s.inverted&&s.vy>=0&&(oldY+s.halfHeight<=bottom+.1||followsRamp)&&(s.y+s.halfHeight>=bottom||followsRamp)){target=landed?std::min(target,bottom-s.halfHeight):bottom-s.halfHeight;landed=true;}
        }
        if(teleported){landed=true;target=s.y;}
        if(landed){s.y=target;s.vy=0;s.grounded=true;}else s.grounded=false;
    }else if(!s.boundary&&(s.y-s.halfHeight<s.floor||s.y+s.halfHeight>s.ceiling))return false;
    for(auto const&o:objects){
        // A spider traverses space instantaneously; only its destination is a
        // candidate collision. The real native spider jump is never simulated.
        double x1=teleported?s.x:oldX,y1=teleported?s.y:oldY;
        double margin=o.solid?-.05:3.0;
        if(forecastOverlap(x1,y1,s.x,s.y,s.halfWidth+margin,s.halfHeight+margin,o,time,time+dt))return false;
    }
    return std::isfinite(s.x)&&std::isfinite(s.y)&&std::isfinite(s.vy);
}
bool hasInteractionChain(FormState const&s,std::span<Interaction const> interactions){
    if(std::abs(s.vx)<.1)return false;unsigned count=0;
    for(auto const&i:interactions){if(i.kind>InteractionKind::BluePad&&i.kind!=InteractionKind::SpiderOrb&&i.kind!=InteractionKind::SpiderPad)continue;
        double time=((i.area.left+i.area.right)/2-s.x)/s.vx;if(time>=-4&&time<=64&&++count>=2)return true;}
    return false;
}
void interactionCandidates(std::span<Interaction const>source,double left,double right,double radius,double start,double end,std::vector<Interaction>&result){
    result.clear();for(std::size_t i=0;i<source.size()&&i<64;i++)if(overlapsHorizontal(source[i].area,left,right,radius,start,end)){auto copy=source[i];if(copy.slot>=64)copy.slot=i;result.push_back(copy);}
}
double approachCost(FormState const&s,std::span<Obstacle const> objects,double time,double dt){
    if(s.form!=Form::Cube&&s.form!=Form::Robot&&s.form!=Form::Ball&&s.form!=Form::Spider)return 0;
    double nearest=96;
    for(auto const&base:objects){if(base.ramp)continue;auto o=obstacleAt(base,time);
        double gap=s.vx>=0?o.left-s.x-s.halfWidth:s.x-s.halfWidth-o.right;
        if(gap<0||gap>=nearest||s.y+s.halfHeight<=o.bottom||s.y-s.halfHeight>=o.top)continue;
        nearest=gap;
    }
    double proximity=1-nearest/96;return .5*proximity*proximity*dt;
}
double formCost(FormState const& s,FormState const& before,bool down,double dt,double centre,double bias){
    bool groundedMode=s.form==Form::Cube||s.form==Form::Robot||s.form==Form::Ball||s.form==Form::Spider;
    // Ground modes have no reason to seek the middle of a flight corridor.
    // Safe walking wins over an unnecessary jump; survival still comes first.
    double cost=(down!=before.held?.25:0)+(down&&!before.held?(groundedMode?1.2:.25):0);
    if(!groundedMode){double error=std::max(0.,std::abs(s.y-centre-bias*25)-10);cost+=error*error*.0005*dt;}
    else {
        if(down)cost+=.03*dt;
        if(before.grounded&&!s.grounded&&(s.form==Form::Cube||s.form==Form::Robot)&&s.vy*(s.inverted?-1:1)>0)cost+=.6;
    }
    return cost;
}
bool equivalentFormStates(FormState const&a,FormState const&b){
    return a.form==b.form&&a.inverted==b.inverted&&a.held==b.held&&a.grounded==b.grounded&&
        std::abs(a.scale-b.scale)<1e-6&&std::abs(a.vx-b.vx)<1e-6&&std::abs(a.gravityMod-b.gravityMod)<1e-6&&
        std::abs(a.halfWidth-b.halfWidth)<.1&&std::abs(a.halfHeight-b.halfHeight)<.1&&
        a.ringArmed==b.ringArmed&&a.boundary==b.boundary&&std::abs(a.x-b.x)<1.5&&
        std::abs(a.y-b.y)<2&&std::abs(a.vy-b.vy)<.5&&std::abs(a.pressRemaining-b.pressRemaining)<.5&&
        (a.form!=Form::Robot||std::abs(a.robotTime-b.robotTime)<.5);
}
static FormDecision planFormInput(FormState const&initial,std::span<Obstacle const>objects,
    std::span<Interaction const>interactions,FailureMemory const*failures,double bias,bool includeForecast,unsigned capacity,unsigned depthLimit=56,PlanningBudget* budget=nullptr){
    if(!std::isfinite(initial.x)||!std::isfinite(initial.y)||!std::isfinite(initial.vy)||!std::isfinite(initial.vx)||
        !std::isfinite(initial.floor)||!std::isfinite(initial.ceiling)||!std::isfinite(initial.gravity)||!std::isfinite(initial.scale)||initial.gravity<=0||initial.scale<=0||static_cast<int>(initial.form)<0||static_cast<int>(initial.form)>7)return {};
    struct Node{FormState s;std::uint64_t used=0;double cost=0;bool first=false;int trace=0;unsigned short inputs=0;};
    struct Trace{Point p;int parent;};
    std::vector<Trace> tree; if(includeForecast){tree.reserve(7169);tree.push_back({{initial.x,initial.y},-1});}
    std::vector<Obstacle> nearby,danger;nearby.reserve(objects.size());danger.reserve(objects.size());
    std::vector<Node> beam{{initial}},next;beam.reserve(capacity);next.reserve(2*capacity);
    FormDecision result;int bestTrace=0;double time=0;
    bool chain=hasInteractionChain(initial,interactions);std::vector<Interaction> activeInteractions;activeInteractions.reserve(interactions.size());
    double centre=std::isfinite(initial.ceiling)&&initial.ceiling-initial.floor<1500?(initial.floor+initial.ceiling)/2:initial.y;
    if(initial.routeTarget&&std::isfinite(*initial.routeTarget))centre=*initial.routeTarget;
    for(int depth=0;depth<depthLimit;++depth){double dt=depth<8?.25:depth<24?.5:1.;next.clear();
        double left=beam.front().s.x,right=left,radius=0;
        for(auto const& n:beam){left=std::min(left,n.s.x-std::abs(n.s.vx)*dt);right=std::max(right,n.s.x+std::abs(n.s.vx)*dt);radius=std::max(radius,n.s.halfWidth*1.7);}
        horizontalCandidates(objects,left,right,radius,time,time+dt,nearby);
        horizontalCandidates(objects,left-96,right+96,radius,time,time+dt,danger);
        interactionCandidates(interactions,left,right,radius,time,time+dt,activeInteractions);
        for(auto const&n:beam)for(bool down:{false,true}){
            if(budget&&!budget->consume())goto planningFinished;
            auto child=n;if(depth==0)child.first=down;if(depth<12&&down)child.inputs|=static_cast<unsigned short>(1u<<depth);
            if(!advanceForm(child.s,down,dt,nearby,activeInteractions,child.used,time))continue;
            child.cost+=formCost(child.s,n.s,down,dt,centre,bias)+approachCost(child.s,danger,time+dt,dt);
            if(failures)child.cost+=failures->penalty(child.s,down)*dt;
            if(includeForecast){tree.push_back({{child.s.x,child.s.y},n.trace});child.trace=static_cast<int>(tree.size()-1);}
            next.push_back(child);
        }
        if(next.empty()){if(includeForecast)result.forecast.collision=Point{beam.front().s.x+beam.front().s.vx*dt,beam.front().s.y+beam.front().s.vy*dt};break;}
        std::stable_sort(next.begin(),next.end(),[](auto const&a,auto const&b){return a.cost<b.cost;});beam.clear();
        int counts[2]{};bool both=std::any_of(next.begin(),next.end(),[](auto const&n){return n.first;})&&std::any_of(next.begin(),next.end(),[](auto const&n){return !n.first;});
        if(chain)for(auto const&n:next){
            bool same=std::any_of(beam.begin(),beam.end(),[&](auto const&b){return b.first==n.first&&b.used==n.used;});
            if(!same){beam.push_back(n);++counts[n.first];}if(beam.size()==16)break;
        }
        for(auto const&n:next){if(both&&counts[n.first]>=static_cast<int>(capacity/2))continue;bool duplicate=std::any_of(beam.begin(),beam.end(),[&](auto const&b){return n.first==b.first&&n.used==b.used&&equivalentFormStates(n.s,b.s);});
            if(!duplicate){beam.push_back(n);++counts[n.first];}if(beam.size()==capacity)break;}
        // Ball/spider flips keep balanced quotas; their opposite-surface
        // routes must not be displaced by spare states from one control.
        if(initial.form!=Form::Ball&&initial.form!=Form::Spider&&beam.size()<capacity)for(auto const&n:next){
            bool duplicate=std::any_of(beam.begin(),beam.end(),[&](auto const&b){return n.first==b.first&&n.used==b.used&&equivalentFormStates(n.s,b.s);});
            if(!duplicate)beam.push_back(n);if(beam.size()==capacity)break;
        }
        std::stable_sort(beam.begin(),beam.end(),[](auto const&a,auto const&b){return a.cost<b.cost;});
        time+=dt;result.down=beam.front().first;result.survival=time;bestTrace=beam.front().trace;result.plan={beam.front().inputs,static_cast<unsigned>(std::min(depth+1,12)),std::min(time,3.)};
        if(beam.front().s.boundary){result.handoff=true;break;}
        if(depth+1==depthLimit&&depthLimit<80&&!beam.front().s.grounded&&
            (beam.front().s.form==Form::Cube||beam.front().s.form==Form::Robot))depthLimit=80;
    }
planningFinished:
    if(includeForecast)for(int i=bestTrace;i>=0;i=tree[i].parent)result.forecast.path.push_back(tree[i].p);
    std::reverse(result.forecast.path.begin(),result.forecast.path.end());result.forecast.down=result.down;result.forecast.survival=result.survival;return result;
}
FormDecision chooseFormInput(FormState const&state,std::span<Obstacle const>objects,
    std::span<Interaction const>interactions,FailureMemory const*failures,double bias,bool preview,PlanningBudget* budget){
    unsigned depth=state.planningHorizon>66?112:state.planningHorizon>42?80:56;
    if(hasInteractionChain(state,interactions))depth=std::max(depth,80u);
    auto decision=planFormInput(state,objects,interactions,failures,bias,preview,64,depth,budget);
    // Spend extra work only at a near dead end. Easy stretches retain the
    // small beam; a fuller search may preserve a previously pruned escape.
    if(!decision.handoff&&decision.survival>0&&decision.survival<24){
        auto rescue=planFormInput(state,objects,interactions,failures,bias,preview,128,80,budget);
        if(rescue.survival>decision.survival+1e-8)decision=std::move(rescue);
    }
    return decision;
}

}
