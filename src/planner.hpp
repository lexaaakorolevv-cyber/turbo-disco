#pragma once
#include "intelligence.hpp"
namespace rf {
enum class InteractionKind { YellowOrb,PinkOrb,RedOrb,BlueOrb,GreenOrb,BlackOrb,
    YellowPad,PinkPad,RedPad,BluePad,GravityPortal,ModePortal,SizePortal,SpeedPortal,SpiderOrb,SpiderPad };
struct Interaction {
    Obstacle area;
    InteractionKind kind=InteractionKind::YellowOrb;
    double value=0;
    Form target=Form::Cube;
    bool inverted=false,reverse=false;
};
struct FormDecision {bool down=false;double survival=0;Forecast forecast;};
// Approximate forward model only; it never mutates a Geometry Dash object.
// The real game and full replay remain the final judges.
bool advanceForm(FormState& state,bool down,double dt,std::span<Obstacle const> obstacles,
    std::span<Interaction const> interactions,std::uint64_t& used,double time=0);
FormDecision chooseFormInput(FormState const& state,std::span<Obstacle const> obstacles,
    std::span<Interaction const> interactions,FailureMemory const* failures=nullptr,double bias=0,bool includeForecast=true);
}
