#pragma once
#include "geometry.hpp"
#include <array>
#include <span>
#include <unordered_map>
#include <string>
#include <nlohmann/json.hpp>
namespace rf {
enum class Form { Cube, Ship, Wave, Ball, Ufo, Robot, Spider, Swing };
char const* formName(Form form);
struct FormState {
    Form form=Form::Cube;
    double x=0,y=105,vy=0,vx=5.2,halfWidth=14,halfHeight=14;
    double floor=91,ceiling=1e6,scale=1,gravity=1,jump=11.18,gravityMod=1;
    double pressRemaining=0,pressInterval=3.75,robotTime=0;
    bool grounded=true,inverted=false,held=false;
    std::array<double,4> acceleration{1,1,1,1};
    double waveSlope=1;
};
struct PhysicsSample {
    FormState state;
    double time=0;
    std::uint64_t edge=0;
    bool disturbed=false;
};
class Calibration {
    struct Estimate { double ratio=1; unsigned samples=0; };
    struct Bucket { std::array<Estimate,7> values; };
    std::array<Bucket,32> m_buckets{};
    std::optional<PhysicsSample> m_previous;
    static std::size_t key(FormState const&);
public:
    void resetSampling(){m_previous.reset();}
    void observe(PhysicsSample const& sample);
    FormState adjusted(FormState state) const;
    unsigned samples() const;
    nlohmann::json toJson() const;
    static Calibration fromJson(nlohmann::json const&);
};
class MotionTracker {
    struct Entry {double x,y,time,vx=0,vy=0;unsigned samples=0;};
    std::unordered_map<std::uint64_t,Entry> m_entries;
public:
    void reset(){m_entries.clear();}
    void update(Obstacle& obstacle,double time);
};
enum class FailureKind { Unknown, LatePress, EarlyPress, NeedHold, NeedRelease, WrongRoute };
char const* failureName(FailureKind);
struct FailureRecord {
    Form form=Form::Cube;
    FailureKind kind=FailureKind::Unknown;
    double x=0,y=0,vy=0;
    bool held=false,inverted=false,mini=false;
    unsigned count=1;
    std::uint64_t object=0;
};
class FailureMemory {
    std::vector<FailureRecord> m_records;
    mutable std::vector<std::size_t> m_byX;
    mutable bool m_indexDirty=true;
    void index() const;
public:
    void record(FailureRecord const& failure);
    double penalty(FormState const& state,bool down) const;
    double timingBias(FormState const& state) const;
    double heightBias(FormState const& state) const;
    std::size_t size() const{return m_records.size();}
    nlohmann::json toJson() const;
    static FailureMemory fromJson(nlohmann::json const&);
};
}
