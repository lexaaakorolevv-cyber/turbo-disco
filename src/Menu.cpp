#include "Runtime.hpp"
#include "ui_geometry.hpp"
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/cocos/draw_nodes/CCDrawNode.h>
#include <algorithm>
#include <cmath>
using namespace geode::prelude;
namespace rf { namespace {
constexpr ccColor4F Panel{1,1,1,1},Card{.96f,.97f,.985f,1},Border{.83f,.86f,.90f,1};
constexpr ccColor4F Mint{.16f,.39f,.93f,1},Blue{.16f,.39f,.93f,1},Coral{.86f,.23f,.32f,1};
constexpr ccColor3B Text{31,43,61},Muted{105,118,137};
char const* systemFont(){
#ifdef GEODE_IS_ANDROID
return "sans-serif";
#else
return "Arial";
#endif
}
CCLabelTTF* label(std::string const& text,float size,CCSize bounds={}){
 auto l=bounds.width>0?CCLabelTTF::create(text.c_str(),systemFont(),size,bounds,kCCTextAlignmentCenter):CCLabelTTF::create(text.c_str(),systemFont(),size);
 l->setColor(Text);return l;
}
void roundRect(CCDrawNode* d,float x,float y,float w,float h,float radius,ccColor4F fill,ccColor4F stroke=Border){
 radius=std::min({radius,w/2,h/2});CCPoint points[24];int count=0;
 for(int corner=0;corner<4;corner++){
  float cx=(corner==0||corner==3)?x+w-radius:x+radius;
  float cy=corner<2?y+h-radius:y+radius;
  for(int i=0;i<6;i++){double a=(corner*90+i*18)*3.141592653589793/180.;points[count++]=CCPoint{cx+radius*static_cast<float>(std::cos(a)),cy+radius*static_cast<float>(std::sin(a))};}
 }
 d->drawPolygon(points,count,fill,.65f,stroke);
}
CCNode* tile(float w,float h,ccColor4F fill,ccColor4F stroke=Border){
 auto n=CCNode::create();n->setContentSize({w,h});auto d=CCDrawNode::create();roundRect(d,0,0,w,h,7,fill,stroke);n->addChild(d);return n;
}
void setText(CCLabelTTF* l,std::string const&s){if(s!=l->getString())l->setString(s.c_str());}
CCMenuItemSpriteExtra* control(CCObject* target,SEL_MenuHandler callback,char const* caption,float w,float h,ccColor4F fill,ccColor4F stroke=Border){
 auto bg=tile(w,h,fill,stroke);auto text=label(caption,h<26?10:12);
 text->setScale(std::min(1.f,(w-14)/std::max(1.f,text->getContentSize().width)));text->setPosition({w/2,h/2});bg->addChild(text);
 auto b=CCMenuItemSpriteExtra::create(bg,target,callback);b->m_scaleMultiplier=1.025f;return b;
}
class Menu final:public Popup {
 Ref<PauseLayer> m_pause;Ref<EndLevelLayer> m_end;
 std::array<std::vector<CCMenuItemSpriteExtra*>,3> m_buttons;
 std::vector<CCNode*> m_botNodes;
 std::array<CCMenuItemSpriteExtra*,3> m_tabs{};
 CCLabelTTF *m_info=nullptr,*m_best=nullptr,*m_attempts=nullptr,*m_time=nullptr,*m_pathCaption=nullptr,*m_smoothCaption=nullptr,*m_helpCaption=nullptr;
 float m_elapsed=1;int m_currentPage=0;
 CCLabelTTF* caption(CCMenuItemSpriteExtra*b){return static_cast<CCLabelTTF*>(b->getNormalImage()->getChildren()->objectAtIndex(1));}
 CCMenuItemSpriteExtra* add(char const* text,int page,CCPoint pos,SEL_MenuHandler action,float width=200,float height=36,ccColor4F color=Card){
  auto b=control(this,action,text,width,height,color);b->setPosition(pos);m_buttonMenu->addChild(b);if(page>=0)m_buttons[page].push_back(b);return b;
 }
 bool initMenu(PauseLayer*p,EndLevelLayer*e){
  auto view=CCDirector::sharedDirector()->getWinSize();
  if(!Popup::init(460,300))return false;m_pause=p;m_end=e;m_bgSprite->setVisible(false);m_closeBtn->setVisible(false);m_closeBtn->setEnabled(false);
  m_mainLayer->setScale(std::min({1.f,(view.width-24)/460,(view.height-24)/300}));
  auto skin=CCDrawNode::create();roundRect(skin,0,0,460,300,12,Panel);m_mainLayer->addChild(skin,-1);
  auto title=label("Route Finder",19);title->setAnchorPoint({0,.5f});title->setPosition({24,274});m_mainLayer->addChild(title);
  auto version=label(fmt::format("v{}",ModVersion),9);version->setColor(Muted);version->setPosition({375,275});m_mainLayer->addChild(version);
  m_mainLayer->updateLayout();add("×",-1,{434,275},menu_selector(Menu::close),26,26);
  m_tabs[0]=add("Бот",-1,{91,230},menu_selector(Menu::botTab),130,28);
  m_tabs[1]=add("Макрос",-1,{230,230},menu_selector(Menu::macroTab),130,28);
  m_tabs[2]=add("Настройки",-1,{369,230},menu_selector(Menu::settingsTab),130,28);
  auto metric=[&](char const* text,float x){auto card=tile(130,48,Card);card->setPosition({x-65,157});m_mainLayer->addChild(card);m_botNodes.push_back(card);
   auto name=label(text,9);name->setColor(Muted);name->setPosition({65,35});card->addChild(name);auto value=label("—",20);value->setPosition({65,15});card->addChild(value);return value;};
  m_attempts=metric("Попытки",92);m_best=metric("Лучший результат",230);m_time=metric("Осталось",368);
  auto start=add("Найти прохождение",0,{230,128},menu_selector(Menu::search),412,36,Blue);caption(start)->setColor({255,255,255});
  auto stop=add("Остановить",0,{126,83},menu_selector(Menu::stopSearch));caption(stop)->setColor({207,46,65});
  m_helpCaption=caption(add("Ручная помощь",0,{334,83},menu_selector(Menu::help)));
  add("Записать вручную",1,{126,175},menu_selector(Menu::record));add("Загрузить макрос",1,{334,175},menu_selector(Menu::loadMacro));
  add("Сохранить макрос",1,{126,122},menu_selector(Menu::saveMacro));add("Воспроизвести",1,{334,122},menu_selector(Menu::playMacro));
  m_pathCaption=caption(add("",2,{230,180},menu_selector(Menu::path),412,34));
  m_smoothCaption=caption(add("",2,{230,135},menu_selector(Menu::smooth),412,34));
  add("Лимиты поиска",2,{230,90},menu_selector(Menu::limits),412,34);
  auto status=tile(412,46,Card);status->setPosition({24,16});m_mainLayer->addChild(status);
  m_info=label("",10,{392,0});m_info->setColor(Muted);m_info->setPosition({206,23});status->addChild(m_info);
  pages(0);Runtime::get().setMenuOpen(true);scheduleUpdate();update(0);return true;
 }
 void update(float dt)override{
  auto&r=Runtime::get();r.poll();m_elapsed+=dt;if(m_elapsed<.1f)return;m_elapsed=0;
  setText(m_attempts,fmt::format("{}",r.attempts()));setText(m_best,fmt::format("{:.1f}%",r.bestPercent()));
  setText(m_time,r.active()?fmt::format("{}:{:02}",r.remainingSeconds()/60,r.remainingSeconds()%60):"—");
  setText(m_pathCaption,r.previewEnabled()?"Траектория: показывать":"Траектория: скрыть");
  setText(m_smoothCaption,r.smoothSearch()?"Нагрузка: ниже":"Нагрузка: обычная");
  setText(m_helpCaption,r.assisting()?"Вернуть управление боту":"Ручная помощь");
  setText(m_info,r.status()+(r.dualEnabled()?"\nDual: общая кнопка для обоих игроков":""));
  m_info->setScale(std::min(1.f,36.f/std::max(1.f,m_info->getContentSize().height)));
 }
 void pages(int page){m_currentPage=page;
  for(int i=0;i<3;i++){for(auto b:m_buttons[i]){b->setVisible(page==i);b->setEnabled(page==i);}
   auto bg=static_cast<CCDrawNode*>(m_tabs[i]->getNormalImage()->getChildren()->objectAtIndex(0));bg->clear();roundRect(bg,0,0,130,28,7,page==i?Blue:Card);caption(m_tabs[i])->setColor(page==i?ccColor3B{255,255,255}:Text);}
  for(auto n:m_botNodes)n->setVisible(page==0);m_elapsed=1;
 }
 void onClose(CCObject*s)override{Runtime::get().setMenuOpen(false);Popup::onClose(s);}
 void continueRun(){if(!Runtime::get().active())return;Runtime::get().resumeOverlay(m_pause.data(),m_end.data());onClose(nullptr);}
 void close(CCObject*s){onClose(s);}void botTab(CCObject*){pages(0);}void macroTab(CCObject*){pages(1);}void settingsTab(CCObject*){pages(2);}
 void help(CCObject*){auto&r=Runtime::get();if(r.assisting())r.returnToBot();else r.takeControl();continueRun();}
 void path(CCObject*){Runtime::get().togglePreview();m_elapsed=1;}void smooth(CCObject*){Runtime::get().toggleSmooth();m_elapsed=1;}
 void limits(CCObject*){geode::openSettingsPopup(Mod::get());}
 void search(CCObject*){Runtime::get().startSearch();continueRun();}void stopSearch(CCObject*){Runtime::get().stop();m_elapsed=1;}
 void saveMacro(CCObject*){Runtime::get().save();m_elapsed=1;}void loadMacro(CCObject*){Runtime::get().load();m_elapsed=1;}
 void playMacro(CCObject*){Runtime::get().play();continueRun();}void record(CCObject*){Runtime::get().startRecording();continueRun();}
public:static Menu* create(PauseLayer*p,EndLevelLayer*e){auto r=new Menu;if(r->initMenu(p,e)){r->autorelease();return r;}delete r;return nullptr;}
};
class Hud final:public CCNode {
 CCMenu* m_menu=nullptr;CCMenuItemSpriteExtra *m_open=nullptr,*m_stop=nullptr,*m_help=nullptr;
 CCLabelTTF* m_status=nullptr;CCNode* m_plate=nullptr;CCDrawNode* m_draw=nullptr;
 CCSize m_viewport;float m_textTime=1,m_pathTime=1;bool m_hadPath=false;std::string m_lastMode;
 void open(CCObject*){showMenu();}void stop(CCObject*){Runtime::get().stop();}
 void help(CCObject*){auto&r=Runtime::get();if(r.assisting())r.returnToBot();else r.takeControl();}
 void update(float dt)override{
  auto&r=Runtime::get();r.poll();auto size=CCDirector::sharedDirector()->getWinSize();bool resize=size.width!=m_viewport.width||size.height!=m_viewport.height;
  if(resize){m_viewport=size;setContentSize(size);m_menu->setContentSize(size);m_open->setPosition({size.width-166,size.height-58});m_stop->setPosition({size.width-112,size.height-58});m_help->setPosition({size.width-58,size.height-58});m_plate->setPosition({size.width-212,size.height-98});float w=190;m_plate->setContentSize({w,54});
   auto bg=static_cast<CCDrawNode*>(m_plate->getChildren()->objectAtIndex(0));bg->clear();roundRect(bg,0,0,w,54,7,{1,1,1,.94f});m_textTime=1;}
  m_textTime+=dt;m_pathTime+=dt;auto mode=r.modeName();
  if(m_textTime>=.1f||mode!=m_lastMode){m_textTime=0;m_lastMode=mode;setText(m_status,fmt::format("{}{}\n#{}  ·  {:.1f}%{}",mode,r.dualEnabled()?"  ·  dual":"",r.attempts(),r.bestPercent(),r.searching()?fmt::format("  ·  {}/16",r.cps()):""));m_status->setScale(std::min(1.f,172.f/std::max(1.f,m_status->getContentSize().width)));}
  bool visible=r.showPreview()&&!r.frozen();auto layer=r.layer();
  if(!visible||!layer||!layer->m_player1||!layer->m_player1->getParent()){if(m_hadPath){m_draw->clear();m_hadPath=false;}return;}
  if(m_pathTime<1.f/30&&!resize)return;m_pathTime=0;m_draw->clear();m_hadPath=true;
  auto local=worldToNodeTransform();
  auto draw=[&](PlayerObject* p,Forecast const&trace,ccColor4F color){if(!p||!p->getParent())return;auto world=p->getParent()->nodeToWorldTransform();
    auto screen=[&](Point q){return CCPointApplyAffineTransform(CCPointApplyAffineTransform({static_cast<float>(q.x),static_cast<float>(q.y)},world),local);};
    for(std::size_t i=1;i<trace.path.size()&&i<160;i++)m_draw->drawSegment(screen(trace.path[i-1]),screen(trace.path[i]),.7f,color);
    if(trace.collision)m_draw->drawDot(screen(*trace.collision),2.6f,Coral);
  };
  draw(layer->m_player1,r.forecast(),{.2f,.45f,1,.8f});if(r.dualEnabled())draw(layer->m_player2,r.secondForecast(),{.9f,.25f,.55f,.8f});
 }
public:static Hud* create(){
 auto h=new Hud;if(!h->init()){delete h;return nullptr;}h->autorelease();h->setAnchorPoint({0,0});h->setPosition({0,0});
 h->m_menu=CCMenu::create();h->m_menu->ignoreAnchorPointForPosition(false);h->m_menu->setAnchorPoint({0,0});h->m_menu->setPosition({0,0});h->addChild(h->m_menu);
 h->m_open=control(h,menu_selector(Hud::open),"BOT",48,24,Card,Mint);h->m_menu->addChild(h->m_open);
 h->m_stop=control(h,menu_selector(Hud::stop),"STOP",48,24,Card,Coral);h->m_menu->addChild(h->m_stop);
 h->m_help=control(h,menu_selector(Hud::help),"HELP",48,24,Card,Blue);h->m_menu->addChild(h->m_help);
 float width=190;h->m_plate=tile(width,54,{1,1,1,.94f});h->addChild(h->m_plate,-1);
 h->m_status=label("",8);h->m_status->setAnchorPoint({.5f,.5f});h->m_status->setPosition({95,13});h->m_plate->addChild(h->m_status);
 h->m_draw=CCDrawNode::create();h->addChild(h->m_draw,-2);h->scheduleUpdate();h->update(0);return h;
 }
};
}
void showMenu(PauseLayer*p,EndLevelLayer*e){Runtime::get().syncActiveLayer();if(!Runtime::get().frozen())if(auto m=Menu::create(p,e))m->show();}
void attachHud(PlayLayer*l){auto p=l->m_uiLayer;if(!p||p->getChildByID("routefinder-hud"))return;if(auto h=Hud::create()){h->setID("routefinder-hud");p->addChild(h,10000);}}
}
