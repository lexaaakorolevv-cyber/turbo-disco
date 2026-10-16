#include "planner.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>
using namespace rf;
int main(){
 std::vector<Obstacle> objects;for(int i=0;i<512;i++){double x=-80+(i%64)*9,y=i<256?-180-(i/64)*30:730+(i/64)*30;objects.push_back({x,x+6,y,y+12});}
 for(bool dual:{false,true}){
  std::vector<double> times;unsigned maximumNodes=0;
  for(int frame=0;frame<48;frame++){
#ifdef RF_LEGACY_FRAME_CAP
   PlanningBudget budget;budget.reset(6000,3.);
#endif
   auto begin=std::chrono::steady_clock::now();
   for(int update=0;update<4;update++){
#ifdef RF_LEGACY_FRAME_CAP
    if(!budget.available())break;
#endif
    FormState s;s.form=Form::Ufo;s.grounded=false;s.y=150+frame%12;s.floor=0;s.ceiling=300;s.x=frame%16;s.vy=(frame%9-4)*.6;auto second=s;second.y+=350;second.floor=350;second.ceiling=650;
#ifndef RF_LEGACY_FRAME_CAP
    if(dual)chooseDualInput(s,second,objects,{}, {},nullptr,false,true);else chooseFormInput(s,objects,{},nullptr,0,false);
#else
    if(dual)chooseDualInput(s,second,objects,{}, {},nullptr,false,true,&budget);else chooseFormInput(s,objects,{},nullptr,0,false,&budget);
    maximumNodes=std::max(maximumNodes,budget.used());
#endif
   }
   times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count());
  }
  std::sort(times.begin(),times.end());double total=0;for(auto value:times)total+=value;
  std::cout<<(dual?"independent-dual":"ufo")<<" mean_ms="<<total/times.size()<<" p95_ms="<<times[times.size()*95/100]<<" max_ms="<<times.back()<<" max_nodes="<<maximumNodes<<'\n';
 }
}
