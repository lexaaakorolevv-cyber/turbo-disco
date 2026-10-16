#include "planner.hpp"
#include <algorithm>
#include <cmath>
namespace rf {
DualDecision chooseDualInput(FormState const&a,FormState const&b,std::span<Obstacle const> objects,
 std::span<Interaction const> ia,std::span<Interaction const> ib,FailureMemory const* memory,bool preview){
 struct Node{FormState a,b;std::uint64_t ua=0,ub=0;double cost=0;bool first=false;int trace=0;unsigned short inputs=0;};
 struct Trace{Point a,b;int parent;};
 auto valid=[](FormState const&s){return std::isfinite(s.x)&&std::isfinite(s.y)&&std::isfinite(s.vy)&&std::isfinite(s.vx)&&std::isfinite(s.gravity)&&s.gravity>0&&std::isfinite(s.scale)&&s.scale>0&&s.ceiling>s.floor;};
 if(!valid(a)||!valid(b))return {};
 std::vector<Node> beam{{a,b}},next;beam.reserve(64);next.reserve(128);
 std::vector<Trace> tree;if(preview){tree.reserve(7169);tree.push_back({{a.x,a.y},{b.x,b.y},-1});}
 std::vector<Obstacle> nearby,danger;nearby.reserve(objects.size());danger.reserve(objects.size());DualDecision result;int trace=0;double time=0;
 auto centre=[](FormState const&s){return s.ceiling-s.floor<1500?(s.floor+s.ceiling)/2:s.y;};
 double ca=centre(a),cb=centre(b);
 for(int depth=0;depth<56;depth++){
  double dt=depth<8?.25:depth<24?.5:1.;next.clear();double left=beam.front().a.x,right=left,radius=0;
  for(auto const&n:beam)for(auto const*s:{&n.a,&n.b}){left=std::min(left,s->x-std::abs(s->vx)*dt);right=std::max(right,s->x+std::abs(s->vx)*dt);radius=std::max(radius,s->halfWidth*1.7);}
  horizontalCandidates(objects,left,right,radius,time,time+dt,nearby);
  horizontalCandidates(objects,left-96,right+96,radius,time,time+dt,danger);
  for(auto const&n:beam)for(bool down:{false,true}){
   auto child=n;if(depth==0)child.first=down;if(depth<12&&down)child.inputs|=static_cast<unsigned short>(1u<<depth);
   if(!advanceForm(child.a,down,dt,nearby,ia,child.ua,time)||!advanceForm(child.b,down,dt,nearby,ib,child.ub,time))continue;
   child.cost+=formCost(child.a,n.a,down,dt,ca)+formCost(child.b,n.b,down,dt,cb)+approachCost(child.a,danger,time+dt,dt)+approachCost(child.b,danger,time+dt,dt);
   if(memory)child.cost+=(memory->penalty(child.a,down)+memory->penalty(child.b,down))*dt;
   if(preview){tree.push_back({{child.a.x,child.a.y},{child.b.x,child.b.y},n.trace});child.trace=static_cast<int>(tree.size()-1);}
   next.push_back(child);
  }
  if(next.empty()){
   if(preview){result.first.collision=Point{beam.front().a.x,beam.front().a.y};result.second.collision=Point{beam.front().b.x,beam.front().b.y};}break;
  }
  std::stable_sort(next.begin(),next.end(),[](auto const&x,auto const&y){return x.cost<y.cost;});beam.clear();
  int counts[2]{};bool both=std::any_of(next.begin(),next.end(),[](auto const&n){return n.first;})&&std::any_of(next.begin(),next.end(),[](auto const&n){return !n.first;});
  for(auto const&n:next){if(both&&counts[n.first]>=32)continue;bool duplicate=std::any_of(beam.begin(),beam.end(),[&](auto const&x){return n.first==x.first&&n.ua==x.ua&&n.ub==x.ub&&equivalentFormStates(n.a,x.a)&&equivalentFormStates(n.b,x.b);});if(!duplicate){beam.push_back(n);++counts[n.first];}if(beam.size()==64)break;}
  time+=dt;result.down=beam.front().first;result.survival=time;trace=beam.front().trace;result.plan={beam.front().inputs,static_cast<unsigned>(std::min(depth+1,12)),std::min(time,3.)};
  if(beam.front().a.boundary||beam.front().b.boundary)break;
 }
 if(preview){for(int i=trace;i>=0;i=tree[i].parent){result.first.path.push_back(tree[i].a);result.second.path.push_back(tree[i].b);}
  std::reverse(result.first.path.begin(),result.first.path.end());std::reverse(result.second.path.begin(),result.second.path.end());
  result.first.down=result.second.down=result.down;result.first.survival=result.second.survival=time;}
 return result;
}
}
