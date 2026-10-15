#include "core.hpp"
#include <iostream>
#include <stdexcept>
#include <cmath>
using namespace rf;
namespace {
void check(bool ok,char const* msg){if(!ok)throw std::runtime_error(msg);}
template<class F> void rejects(F f){bool rejected=false;try{f();}catch(std::exception const&){rejected=true;}check(rejected,"Corrupt session accepted");}
Macro fixture(){
 Macro m;m.environment={{"gd","2.2081"},{"mod",ModVersion},{"level_id",1}};
 m.ticks=m.outerUpdates=33;m.bestPercent=20;m.searchMaxCps=16;
 m.actions={{0,true},{10,false},{20,true},{32,false}};m.timings.resize(33,{.25,false,true});
 for(Tick t=0;t<33;t+=16)m.trace.push_back({t,1,2,0,1,1,.25});return m;
}
}
int main(){try{
 Search original(1337,120);original.next(500);
 std::vector<Action> parent{{20,true},{40,false},{100,true},{124,false},{204,true},{228,false}};
 original.observe(parent,260,60);original.next(500);original.next(500);
 auto stored=original.snapshot();
 auto restored=Search::restore(nlohmann::json::parse(stored.dump()));
 bool explored=false,restarted=false;
 for(int i=0;i<320;++i){
  auto a=original.next(500),b=restored.next(500);
  check(a==b && original.guidanceFrom()==restored.guidanceFrom() && original.policy()==restored.policy() && original.phase()==restored.phase(),"Resume changed future candidates");
  if(original.phase()==SearchPhase::Restart){check(a.empty()&&original.guidanceFrom()==0,"Restart reused a stale prefix");restarted=true;}
  explored |= original.phase()==SearchPhase::Explore;
  original.observe(a,260,60);restored.observe(b,260,60);
  if(i%37==0)restored=Search::restore(nlohmann::json::parse(restored.snapshot().dump()));
 }
 check(explored&&restarted,"Stagnation did not explore another route");
 check(original.best().percent>=60,"Restart discarded the best progress");
 auto empty=Search::restore(Search(42,120).snapshot());check(empty.next(500).empty(),"Empty session broke first attempt");
 auto bad=stored;bad["schema"]=9;rejects([&]{Search::restore(bad);});
 bad=stored;bad["rng"]="broken";rejects([&]{Search::restore(bad);});
 bad=stored;bad["rng"]=bad["rng"].get<std::string>()+" rubbish";rejects([&]{Search::restore(bad);});
 bad=stored;bad["edits"][0][0]=999999;rejects([&]{Search::restore(bad);});
 bad=stored;bad["edits"][0][2]=std::uint64_t(-1);rejects([&]{Search::restore(bad);});
 bad=stored;bad["phase"]=99;rejects([&]{Search::restore(bad);});
 bad=stored;bad["elites"][0]["percent"]=-1;rejects([&]{Search::restore(bad);});
 bad=stored;bad["elites"][0]["actions"][0][0]=-1;rejects([&]{Search::restore(bad);});
 rejects([&]{expandPlan(nlohmann::json::array({{4,true},{4,false}}));});
 rejects([&]{expandPlan(nlohmann::json::array({{4,false}}));});
 auto best=fixture();auto payload=searchSessionJson(best,original,parent,1337,123,300.5);
 auto path=std::filesystem::temp_directory_path()/"routefinder-session-test"/"search.json";
 saveJsonFile(path,payload,-1);auto session=parseSearchSession(loadJsonFile(path));
 check(session.plan==parent&&session.attempts==123&&session.seed==1337&&session.seconds==300.5,"Session metadata/pending plan lost");
 check(toJson(session.best)==toJson(best),"Best macro changed during session save");
 std::filesystem::rename(path,path.string()+".bak");check(parseSearchSession(loadJsonFile(path)).attempts==123,"Interrupted replacement did not recover backup");
 std::filesystem::remove_all(path.parent_path());
 payload["best"]["completed"]=true;payload["best"]["verified_from_start"]=true;
 check(!parseSearchSession(payload).best.verified,"Disk flag bypassed native verification");
 payload["max_cps"]=0;rejects([&]{parseSearchSession(payload);});
 std::cout<<"Session RNG/archive/queue/pending-plan recovery, corrupt input, backup, native-verification boundary and stagnation escape: PASS\n";
}catch(std::exception const&e){std::cerr<<e.what()<<'\n';return 1;}}
