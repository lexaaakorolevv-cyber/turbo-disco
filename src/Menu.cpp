#include "Runtime.hpp"
#include "ui_geometry.hpp"
#include <Geode/ui/Popup.hpp>
#include <Geode/cocos/draw_nodes/CCDrawNode.h>
#include <algorithm>
#include <cmath>
using namespace geode::prelude;
namespace rf { namespace {
constexpr ccColor4F Panel{.047f,.071f,.114f,1},Card{.082f,.118f,.176f,1},Border{.16f,.22f,.30f,1};
constexpr ccColor4F Mint{.27f,.88f,.69f,1},Blue{.35f,.61f,.97f,1},Coral{.96f,.36f,.44f,1};
constexpr ccColor3B Text{229,237,248},Muted{142,162,188};
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
 std::vector<CCMenuItemSpriteExtra*> m_primary,m_extra;
 CCLabelTTF *m_info=nullptr,*m_best=nullptr,*m_attempts=nullptr,*m_state=nullptr,*m_page=nullptr;
 CCLabelTTF *m_pathCaption=nullptr,*m_smoothCaption=nullptr;
 CCDrawNode* m_progress=nullptr;PanelGeometry m_geometry{};
 bool m_compact=false;float m_elapsed=1;std::string m_lastMode;
 bool initMenu(PauseLayer* pause,EndLevelLayer* end){
  auto viewport=CCDirector::sharedDirector()->getWinSize();m_geometry=panelGeometry(viewport.width,viewport.height);auto g=m_geometry;
  if(!Popup::init(g.width,g.height))return false;m_pause=pause;m_end=end;m_bgSprite->setVisible(false);
  auto skin=CCDrawNode::create();roundRect(skin,2,-3,g.width,g.height,12,{0,0,0,.35f},{0,0,0,0});roundRect(skin,0,0,g.width,g.height,12,Panel);m_mainLayer->addChild(skin,-1);
  auto mark=CCDrawNode::create();mark->drawSegment({18,g.titleY-6},{24,g.titleY+6},1.3f,Mint);mark->drawSegment({24,g.titleY+6},{32,g.titleY-2},1.3f,Mint);mark->drawDot({18,g.titleY-6},2.2f,Mint);mark->drawDot({32,g.titleY-2},2.2f,Mint);m_mainLayer->addChild(mark);
  auto title=label("ROUTE FINDER",17);title->setAnchorPoint({0,.5f});title->setPosition({42,g.titleY});m_mainLayer->addChild(title);
  auto version=label(fmt::format("v{}",ModVersion),8);version->setColor(Muted);version->setAnchorPoint({1,.5f});version->setPosition({g.width-20,g.titleY});m_mainLayer->addChild(version);
  m_page=label("ПОИСК И МАКРОСЫ",8);m_page->setColor(Muted);m_page->setAnchorPoint({0,.5f});m_page->setPosition({18,g.titleY-19});m_mainLayer->addChild(m_page);
  m_mainLayer->updateLayout();
  auto add=[&](char const* caption,int column,int row,SEL_MenuHandler callback,ccColor4F fill=Card,ccColor4F stroke=Border){
   auto b=control(this,callback,caption,g.buttonWidth,g.buttonHeight,fill,stroke);b->setPosition({column?g.rightX:g.leftX,g.firstRowY-row*g.rowSpacing});m_buttonMenu->addChild(b);m_primary.push_back(b);return static_cast<CCLabelTTF*>(b->getNormalImage()->getChildren()->objectAtIndex(1));
  };
  add("Найти прохождение",0,0,menu_selector(Menu::search),{.10f,.33f,.29f,1},Mint);
  add("Остановить",1,0,menu_selector(Menu::stopSearch),{.24f,.10f,.15f,1},Coral);
  add("Сохранить макрос",0,1,menu_selector(Menu::saveMacro));add("Загрузить макрос",1,1,menu_selector(Menu::loadMacro));
  add("Воспроизвести",0,2,menu_selector(Menu::playMacro),{.10f,.20f,.35f,1},Blue);add("Дополнительно",1,2,menu_selector(Menu::more));
  auto primary=std::move(m_primary);m_primary.clear();
  add("Записать вручную",0,0,menu_selector(Menu::record));add("Взять управление",1,0,menu_selector(Menu::assist));
  add("Вернуть боту",0,1,menu_selector(Menu::giveBack));m_pathCaption=add("",1,1,menu_selector(Menu::path));
  add("Назад",0,2,menu_selector(Menu::back));m_smoothCaption=add("",1,2,menu_selector(Menu::smooth));
  m_extra=std::move(m_primary);m_primary=std::move(primary);pages(false);
  auto card=tile(g.width-32,g.statusHeight,Card);card->setPosition({16,g.statusY-g.statusHeight/2});m_mainLayer->addChild(card);
  m_compact=g.statusHeight<65;float top=g.statusHeight;
  m_state=label("",8);m_state->setAnchorPoint({0,.5f});m_state->setColor({81,225,177});m_state->setPosition({12,top-12});card->addChild(m_state);
  m_best=label("",20);m_best->setAnchorPoint({0,.5f});m_best->setPosition({12,top-32});card->addChild(m_best);
  m_attempts=label("",10);m_attempts->setAnchorPoint({1,.5f});m_attempts->setColor(Muted);m_attempts->setPosition({g.width-56,top-32});card->addChild(m_attempts);
  m_progress=CCDrawNode::create();card->addChild(m_progress);
  m_info=label("",m_compact?8:9,{g.width-56,0});m_info->setColor(Muted);m_info->setPosition({(g.width-32)/2,m_compact?top/2:16});card->addChild(m_info);
  m_state->setVisible(!m_compact);m_best->setVisible(!m_compact);m_attempts->setVisible(!m_compact);
  auto hint=label("16 CPS  /  HELP — ручная помощь",8);hint->setColor(Muted);hint->setPosition({g.width/2,16});hint->setScale(std::min(1.f,(g.width-24)/std::max(1.f,hint->getContentSize().width)));m_mainLayer->addChild(hint);
  Runtime::get().setMenuOpen(true);scheduleUpdate();update(0);return true;
 }
 void update(float dt) override{
  auto&r=Runtime::get();r.poll();m_elapsed+=dt;auto mode=r.modeName();
  if(m_elapsed<.1f&&mode==m_lastMode)return;m_elapsed=0;m_lastMode=mode;
  auto g=m_geometry;setText(m_state,r.active()?fmt::format("{}  /  {}:{:02}",mode,r.remainingSeconds()/60,r.remainingSeconds()%60):mode);setText(m_best,fmt::format("{:.2f}%",r.bestPercent()));setText(m_attempts,fmt::format("{} попыток",r.attempts()));
  setText(m_pathCaption,r.previewEnabled()?"Траектория: вкл":"Траектория: выкл");setText(m_smoothCaption,r.smoothSearch()?"Режим: плавный":"Режим: точный");
  for(auto text:{m_pathCaption,m_smoothCaption})text->setScale(std::min(1.f,(g.buttonWidth-14)/std::max(1.f,text->getContentSize().width)));
  auto info=m_compact?fmt::format("{} попыток  /  {:.1f}%\n{}",r.attempts(),r.bestPercent(),r.status()):r.detail();setText(m_info,info);
  m_info->setScale(std::min(1.f,(m_compact?g.statusHeight-6:25.f)/std::max(1.f,m_info->getContentSize().height)));
  if(!m_compact){m_progress->clear();float y=g.statusHeight-48,w=g.width-56;roundRect(m_progress,12,y,w,3,1.5f,Border,Border);float fill=w*std::clamp(static_cast<float>(r.bestPercent())/100,0.f,1.f);if(fill>.1f)roundRect(m_progress,12,y,fill,3,1.5f,Mint,Mint);}
 }
 void onClose(CCObject*s)override{Runtime::get().setMenuOpen(false);Popup::onClose(s);}
 void continueRun(){if(!Runtime::get().active())return;Runtime::get().resumeOverlay(m_pause.data(),m_end.data());onClose(nullptr);}
 void pages(bool extra){for(auto b:m_primary){b->setVisible(!extra);b->setEnabled(!extra);}for(auto b:m_extra){b->setVisible(extra);b->setEnabled(extra);}setText(m_page,extra?"УПРАВЛЕНИЕ И ОТОБРАЖЕНИЕ":"ПОИСК И МАКРОСЫ");m_elapsed=1;}
 void more(CCObject*){pages(true);}void back(CCObject*){pages(false);}
 void assist(CCObject*){Runtime::get().takeControl();continueRun();}void giveBack(CCObject*){Runtime::get().returnToBot();continueRun();}
 void path(CCObject*){Runtime::get().togglePreview();m_elapsed=1;}void smooth(CCObject*){Runtime::get().toggleSmooth();m_elapsed=1;}
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
  if(resize){m_viewport=size;setContentSize(size);m_menu->setContentSize(size);m_open->setPosition({size.width-42,size.height-60});m_stop->setPosition({size.width-42,size.height-90});m_help->setPosition({size.width-42,size.height-120});m_plate->setPosition({12,size.height-76});float w=std::min(260.f,size.width-108);m_plate->setContentSize({w,32});
   auto bg=static_cast<CCDrawNode*>(m_plate->getChildren()->objectAtIndex(0));bg->clear();roundRect(bg,0,0,w,32,7,{.047f,.071f,.114f,.88f});m_textTime=1;}
  m_textTime+=dt;m_pathTime+=dt;auto mode=r.modeName();
  if(m_textTime>=.1f||mode!=m_lastMode){m_textTime=0;m_lastMode=mode;setText(m_status,fmt::format("RF  /  {}{}\nПопыток: {}    Лучший: {:.1f}%",mode,r.searching()?fmt::format("  /  {}/16 CPS",r.cps()):"",r.attempts(),r.bestPercent()));float width=std::min(260.f,size.width-108);m_status->setScale(std::min(1.f,(width-20)/std::max(1.f,m_status->getContentSize().width)));}
  bool visible=r.showPreview()&&!r.frozen();auto layer=r.layer();
  if(!visible||!layer||!layer->m_player1||!layer->m_player1->getParent()){if(m_hadPath){m_draw->clear();m_hadPath=false;}return;}
  if(m_pathTime<1.f/30&&!resize)return;m_pathTime=0;m_draw->clear();m_hadPath=true;
  auto world=layer->m_player1->getParent()->nodeToWorldTransform(),local=worldToNodeTransform();
  auto screen=[&](Point p){return CCPointApplyAffineTransform(CCPointApplyAffineTransform({static_cast<float>(p.x),static_cast<float>(p.y)},world),local);};
  auto const&trace=r.forecast();
  // Keep every corner; drawing is capped at 30 Hz, not every render frame.
  for(std::size_t i=1;i<trace.path.size()&&i<160;i++)m_draw->drawSegment(screen(trace.path[i-1]),screen(trace.path[i]),.7f,{.27f,.88f,.69f,.72f});
  if(trace.collision)m_draw->drawDot(screen(*trace.collision),2.6f,Coral);
 }
public:static Hud* create(){
 auto h=new Hud;if(!h->init()){delete h;return nullptr;}h->autorelease();h->setAnchorPoint({0,0});h->setPosition({0,0});
 h->m_menu=CCMenu::create();h->m_menu->ignoreAnchorPointForPosition(false);h->m_menu->setAnchorPoint({0,0});h->m_menu->setPosition({0,0});h->addChild(h->m_menu);
 h->m_open=control(h,menu_selector(Hud::open),"BOT",60,24,Card,Mint);h->m_menu->addChild(h->m_open);
 h->m_stop=control(h,menu_selector(Hud::stop),"STOP",60,24,{.24f,.10f,.15f,.95f},Coral);h->m_menu->addChild(h->m_stop);
 h->m_help=control(h,menu_selector(Hud::help),"HELP",60,24,Card,Blue);h->m_menu->addChild(h->m_help);
 float width=std::min(260.f,CCDirector::sharedDirector()->getWinSize().width-108);h->m_plate=tile(width,32,{.047f,.071f,.114f,.88f});h->addChild(h->m_plate);
 h->m_status=label("",9);h->m_status->setAnchorPoint({0,.5f});h->m_status->setPosition({10,16});h->m_plate->addChild(h->m_status);
 h->m_draw=CCDrawNode::create();h->addChild(h->m_draw,-1);h->scheduleUpdate();h->update(0);return h;
 }
};
}
void showMenu(PauseLayer*p,EndLevelLayer*e){Runtime::get().syncActiveLayer();if(!Runtime::get().frozen())if(auto m=Menu::create(p,e))m->show();}
void attachHud(PlayLayer*l){auto p=l->m_uiLayer;if(!p||p->getChildByID("routefinder-hud"))return;if(auto h=Hud::create()){h->setID("routefinder-hud");p->addChild(h,10000);}}
}
