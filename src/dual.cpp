#include "planner.hpp"
#include <algorithm>
#include <cmath>
namespace rf {
DualDecision chooseDualInput(FormState const&a,FormState const&b,std::span<Obstacle const> objects,
 std::span<Interaction const> ia,std::span<Interaction const> ib,FailureMemory const* memory,bool preview,bool independent,PlanningBudget* budget){
 struct Node{FormState a,b;std::uint64_t ua=0,ub=0;double cost=0;bool first=false;int trace=0;unsigned short inputs=0,inputs2=0;unsigned first2=0;};
 struct Trace{Point a,b;int parent;};
 auto valid=[](FormState const&s){return std::isfinite(s.x)&&std::isfinite(s.y)&&std::isfinite(s.vy)&&std::isfinite(s.vx)&&std::isfinite(s.gravity)&&s.gravity>0&&std::isfinite(s.scale)&&s.scale>0&&s.ceiling>s.floor;};
 if(!valid(a)||!valid(b))return {};
 std::vector<Node> beam{{a,b}},next;beam.reserve(independent?96:64);next.reserve(independent?384:128);
 std::vector<Trace> tree;if(preview){tree.reserve(7169);tree.push_back({{a.x,a.y},{b.x,b.y},-1});}
 std::vector<Obstacle> nearby,danger;nearby.reserve(objects.size());danger.reserve(objects.size());DualDecision result;int trace=0;double time=0;
 auto centre=[](FormState const&s){return s.ceiling-s.floor<1500?(s.floor+s.ceiling)/2:s.y;};
 double ca=centre(a),cb=centre(b);bool chain=hasInteractionChain(a,ia)||hasInteractionChain(b,ib);
 std::vector<Interaction> activeA,activeB;activeA.reserve(ia.size());activeB.reserve(ib.size());
 for(int depth=0;depth<(chain?80:56);depth++){
  double dt=depth<8?.25:depth<24?.5:1.;next.clear();double left=beam.front().a.x,right=left,radius=0;
  for(auto const&n:beam)for(auto const*s:{&n.a,&n.b}){left=std::min(left,s->x-std::abs(s->vx)*dt);right=std::max(right,s->x+std::abs(s->vx)*dt);radius=std::max(radius,s->halfWidth*1.7);}
  horizontalCandidates(objects,left,right,radius,time,time+dt,nearby);
  horizontalCandidates(objects,left-96,right+96,radius,time,time+dt,danger);
  interactionCandidates(ia,left,right,radius,time,time+dt,activeA);interactionCandidates(ib,left,right,radius,time,time+dt,activeB);
  for(auto const&n:beam)for(unsigned action=0;action<(independent?4u:2u);++action){
   if(budget&&!budget->consume())goto planningFinished;
   bool down=action&1,down2=independent?bool(action&2):down;
   auto child=n;if(depth==0){child.first=down;child.first2=down2;}if(depth<12&&down)child.inputs|=static_cast<unsigned short>(1u<<depth);if(depth<12&&down2)child.inputs2|=static_cast<unsigned short>(1u<<depth);
   if(!advanceForm(child.a,down,dt,nearby,activeA,child.ua,time)||!advanceForm(child.b,down2,dt,nearby,activeB,child.ub,time))continue;
   child.cost+=formCost(child.a,n.a,down,dt,ca)+formCost(child.b,n.b,down2,dt,cb)+approachCost(child.a,danger,time+dt,dt)+approachCost(child.b,danger,time+dt,dt);
   if(memory)child.cost+=(memory->penalty(child.a,down)+memory->penalty(child.b,down2))*dt;
   if(preview){tree.push_back({{child.a.x,child.a.y},{child.b.x,child.b.y},n.trace});child.trace=static_cast<int>(tree.size()-1);}
   next.push_back(child);
  }
  if(next.empty()){
   if(preview){result.first.collision=Point{beam.front().a.x,beam.front().a.y};result.second.collision=Point{beam.front().b.x,beam.front().b.y};}break;
  }
  std::stable_sort(next.begin(),next.end(),[](auto const&x,auto const&y){return x.cost<y.cost;});beam.clear();
  unsigned capacity=independent?96:64,groups=independent?4:2;unsigned counts[4]{};
  // Reserve space for each immediate control pair, then fill spare capacity.
  // This preserves a costly early maneuver without wasting empty quotas.
  auto group=[](auto const&n){return unsigned(n.first)+2*n.first2;};
  auto add=[&](auto const&n){bool duplicate=std::any_of(beam.begin(),beam.end(),[&](auto const&x){return group(n)==group(x)&&n.ua==x.ua&&n.ub==x.ub&&equivalentFormStates(n.a,x.a)&&equivalentFormStates(n.b,x.b);});if(!duplicate){beam.push_back(n);++counts[group(n)];}};
  if(chain)for(auto const&n:next){bool same=std::any_of(beam.begin(),beam.end(),[&](auto const&x){return group(x)==group(n)&&x.ua==n.ua&&x.ub==n.ub;});if(!same)add(n);if(beam.size()==16)break;}
  unsigned quota=capacity/groups;
  for(auto const&n:next){if(counts[group(n)]<quota)add(n);if(beam.size()==capacity)break;}
  if(beam.size()<capacity)for(auto const&n:next){add(n);if(beam.size()==capacity)break;}
  std::stable_sort(beam.begin(),beam.end(),[](auto const&x,auto const&y){return x.cost<y.cost;});
  time+=dt;result.down=beam.front().first;result.secondDown=beam.front().first2;result.survival=time;trace=beam.front().trace;result.plan={beam.front().inputs,static_cast<unsigned>(std::min(depth+1,12)),std::min(time,3.)};result.secondPlan={beam.front().inputs2,static_cast<unsigned>(std::min(depth+1,12)),std::min(time,3.)};
  if(beam.front().a.boundary||beam.front().b.boundary)break;
 }
planningFinished:
 if(preview){for(int i=trace;i>=0;i=tree[i].parent){result.first.path.push_back(tree[i].a);result.second.path.push_back(tree[i].b);}
  std::reverse(result.first.path.begin(),result.first.path.end());std::reverse(result.second.path.begin(),result.second.path.end());
  result.first.down=result.down;result.second.down=result.secondDown;result.first.survival=result.second.survival=time;}
 return result;
}
}
