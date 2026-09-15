#include "gamemenu.h"
#include "utils/feedback.h"

#include <Tempest/Painter>
#include <Tempest/Log>
#include <Tempest/TextCodec>
#include <Tempest/Dialog>
#include <Tempest/SystemApi>

#include <algorithm>

#include "utils/string_frm.h"
#include "world/objects/npc.h"
#include "world/world.h"
#include "ui/menuroot.h"
#include "utils/gthfont.h"
#include "utils/fileutil.h"
#include "utils/saveslot.h"
#include "utils/keycodec.h"
#include "game/definitions/musicdefinitions.h"
#include "game/serialize.h"
#include "game/savegameheader.h"
#include "gothic.h"
#include "resources.h"
#include "build.h"

using namespace Tempest;

static const float scriptDiv=8192.0f;


struct GameMenu::KeyEditDialog : Dialog {
  KeyEditDialog(){
    setFocusPolicy(ClickFocus);
    setCursorShape(CursorShape::Hidden);
    setFocus(true);
    }
  void keyDownEvent(KeyEvent &e) override { e.accept(); }
  void keyUpEvent  (KeyEvent &e) override {
    close();
    key = e.key;
    }

  void mouseDownEvent(MouseEvent& e) override { e.accept(); }
  void mouseUpEvent  (MouseEvent& e) override {
    close();
    mkey = e.button;
    }

  void paintEvent (PaintEvent&) override {}
  void paintShadow(PaintEvent&) override {}

  Event::KeyType     key  = Event::K_ESCAPE;
  Event::MouseButton mkey = Event::ButtonNone;
  };

struct GameMenu::SavNameDialog : Dialog {
  SavNameDialog(GameMenu& menu, std::string& text):menu(menu), text(text), text0(text) {
    setFocusPolicy(ClickFocus);
    setCursorShape(CursorShape::Hidden);
    setFocus(true);
    }

  ~SavNameDialog() override {
    SystemApi::hideSoftInput();
    menu.update();
    }

  void beginTextInput() {
    menu.update();
    SystemApi::showSoftInput(text);
    }

  void mouseDownEvent(MouseEvent& e) override { e.accept(); }
  void mouseUpEvent  (MouseEvent&) override {
    close();
    accepted = true;
    }

  void keyDownEvent(KeyEvent &e) override { e.accept(); }
  void keyUpEvent  (KeyEvent &e) override {
    update();
    // The name is painted by the underlying menu, not this transparent input dialog.
    menu.update();

    if(e.key==Event::K_ESCAPE) {
      text = text0;
      close();
      return;
      }
    if(e.key==Event::K_Return) {
      accepted = true;
      close();
      return;
      }
    if(e.key==Event::K_Back && text.size()>0) {
      text.pop_back();
      return;
      }

    char ch[2] = {'\0','\0'};
    if(('a'<=e.code && e.code<='z') || ('A'<=e.code && e.code<='Z')  ||
       ('0'<=e.code && e.code<='9') || e.code==' ' || e.code=='.')
      ch[0] = char(e.code);
    if(ch[0]=='\0')
      return;
    text = text + ch;
    }

  void paintEvent (PaintEvent&) override {}
  void paintShadow(PaintEvent&) override {}

  GameMenu&    menu;
  std::string& text;
  std::string  text0;

  bool         accepted = false;
  };

GameMenu::GameMenu(MenuRoot &owner, KeyCodec& keyCodec, zenkit::DaedalusVm& vm, std::string_view menuSection, KeyCodec::Action kClose)
  :owner(owner), keyCodec(keyCodec), vm(&vm), kClose(kClose) {
  setCursorShape(CursorShape::Hidden);
  timer.timeout.bind(this,&GameMenu::onTick);
  timer.start(100);

  textBuf.reserve(64);

  auto* menuSectionSymbol = vm.find_symbol_by_name(menuSection);
  if(menuSectionSymbol!=nullptr) {
    menu = vm.init_instance<zenkit::IMenu>(menuSectionSymbol);
    } else {
    Tempest::Log::e("Cannot initialize menu ", menuSection, ": Symbol not found.");
    menu = std::make_shared<zenkit::IMenu>();
    }

  back = Resources::loadTexture(menu->back_pic);

  initItems();
  float infoX = 1000.0f/scriptDiv;
  float infoY = 7500.0f/scriptDiv;

  // There could be script-defined values
  if(vm.find_symbol_by_name("MENU_INFO_X") != nullptr && vm.find_symbol_by_name("MENU_INFO_X") != nullptr) {
    auto* symX = vm.find_symbol_by_name("MENU_INFO_X");
    auto* symY = vm.find_symbol_by_name("MENU_INFO_Y");

    infoX = float(symX->get_int())/scriptDiv;
    infoY = float(symY->get_int())/scriptDiv;
    }
  setPosition(int(infoX*float(w())),int(infoY*float(h())));

  setSelection(Gothic::inst().isInGameAndAlive() ? menu->default_ingame : menu->default_outgame);
  // Prefer an existing save at startup or after death; preserve the living player's pause-menu default.
  if(menuSection==Gothic::inst().menuMain() && !Gothic::inst().isInGameAndAlive()) {
    for(int i=0;i<zenkit::IMenu::item_count;++i) {
      const auto& item=hItems[i];
      if(item.name=="MENUITEM_MAIN_SAVEGAME_LOAD" && isSelectable(item.handle) && isEnabled(item.handle)) {
        if(SaveSlot::hasAny("."))
          setSelection(i);
        break;
        }
      }
    }
  updateValues();
  slider = Resources::loadTexture("MENU_SLIDER_POS.TGA");

  up     = Resources::loadTexture("O.TGA");
  down   = Resources::loadTexture("U.TGA");

  Gothic::inst().pushPause();
  }

GameMenu::~GameMenu() {
  Gothic::flushSettings();
  Gothic::inst().popPause();
  Resources::device().waitIdle(); // safe-delete savethumb
  }

void GameMenu::resetVm(zenkit::DaedalusVm* inVm) {
  while(closeNestedView()) {}
  vm = inVm;
  for(int i=0; i<zenkit::IMenu::item_count; ++i){
    hItems[i].handle = nullptr;
    }
  if(vm==nullptr)
    return;
  initItems();
  updateValues();
  }

GameMenu::QuestStat GameMenu::toStatus(std::string_view str) {
  if(str=="CURRENTMISSIONS")
    return QuestStat::Current;
  if(str=="OLDMISSIONS")
    return QuestStat::Old;
  if(str=="FAILEDMISSIONS")
    return QuestStat::Failed;
  if(str=="LOG")
    return QuestStat::Log;
  return QuestStat::Log;
  }

bool GameMenu::isCompatible(const QuestLog::Quest& q, QuestStat st) {
  if(q.section==QuestLog::Section::Note && st==QuestStat::Log)
    return true;
  return (uint8_t(q.status)==uint8_t(st) && q.section!=QuestLog::Section::Note);
  }

int32_t GameMenu::numQuests(const QuestLog* ql, QuestStat st) {
  if(ql==nullptr)
    return 0;
  int32_t num = 0;
  for(size_t i=0; i<ql->questCount(); ++i)
    if(isCompatible(ql->quest(i),st))
      ++num;
  return num;
  }

void GameMenu::initItems() {
  for(int i=0; i<zenkit::IMenu::item_count; ++i){
    if(menu->items[i].empty())
      continue;

    hItems[i].name = menu->items[i];

    auto* menuItemSymbol = vm->find_symbol_by_name(hItems[i].name);
    if (menuItemSymbol != nullptr) {
      hItems[i].handle = vm->init_instance<zenkit::IMenuItem>(menuItemSymbol);
      } else {
      Tempest::Log::e("Cannot initialize menu item ", hItems[i].name, ": Symbol not found.");
      hItems[i].handle = std::make_shared<zenkit::IMenuItem>();
      }

    hItems[i].img = Resources::loadTexture(hItems[i].handle->backpic);

    if(hItems[i].handle->type==zenkit::MenuItemType::LISTBOX) {
      hItems[i].visible = false;
      }
    updateItem(hItems[i]);
    }
  addMissingQuickLoad();
  initAndroidVideo();
  }

void GameMenu::initAndroidVideo() {
#if defined(__ANDROID__)
  const Item* resolution=nullptr;
  const Texture2d* sliderBackground=nullptr;
  for(const auto& item:hItems)
    if(item.name=="MENUITEM_VID_RESOLUTION_CHOICE") resolution=&item;
  for(const auto& item:hItems)
    if(item.name=="MENUITEM_VID_BRIGHTNESS_SLIDER") sliderBackground=item.img;
  if(resolution==nullptr) return;
  const auto font=resolution->handle->fontname;
  auto labelFont=font;
  auto titleFont=font;
  auto backFont=font;
  std::string titleText="VIDEO SETTINGS", backText="BACK";
  for(const auto& item:hItems) {
    if(item.name=="MENUITEM_VID_RESOLUTION") labelFont=item.handle->fontname;
    if(item.name=="MENUITEM_VID_HEADLINE") {
      titleFont=item.handle->fontname;
      titleText=item.handle->text[0];
      }
    if(item.name=="MENUITEM_VID_BACK") {
      backFont=item.handle->fontname;
      backText=item.handle->text[0];
      }
    }
  // Keep the original Gothic menu background and fonts, without editing either game's scripts.
  for(auto& item:hItems) item=Item();
  size_t index=0;
  auto add=[&](const char* name,const char* text,int x,int y,int width,zenkit::MenuItemType type,bool selectable) -> Item& {
    auto& item=hItems[index++];
    item.name=name;
    item.handle=std::make_shared<zenkit::IMenuItem>();
    auto& h=*item.handle;
    h.fontname=font;
    h.text[0]=text;
    h.pos_x=x; h.pos_y=y; h.dim_x=width; h.dim_y=350;
    h.type=type;
    h.flags=selectable ? zenkit::MenuItemFlag::SELECTABLE : zenkit::MenuItemFlag(0);
    return item;
    };
  auto& title=add("ANDROID_VIDEO_TITLE",titleText.c_str(),1000,750,6200,zenkit::MenuItemType::TEXT,false);
  title.handle->fontname=titleFont;
  title.handle->dim_y=650;
  title.handle->flags=zenkit::MenuItemFlag::CENTERED;
  struct Row { const char* name; const char* label; const char* values; const char* section; const char* option; };
  const Row rows[]={
    {"ANDROID_SCALE","Resolution","","ENGINE","renderScale"},
    {"ANDROID_FOG","Half-res fog","Off|On","ENGINE","fogHalfResolution"},
    {"ANDROID_SSAO","Half-res SSAO","Off|On","ENGINE","ssaoHalfResolution"},
    {"ANDROID_SHADOWS","Shadows","512|1024|1536|2048","ENGINE","shadowMapResolution"},
    {"ANDROID_BRIGHTNESS","Brightness","","VIDEO","zVidBrightness"},
    {"ANDROID_CONTRAST","Contrast","","VIDEO","zVidContrast"},
    {"ANDROID_GAMMA","Gamma","","VIDEO","zVidGamma"},
    };
  int y=1850;
  for(const auto& row:rows) {
    const std::string labelName=std::string(row.name)+"_LABEL";
    auto& label=add(labelName.c_str(),row.label,1000,y,3700,zenkit::MenuItemType::TEXT,false);
    label.handle->fontname=labelFont;
    label.handle->dim_y=600;
    label.handle->user_string[0]=row.name;
    auto& item=add(row.name,row.values,4800,y,2200,
                   row.values[0]==0 ? zenkit::MenuItemType::SLIDER : zenkit::MenuItemType::CHOICEBOX,true);
    item.handle->on_chg_set_option_section=row.section;
    item.handle->on_chg_set_option=row.option;
    if(item.handle->type==zenkit::MenuItemType::SLIDER) {
      item.img=sliderBackground;
      item.handle->dim_y=600;
      }
    else {
      // Original Gothic choice values sit slightly below the larger label's top edge.
      item.handle->pos_y+=120;
      }
    updateItem(item);
    y+=650;
    }
  auto& back=add("ANDROID_VIDEO_BACK",backText.c_str(),1000,6800,6200,zenkit::MenuItemType::TEXT,true);
  back.handle->fontname=backFont;
  back.handle->dim_y=650;
  back.handle->flags=back.handle->flags | zenkit::MenuItemFlag::CENTERED;
  back.handle->on_sel_action[0]=int(zenkit::MenuItemSelectAction::BACK);
#endif
  }

void GameMenu::addMissingQuickLoad() {
  // Gothic 1's scripts omit slot zero. Reuse the load list's styling without modifying MENU.DAT.
  if(Gothic::inst().version().game!=1)
    return;
  int first=-1, last=-1, end=0;
  for(int i=0;i<zenkit::IMenu::item_count;++i) {
    const auto& item=hItems[i];
    if(item.handle==nullptr)
      continue;
    end=i+1;
    if(item.handle->on_sel_action_s[0]!="SAVEGAME_LOAD")
      continue;
    const auto slot=saveSlotId(item);
    if(slot==0)
      return;
    if(slot==size_t(-1))
      continue;
    if(first<0) first=i;
    last=i;
    }
  if(first<0 || end==zenkit::IMenu::item_count)
    return;

  Item quick;
  quick.name="MENUITEM_LOAD_SLOT0";
  quick.handle=std::make_shared<zenkit::IMenuItem>(*hItems[first].handle);
  quick.img=hItems[first].img;
  quick.handle->text[0]="- Quick Save -";
  const int spacing=last>first ? (hItems[last].handle->pos_y-hItems[first].handle->pos_y)/(last-first) : 300;
  quick.handle->pos_y=hItems[last].handle->pos_y+spacing;
  std::move_backward(hItems+last+1,hItems+end,hItems+end+1);
  hItems[last+1]=std::move(quick);
  updateItem(hItems[last+1]);
  }

void GameMenu::paintEvent(PaintEvent &e) {
  const float scale = Gothic::interfaceScale(this);

  Painter p(e);
  p.setScissor(-x(),-y(),owner.w(),owner.h());

  if(back) {
    p.setBrush(*back);
    p.drawRect(0,0,w(),h(),
               0,0,back->w(),back->h());
    }

  for(auto& hItem:hItems)
    drawItem(p,hItem);

  if(menu->flags & zenkit::MenuFlag::SHOW_INFO) {
    if(auto sel=selectedItem()) {
      auto& item = sel->handle;
      if(item->text->size()>0) {
        auto             fnt   = Resources::font(scale);
        std::string_view txt   = item->text[1];
        int              tw    = fnt.textSize(txt).w;

        fnt.drawText(p,(w()-tw)/2,h()-int(7*scale),txt);
        }
      }
    }

  if(owner.hasVersionLine()) {
    auto&       fnt   = Resources::font(scale);
    fnt.drawText(p, w()-fnt.textSize(appBuild).w-int(25*scale), h()-int(25*scale), appBuild);
    }

  if(pendingDelete!=nullptr) {
    const auto font = Resources::font(scale);
    const auto titleFont = Resources::font(scale*1.1f);
    const char* title = "Delete save permanently?";
    const string_frm name("\"",deleteName,"\"");
    const std::string_view hint = deleteError.empty() ? deleteHint : deleteError;
    const int padding = std::max(8,int(16*scale));
    const int gap = std::max(4,font.pixelSize()/2);
    const int border = std::max(1,int(scale));
    const int available = std::max(1,w()-2*padding);
    const int contentWidth = std::max({titleFont.textSize(title).w,font.textSize(name).w,font.textSize(hint).w});
    const int width = std::min(available,std::clamp(contentWidth+2*padding,int(280*scale),int(420*scale)));
    const int textWidth = std::max(1,width-2*padding);
    const int titleHeight = titleFont.textSize(textWidth,title).h;
    const int nameHeight = font.textSize(textWidth,name).h;
    const int hintHeight = font.textSize(textWidth,hint).h;
    const int height = 2*padding+titleHeight+nameHeight+hintHeight+3*gap+border;
    const int left = (w()-width)/2;
    const int top = (h()-height)/2;
    p.setBrush(Color(0.f,0.f,0.f,0.88f));
    p.drawRect(left,top,width,height);
    p.setBrush(Color(0.843f,0.761f,0.631f,0.85f));
    p.drawRect(left,top,width,border);
    p.drawRect(left,top+height-border,width,border);
    p.drawRect(left,top,border,height);
    p.drawRect(left+width-border,top,border,height);
    int row = top+padding;
    // Gothic font coordinates use the first line's baseline.
    titleFont.drawText(p,left+padding,row+titleFont.pixelSize(),textWidth,titleHeight,title,AlignHCenter);
    row+=titleHeight+gap;
    font.drawText(p,left+padding,row+font.pixelSize(),textWidth,nameHeight,name,AlignHCenter);
    row+=nameHeight+gap;
    p.setBrush(Color(0.843f,0.761f,0.631f,0.35f));
    p.drawRect(left+padding,row,textWidth,border);
    row+=border+gap;
    font.drawText(p,left+padding,row+font.pixelSize(),textWidth,hintHeight,hint,AlignHCenter);
    }
  }

void GameMenu::drawItem(Painter& p, Item& hItem) {
  if(!hItem.visible || hItem.name.empty())
    return;
  if(isHidden(hItem.handle))
    return;

  auto& item  = hItem.handle;
  auto flags = item->flags;
  getText(hItem,textBuf);

  const int32_t dimx = (item->dim_x!=-1) ? item->dim_x : 8192;
  const int32_t dimy = (item->dim_y!=-1) ? item->dim_y : 750;

  const int   x     = int(float(w()*item->pos_x)/scriptDiv);
  const int   y     = int(float(h()*item->pos_y)/scriptDiv);
  int         szX   = int(float(w()*dimx       )/scriptDiv);
  int         szY   = int(float(h()*dimy       )/scriptDiv);

  if(hItem.img && !hItem.img->isEmpty()) {
    p.setBrush(*hItem.img);
    p.drawRect(x,y,szX,szY,
               0,0,hItem.img->w(),hItem.img->h());
    }

  auto fnt = getTextFont(hItem);
  int  tw  = szX;
  int  th  = szY;

  AlignFlag txtAlign=NoAlign;
  if(flags & zenkit::MenuItemFlag::CENTERED) {
    txtAlign = AlignHCenter | AlignVCenter;
    }

  //p.setBrush(Color(1,1,1,1));
  //p.drawRect(x,y,szX,szY);
  {
  int padd = 0;
  if((flags & zenkit::MenuItemFlag::MULTILINE) &&
     std::min(tw,th)>100*2+fnt.pixelSize()) {
    padd = 100; // TODO: find out exact padding formula
    }
  auto tRect   = Rect(x+padd,y+fnt.pixelSize()+padd,
                      tw-2*padd, th-2*padd);

  if(flags & zenkit::MenuItemFlag::MULTILINE) {
    int lineCnt     = fnt.lineCount(tRect.w,textBuf.data());
    int linesInView = tRect.h/fnt.pixelSize();

    hItem.scroll = std::min(hItem.scroll, std::max(lineCnt-linesInView,0));
    hItem.scroll = std::max(hItem.scroll, 0);
    if(lineCnt>linesInView+hItem.scroll) {
      p.setBrush(*down);
      p.drawRect(x+(tw-down->w())/2, y+th-padd,
                 down->w(), down->h());
      }
    if(hItem.scroll>0) {
      p.setBrush(*up);
      p.drawRect(x+(tw-up->w())/2, y+padd-up->h(),
                 up->w(), up->h());
      }
    } else {
    tRect.h = std::max(tRect.h, fnt.pixelSize());
    }

  fnt.drawText(p, tRect.x,tRect.y, tRect.w, tRect.h,
               textBuf.data(), txtAlign, hItem.scroll);
  }

  if(item->type==zenkit::MenuItemType::SLIDER && slider!=nullptr) {
    drawSlider(p,hItem,x,y,szX,szY);
    }
  else if(item->type==zenkit::MenuItemType::LISTBOX) {
    if(auto ql = Gothic::inst().questLog()) {
      const int px = int(float(w()*item->frame_sizex)/scriptDiv);
      const int py = int(float(h()*item->frame_sizey)/scriptDiv);

      auto st = toStatus(item->user_string[0]);
      drawQuestList(p, hItem, x+px,y+py, szX-2*px,szY-2*py, *ql,st);
      }
    }
  else if(item->type==zenkit::MenuItemType::INPUT) {
    string_frm textBuf;
    if(item->on_chg_set_option_section=="KEYS") {
      auto keys = Gothic::settingsGetS(item->on_chg_set_option_section, item->on_chg_set_option);
      if(&hItem!=ctrlInput)
        textBuf = KeyCodec::keysStr(keys); else
        textBuf = "_";
      }
    else {
      auto str = string_frm(item->text[0]);
      if(str.empty() && &hItem!=ctrlInput && saveSlotId(hItem)!=size_t(-1))
        str = "---";
      if(&hItem!=ctrlInput)
        textBuf = std::string_view(str); else
        textBuf = string_frm(str,"_");
      }

    fnt.drawText(p,
                 x,y+fnt.pixelSize(),
                 szX, std::max(szY,fnt.pixelSize()),
                 textBuf,
                 txtAlign);
    }
  }

void GameMenu::drawSlider(Painter& p, Item& it, int x, int y, int sw, int sh) {
  float k = float(sh/2)/float(slider->h());
  int w = int(float(slider->w())*k);
  int h = int(float(slider->h())*k);
  p.setBrush(*slider);

  auto& sec = it.handle->on_chg_set_option_section;
  auto& opt = it.handle->on_chg_set_option;
  if(sec.empty() || opt.empty())
    return;

  float v  = Gothic::settingsGetF(sec, opt);
  if(it.name=="ANDROID_SCALE") {
    v=(std::clamp(v,50.f,100.f)-50.f)/50.f;
    getTextFont(it).drawText(p,x+sw+8,y+getTextFont(it).pixelSize(),string_frm(Gothic::settingsGetI(sec,opt),"%"));
    p.setBrush(*slider);
    }
  int   dx = int(float(sw-w)*std::max(0.f,std::min(v,1.f)));
  p.drawRect(x+dx,y+(sh-h)/2,w,h,
             0,0,p.brush().w(),p.brush().h());
  }

void GameMenu::drawQuestList(Painter& p, Item& it, int x, int y, int w, int h,
                             const QuestLog& log, QuestStat st) {
  int     itY       = y;
  int32_t listId    = -1;
  int32_t listLen   = numQuests(&log,st);
  int32_t listBegin = it.scroll;
  int32_t listEnd   = listLen;

  const float scale = Gothic::interfaceScale(this);
  for(size_t i=0; i<log.questCount(); i++) {
    auto& quest = log.quest(log.questCount()-i-1);
    if(!isCompatible(quest,st))
      continue;
    ++listId;
    if(listId<listBegin)
      continue;

    auto ft = Resources::FontType::Normal;
    if(listId==it.value && selectedItem()==&it)
      ft = Resources::FontType::Hi;

    auto& fnt = Resources::font(it.handle->fontname,ft,scale);
    auto sz  = fnt.textSize(w,quest.name);
    if(itY+sz.h>h+y) {
      listEnd = listId;
      break;
      }

    fnt.drawText(p,x,itY+int(fnt.pixelSize()),w,sz.h,quest.name,Tempest::AlignLeft);
    itY+=sz.h;
    }

  if(listBegin>it.value) {
    it.scroll = std::max(it.scroll-1,0);
    update();
    }
  if(listEnd<=it.value) {
    it.scroll = std::min(it.scroll+1,listLen);
    update();
    }

  if(listEnd<listLen && down!=nullptr) {
    p.setBrush(*down);
    p.drawRect(x+w-down->w(),y+h-down->h(),
               down->w(),down->h());
    }
  if(listBegin>0 && up!=nullptr) {
    p.setBrush(*up);
    p.drawRect(x+w-up->w(),y,
               up->w(),up->h());
    }
  }

void GameMenu::resizeEvent(SizeEvent &) {
  onTick();
  }

bool GameMenu::closeNestedView() {
  if(pendingDelete!=nullptr) {
    pendingDelete=nullptr;
    }
  else if(journalContent!=nullptr) {
    journalContent->visible=journalContentWasVisible;
    journalContent=nullptr;
    curItem=uint32_t(journalList-hItems);
    }
  else if(journalList!=nullptr) {
    journalList=nullptr;
    curItem=journalCategory;
    }
  else {
    return false;
    }
  update();
  return true;
  }

void GameMenu::showQuest() {
  auto* log=Gothic::inst().questLog();
  auto* content=selectedContentItem(journalList);
  if(log==nullptr || content==nullptr) return;
  const auto status=toStatus(journalList->handle->user_string[0]);
  int32_t selected=0;
  for(size_t i=log->questCount();i>0;--i) {
    const auto& quest=log->quest(i-1);
    if(!isCompatible(quest,status)) continue;
    if(selected++!=journalList->value) continue;
    std::string text;
    for(size_t entry=0;entry<quest.entry.size();++entry) {
      if(entry>0) text+="\n---\n";
      text+=quest.entry[entry];
      }
    journalContent=content;
    journalContentWasVisible=content->visible;
    content->visible=true;
    content->scroll=0;
    content->handle->text[0]=std::move(text);
    curItem=uint32_t(content-hItems);
    Feedback::play(Feedback::Effect::Confirm);
    update();
    return;
    }
  }

void GameMenu::journalInput(KeyCodec::Action key) {
  // Stay in the regular menu input path so touch and gamepad can reach every journal level.
  if(key==KeyCodec::Escape) {
    closeNestedView();
    return;
    }
  if(key==KeyCodec::ActionGeneric && journalContent==nullptr) {
    showQuest();
    return;
    }
  if(key!=KeyCodec::Forward && key!=KeyCodec::Back) return;
  const int32_t dy=key==KeyCodec::Forward ? -1 : 1;
  if(journalContent!=nullptr) {
    journalContent->scroll=std::max(0,journalContent->scroll+dy);
    }
  else {
    const int32_t count=numQuests(Gothic::inst().questLog(),toStatus(journalList->handle->user_string[0]));
    journalList->value=std::clamp(journalList->value+dy,0,std::max(0,count-1));
    }
  update();
  }

bool GameMenu::adjustsHorizontally() {
  if(journalList!=nullptr || pendingDelete!=nullptr) return false;
  auto sel=selectedItem();
  if(sel==nullptr) return false;
  return isHorSelectable(sel->handle) || canAdjustValue();
  }

bool GameMenu::canAdjustValue() {
  if(journalList!=nullptr || pendingDelete!=nullptr || ctrlInput!=nullptr) return false;
  auto first=selectedItem();
  auto item=first;
  // Gothic can select a text label whose EFFECTS chain contains the actual slider.
  for(int count=0; item!=nullptr && count<zenkit::IMenu::item_count; ++count) {
    const auto& handle=item->handle;
    if(!isEnabled(handle) || isHidden(handle)) return false;
    if((handle->type==zenkit::MenuItemType::SLIDER || handle->type==zenkit::MenuItemType::CHOICEBOX) &&
       !handle->on_chg_set_option.empty() && !handle->on_chg_set_option_section.empty()) return true;
    if(!(handle->flags & zenkit::MenuItemFlag::EFFECTS)) return false;
    item=selectedNextItem(item);
    if(item==first) break;
    }
  return false;
  }

void GameMenu::adjustValue(int steps) {
  if(steps==0 || !canAdjustValue()) return;
  exec(*selectedItem(),steps,steps>0 ? KeyCodec::Right : KeyCodec::Left);
  update();
  }

void GameMenu::onKeyboard(KeyCodec::Action key) {
  if(journalList!=nullptr) {
    journalInput(key);
    return;
    }
  if(pendingDelete!=nullptr) {
    if(key==KeyCodec::Escape) {
      pendingDelete=nullptr;
      update();
      }
    else if(key==KeyCodec::ActionGeneric && Gothic::inst().checkLoading()==Gothic::LoadState::Idle) {
      std::error_code error;
      if(SaveSlot::remove(".",saveSlotId(*pendingDelete),error)) {
        Log::i("Deleted save slot ",saveSlotId(*pendingDelete));
        pendingDelete->savHdr = SaveGameHeader();
        pendingDelete->savPriview = Pixmap();
        pendingDelete->handle->text[0] = "---";
        pendingDelete=nullptr;
        updateSavThumb(*selectedItem());
        }
      else {
        Log::e("Cannot delete save: ",error.message());
        deleteError="Could not delete save. Cancel and try again.";
        }
      update();
      }
    return;
    }
  auto sel = selectedItem();
  if(sel!=nullptr && isHorSelectable(sel->handle) && !canAdjustValue()) {
    if(key==KeyCodec::Left)
      key = KeyCodec::Forward;
    else if(key==KeyCodec::Right)
      key = KeyCodec::Back;
    }

  if(key==KeyCodec::Forward || key==KeyCodec::Back) {
    Gothic::inst().emitGlobalSound(Gothic::inst().loadSoundFx("MENU_BROWSE"));
    const int dy = (key==KeyCodec::Forward) ? -1 : +1;
    setSelection(int(curItem)+dy, dy);
    if(auto s = selectedItem())
      updateSavThumb(*s);
    update();
    }

  if((key==KeyCodec::Left || key==KeyCodec::Right) && sel!=nullptr) {
    const int dx = (key==KeyCodec::Left) ? -1 : +1;
    exec(*sel,dx,key);
    }

  if(key==KeyCodec::ActionGeneric && sel!=nullptr) {
    Gothic::inst().emitGlobalSound(Gothic::inst().loadSoundFx("MENU_SELECT"));
    exec(*sel,0,key);
    }
  if(key==KeyCodec::K_Del && sel!=nullptr) {
    if(saveSlotId(*sel)!=size_t(-1)) {
      requestDeleteSave("Enter: delete    Esc: cancel");
      return;
      }
    Gothic::inst().emitGlobalSound(Gothic::inst().loadSoundFx("MENU_SELECT"));
    exec(*sel,0,key);
    }
  }

bool GameMenu::canRequestDeleteSave() const {
  // This is queried by touch input every frame; check the file only when deletion is requested.
  return journalList==nullptr && pendingDelete==nullptr && ctrlInput==nullptr && curItem<zenkit::IMenu::item_count &&
         saveSlotId(hItems[curItem])!=size_t(-1) && Gothic::inst().checkLoading()==Gothic::LoadState::Idle;
  }

void GameMenu::requestDeleteSave(std::string_view hint) {
  if(!canRequestDeleteSave()) return;
  auto sel=selectedItem();
  std::error_code error;
  const auto status=std::filesystem::symlink_status(SaveSlot::path(".",saveSlotId(*sel)),error);
  if(error || !std::filesystem::is_regular_file(status))
    return;
  pendingDelete=sel;
  deleteName=sel->handle->text[0];
  deleteHint=hint;
  deleteError.clear();
  update();
  }

void GameMenu::onTick() {
  update();

  const float scale = Gothic::interfaceScale(this);
  const float fx    = 640.0f * scale;
  const float fy    = 480.0f * scale;

  const float wx = float(owner.w());
  const float wy = float(owner.h());

  Size size = {0, 0};
  if(menu->flags & zenkit::MenuFlag::DONT_SCALE_DIMENSION) {
    size = {int(float(menu->dim_x)*fx/scriptDiv),int(float(menu->dim_y)*fy/scriptDiv)};
    } else {
    size = {int(float(menu->dim_x)*wx/scriptDiv),int(float(menu->dim_y)*wy/scriptDiv)};
    }
  resize(size);

  if(menu->flags & zenkit::MenuFlag::ALIGN_CENTER) {
    setPosition((owner.w()-w())/2, (owner.h()-h())/2);
    } else {
    setPosition(int(float(menu->pos_x)/scriptDiv*fx), int(float(menu->pos_y)/scriptDiv*fy));
    }
  }

void GameMenu::processMusicTheme() {
  if(auto theme = Gothic::musicDef()[menu->music_theme])
    GameMusic::inst().setMusic(*theme,GameMusic::mkTags(GameMusic::Std,GameMusic::Day));
  }

GameMenu::Item *GameMenu::selectedItem() {
  if(curItem<zenkit::IMenu::item_count)
    return &hItems[curItem];
  return nullptr;
  }

GameMenu::Item* GameMenu::selectedNextItem(Item *it) {
  uint32_t cur=curItem+1;
  for(uint32_t i=0;i<zenkit::IMenu::item_count;++i)
    if(&hItems[i]==it) {
      cur=i+1;
      break;
      }

  for(int i=0;i<zenkit::IMenu::item_count;++i,cur++) {
    cur%=zenkit::IMenu::item_count;

    auto& it=hItems[cur].handle;
    if(isEnabled(it))
      return &hItems[cur];
    }
  return nullptr;
  }

GameMenu::Item* GameMenu::selectedContentItem(Item *it) {
  uint32_t cur=curItem+1;
  for(uint32_t i=0;i<zenkit::IMenu::item_count;++i)
    if(&hItems[i]==it) {
      cur=i+1;
      break;
      }

  for(int i=0;i<zenkit::IMenu::item_count;++i,cur++) {
    cur%=zenkit::IMenu::item_count;

    auto& it=hItems[cur].handle;
    if(isEnabled(it) && it->type==zenkit::MenuItemType::TEXT)
      return &hItems[cur];
    }
  return nullptr;
  }

void GameMenu::setSelection(int desired, int seek) {
  uint32_t cur=uint32_t(desired);
  for(int i=0; i<zenkit::IMenu::item_count; ++i,cur+=uint32_t(seek)) {
    cur%=zenkit::IMenu::item_count;

    auto& it=hItems[cur].handle;
    if(isSelectable(it) && isEnabled(it)){
      if(curItem!=cur && curItem!=uint32_t(-1))
        Feedback::play(Feedback::Effect::Navigate);
      curItem=cur;
      for(size_t i=0;i<zenkit::IMenuItem::select_action_count;++i)
        if(it->on_sel_action[i]==int(zenkit::MenuItemSelectAction::EXECUTE_COMMANDS))
          execCommands(it->on_sel_action_s[i],false,KeyCodec::Idle);
      return;
      }
    }
  curItem=uint32_t(-1);
  }

void GameMenu::getText(const Item& it, std::vector<char> &out) {
  if(out.size()==0)
    out.resize(1);
  out[0]='\0';

  const auto& src = it.handle->text[0];
  if(it.handle->type==zenkit::MenuItemType::TEXT) {
    size_t size = src.size();
    out.resize(size+1);
    std::memcpy(out.data(),src.c_str(),size+1);
    return;
    }

  if(it.handle->type==zenkit::MenuItemType::CHOICEBOX) {
    strEnum(src,it.value,out);
    return;
    }
  }

bool GameMenu::selectionIncludes(const Item& item) {
  auto* first=selectedItem();
  if(first==nullptr) return false;
  if(item.name.starts_with("ANDROID_") && item.handle->user_string[0]==first->name)
    return true;
  // Original menus select a label and link its slider through an EFFECTS chain.
  auto* current=first;
  for(size_t count=0;current!=nullptr && count<zenkit::IMenu::item_count;++count) {
    if(current==&item) return true;
    if(!(current->handle->flags & zenkit::MenuItemFlag::EFFECTS)) return false;
    current=selectedNextItem(current);
    if(current==first) break;
    }
  return false;
  }

const GthFont& GameMenu::getTextFont(const GameMenu::Item &it) {
  const float scale = Gothic::interfaceScale(this);
  GthFont ret;
  if(!isEnabled(it.handle))
    return Resources::font(it.handle->fontname, Resources::FontType::Disabled, scale);
  if(selectionIncludes(it))
    return Resources::font(it.handle->fontname, Resources::FontType::Hi, scale);
  return Resources::font(it.handle->fontname, Resources::FontType::Normal, scale);
  }

bool GameMenu::isSelectable(const std::shared_ptr<zenkit::IMenuItem>& item) {
  return item != nullptr && (item->flags & zenkit::MenuItemFlag::SELECTABLE) && !isHidden(item);
  }

bool GameMenu::isHorSelectable(const std::shared_ptr<zenkit::IMenuItem>& item) {
  return item != nullptr && (item->flags & zenkit::MenuItemFlag::HOR_SELECTABLE) && !isHidden(item);
  }

bool GameMenu::isEnabled(const std::shared_ptr<zenkit::IMenuItem>& item) {
  if(item==nullptr)
    return false;
  if((item->flags & zenkit::MenuItemFlag::ONLY_INGAME) && !Gothic::inst().isInGameAndAlive())
    return false;
  if((item->flags & zenkit::MenuItemFlag::ONLY_OUTGAME) && Gothic::inst().isInGameAndAlive())
    return false;
  return true;
  }

bool GameMenu::isHidden(const std::shared_ptr<zenkit::IMenuItem>& item) {
  if(item==nullptr)
    return false;
  if(item->hide_if_option_set.empty())
    return false;

  auto opt = Gothic::inst().settingsGetI(item->hide_if_option_section_set,
                                         item->hide_if_option_set);
  return opt==item->hide_on_value;
  }

void GameMenu::exec(Item &p, int slideDx, KeyCodec::Action hint) {
  Feedback::play(slideDx==0 ? Feedback::Effect::Confirm : Feedback::Effect::Navigate);
  auto* it = &p;
  while(it!=nullptr) {
    if(it==&p)
      execSingle(*it,slideDx,hint); else
      execChgOption(*it,slideDx);
    if(it->handle->flags & zenkit::MenuItemFlag::EFFECTS) {
      auto next=selectedNextItem(it);
      if(next!=&p)
        it=next;
      } else {
      it=nullptr;
      }
    }

  if(closeFlag)
    owner.closeAll();
  else if(exitFlag)
    owner.popMenu();
  }

void GameMenu::execSingle(Item &it, int slideDx, KeyCodec::Action hint) {
  auto& item          = it.handle;
  auto& onSelAction   = item->on_sel_action;
  auto& onSelAction_S = item->on_sel_action_s;
  auto& onEventAction = item->on_event_action;

#if defined(GOTHIC2VR_OPENXR)
  // VR has no keyboard inside this modal loop. Save immediately through the
  // native slot path and let exec() close the menu after the callback returns.
  if(item->type==zenkit::MenuItemType::INPUT && slideDx==0 && saveSlotId(it)!=size_t(-1)) {
    for(const auto& command:onSelAction_S)if(command=="SAVEGAME_SAVE") {
      item->text[0]="Save "+std::to_string(saveSlotId(it)+1);
      execSaveGame(it);closeFlag=true;return;
    }
  }
#endif
  if(item->type==zenkit::MenuItemType::INPUT && slideDx==0) {
    ctrlInput = &it;
    if(item->on_chg_set_option.empty()) {
      SavNameDialog dlg{*this,item->text[0]};
      if(it.savHdr.version==0)
        dlg.text = "";
      dlg.beginTextInput();
      dlg.resize(owner.size());
      dlg.exec();
      ctrlInput = nullptr;
      if(!dlg.accepted)
        return;
      }
    else if(hint==KeyCodec::K_Del) {
      keyCodec.clear(item->on_chg_set_option_section, item->on_chg_set_option);
      updateItem(it);
      ctrlInput = nullptr;
      return; //HACK: there is a SEL_ACTION_BACK token in same item
      }
    else {
      KeyEditDialog dlg;
      dlg.resize(owner.size());
      dlg.exec();
      ctrlInput = nullptr;

      if(dlg.key==Event::K_ESCAPE && dlg.mkey==Event::ButtonNone)
        return;
      int32_t next = 0;
      if(dlg.key==Event::K_ESCAPE)
        next = KeyCodec::keyToCode(dlg.mkey); else
        next = KeyCodec::keyToCode(dlg.key);
      keyCodec.set(item->on_chg_set_option_section, item->on_chg_set_option, next);
      updateItem(it);
      return; //HACK: there is a SEL_ACTION_BACK token in same item
      }
    }

  for(size_t i=0; i<zenkit::IMenuItem::select_action_count; ++i) {
    auto action = zenkit::MenuItemSelectAction(onSelAction[i]);
    switch(action) {
      case zenkit::MenuItemSelectAction::UNKNOWN:
        break;
      case zenkit::MenuItemSelectAction::BACK:
        Gothic::inst().emitGlobalSound(Gothic::inst().loadSoundFx("MENU_ESC"));
        exitFlag = true;
        break;
      case zenkit::MenuItemSelectAction::START_MENU:
        if(vm->find_symbol_by_name(onSelAction_S[i]) != nullptr)
          owner.pushMenu(new GameMenu(owner,keyCodec,*vm,onSelAction_S[i],keyClose()));
        break;
      case zenkit::MenuItemSelectAction::START_ITEM:
        break;
      case zenkit::MenuItemSelectAction::CLOSE:
        Gothic::inst().emitGlobalSound(Gothic::inst().loadSoundFx("MENU_ESC"));

        if(onSelAction_S[i]=="NEW_GAME") {
          Gothic::inst().onStartGame(Gothic::inst().defaultWorld());
          }
        else if(onSelAction_S[i]=="LEAVE_GAME") {
          Log::i("Exiting, by item action (`LEAVE_GAME`)");
          Tempest::SystemApi::exit();
          }
        else if(onSelAction_S[i]=="SAVEGAME_SAVE") {
          execSaveGame(it);
          }
        else if(onSelAction_S[i]=="SAVEGAME_LOAD"){
          if (!execLoadGame(it)){
            break;
          }
        }
        closeFlag = true;
        break;
      case zenkit::MenuItemSelectAction::CON_COMMANDS:
        // TODO: inline marvin commands
        break;
      case zenkit::MenuItemSelectAction::PLAY_SOUND:
        Gothic::inst().emitGlobalSound(Gothic::inst().loadSoundFx(onSelAction_S[i]));
        break;
      case zenkit::MenuItemSelectAction::EXECUTE_COMMANDS:
        execCommands(onSelAction_S[i],true,hint);
        break;
      }
    }

  if(onEventAction[int(zenkit::MenuItemSelectEvent::EXECUTE)]>0){
    auto* sym = vm->find_symbol_by_index(uint32_t(onEventAction[int(zenkit::MenuItemSelectEvent::EXECUTE)]));
    if (sym != nullptr)
      vm->call_function(sym);
    }

  execChgOption(it,slideDx);
  }

void GameMenu::execChgOption(Item &item, int slideDx) {
  auto& sec = item.handle->on_chg_set_option_section;
  auto& opt = item.handle->on_chg_set_option;
  if(sec.empty() || opt.empty())
    return;

  if(item.name=="ANDROID_SCALE") {
    if(slideDx!=0) Gothic::settingsSetI(sec,opt,std::clamp(Gothic::settingsGetI(sec,opt)+slideDx*5,50,100));
    return;
    }
  if(item.name=="ANDROID_SHADOWS") {
    constexpr int values[]={512,1024,1536,2048};
    updateItem(item);
    item.value=std::clamp(item.value+slideDx,0,3);
    Gothic::settingsSetI(sec,opt,values[item.value]);
    return;
    }
  if((item.name=="ANDROID_FOG" || item.name=="ANDROID_SSAO") && slideDx==0) {
    Gothic::settingsSetI(sec,opt,!Gothic::settingsGetI(sec,opt));
    return;
    }

  if(item.handle->type==zenkit::MenuItemType::SLIDER && slideDx!=0) {
    updateItem(item);
    float v = Gothic::settingsGetF(sec, opt);
    v  = std::max(0.f,std::min(v+float(slideDx)*0.03f,1.f));
    Gothic::settingsSetF(sec, opt, v);
    }
  if(item.handle->type==zenkit::MenuItemType::CHOICEBOX) {
    updateItem(item);
    const int cnt = int(strEnumSize(item.handle->text[0]));
    if(slideDx==0 && cnt==2)
      slideDx = 1; // QoL: on/off toggle

    item.value += slideDx; // next value
    if(cnt>0)
      item.value = std::clamp(item.value,0,cnt-1);  else
      item.value = 0;
    Gothic::settingsSetI(sec, opt, item.value);
    }
  }

void GameMenu::execSaveGame(const GameMenu::Item& item) {
  const size_t id = saveSlotId(item);
  if(id==size_t(-1))
    return;

  string_frm fname("save_slot_",int(id),".sav");
  Gothic::inst().save(fname,item.handle->text[0]);
  }

GameMenu::Item* GameMenu::selectedSaveItem() {
  if(pendingDelete!=nullptr || ctrlInput!=nullptr || !Gothic::inst().isInGameAndAlive() ||
     Gothic::inst().checkLoading()!=Gothic::LoadState::Idle) return nullptr;
  auto item=selectedItem();
  if(item==nullptr || !isEnabled(item->handle) || saveSlotId(*item)==size_t(-1)) return nullptr;
  for(const auto& action:item->handle->on_sel_action_s)
    if(action=="SAVEGAME_SAVE") return item;
  return nullptr;
  }

bool GameMenu::overwriteSelectedSave() {
  auto item=selectedSaveItem();
  if(item==nullptr || item->savHdr.version==0 ||
     !FileUtil::exists(TextCodec::toUtf16(SaveSlot::path(".",saveSlotId(*item)).string()))) return false;
  item->handle->text[0]=item->savHdr.name;
  execSaveGame(*item);
  owner.closeAll();
  return true;
  }

void GameMenu::renameSelectedSave() {
  if(selectedSaveItem()!=nullptr) onKeyboard(KeyCodec::ActionGeneric);
  }

bool GameMenu::execLoadGame(const GameMenu::Item &item) {
  const size_t id = saveSlotId(item);
  if(id==size_t(-1))
    return false;

  string_frm fname("save_slot_",int(id),".sav");
  if(!FileUtil::exists(TextCodec::toUtf16(fname.c_str())))
    return false;
  Gothic::inst().load(fname);
  return true;
  }

void GameMenu::execCommands(std::string str, bool isClick, KeyCodec::Action hint) {
  if(str.find("EFFECTS ")==0) {
    // menu log
    const char* arg0 = str.data()+std::strlen("EFFECTS ");
    for(uint32_t id=0; id<zenkit::IMenu::item_count; ++id) {
      auto& i = hItems[id];
      if(i.handle != nullptr && i.handle->type==zenkit::MenuItemType::LISTBOX) {
        i.visible = (i.name==arg0);
        if(i.visible && isClick) {
          const int32_t count=numQuests(Gothic::inst().questLog(),toStatus(i.handle->user_string[0]));
          if(count==0) continue;
          journalCategory=curItem;
          journalList=&i;
          i.value=std::clamp(i.value,0,count-1);
          curItem=id;
          update();
          }
        }
      }
    }

  if(!isClick)
    return;
  if(str.find("RUN ")==0) {
    const char* what = str.data()+4;
    while(*what==' ')
      ++what;
    for(auto& i:hItems)
      if(i.name==what)
        execSingle(i,0,hint);
    }
  if(str=="SETDEFAULT") {
    setDefaultKeys("KEYSDEFAULT0");
    }
  if(str=="SETALTERNATIVE") {
    setDefaultKeys("KEYSDEFAULT1");
    }
  }

void GameMenu::updateItem(GameMenu::Item &item) {
  auto& it   = item.handle;
  item.value = Gothic::settingsGetI(it->on_chg_set_option_section, it->on_chg_set_option);
  if(item.name=="ANDROID_SHADOWS") {
    const int size=item.value;
    item.value=size==512 ? 0 : (size==1024 ? 1 : (size==1536 ? 2 : 3));
    }
  updateSavTitle(item);
  }

void GameMenu::updateSavTitle(GameMenu::Item& sel) {
  if(sel.handle->on_sel_action_s[0]!="SAVEGAME_LOAD" &&
     sel.handle->on_sel_action_s[1]!="SAVEGAME_SAVE")
    return;
  const size_t id = saveSlotId(sel);
  if(id==size_t(-1))
    return;

  char fname[64]={};
  std::snprintf(fname,sizeof(fname)-1,"save_slot_%d.sav",int(id));

  if(!FileUtil::exists(TextCodec::toUtf16(fname))) {
    sel.handle->text[0] = "---";
    return;
    }

  SaveGameHeader hdr;
  try {
    RFile     fin(fname);
    Serialize reader(fin);
    reader.setEntry("header");
    reader.read(hdr);
    if(id!=0 || sel.handle->text[0].empty())
      sel.handle->text[0] = hdr.name;
    sel.savHdr = std::move(hdr);

    if(reader.setEntry("priview.png"))
      reader.read(sel.savPriview); // legacy
    else if(reader.setEntry("preview.png"))
      reader.read(sel.savPriview);
    else if(reader.setEntry("preview.jpg"))
      reader.read(sel.savPriview);
    }
  catch(std::bad_alloc&) {
    return;
    }
  catch(std::system_error& e) {
    Log::d(e.what());
    return;
    }
  catch(std::runtime_error&) {
    return;
    }
  }

void GameMenu::updateSavThumb(GameMenu::Item &sel) {
  if(sel.handle->on_sel_action_s[0]!="SAVEGAME_LOAD" &&
     sel.handle->on_sel_action_s[1]!="SAVEGAME_SAVE")
    return;

  if(!implUpdateSavThumb(sel)) {
    set("MENUITEM_LOADSAVE_THUMBPIC",static_cast<Texture2d*>(nullptr));
    set("MENUITEM_LOADSAVE_LEVELNAME_VALUE","");
    set("MENUITEM_LOADSAVE_DATETIME_VALUE", "");
    set("MENUITEM_LOADSAVE_GAMETIME_VALUE", "");
    set("MENUITEM_LOADSAVE_PLAYTIME_VALUE", "");
    }
  }

void GameMenu::updateVideo() {
  set("MENUITEM_VID_DEVICE_CHOICE",     Resources::renderer());
  set("MENUITEM_VID_RESOLUTION_CHOICE", "full|upscale(75%)|upscale(half)");
  }

void GameMenu::setDefaultKeys(std::string_view preset) {
  keyCodec.setDefaultKeys(preset);
  }

bool GameMenu::implUpdateSavThumb(GameMenu::Item& sel) {
  const size_t id = saveSlotId(sel);
  if(id==size_t(-1))
    return false;

  char fname[64]={};
  std::snprintf(fname,sizeof(fname)-1,"save_slot_%d.sav",int(id));

  if(!FileUtil::exists(TextCodec::toUtf16(fname)))
    return false;

  const SaveGameHeader& hdr = sel.savHdr;
  char form[64]={};
  Resources::device().waitIdle();
  savThumb = Resources::loadTexturePm(sel.savPriview);

  set("MENUITEM_LOADSAVE_THUMBPIC",       &savThumb);
  set("MENUITEM_LOADSAVE_LEVELNAME_VALUE",hdr.world);

  std::snprintf(form,sizeof(form),"%d.%d.%d - %d:%02d",int(hdr.pcTime.tm_mday),int(hdr.pcTime.tm_mon+1),int(1900+hdr.pcTime.tm_year), int(hdr.pcTime.tm_hour),int(hdr.pcTime.tm_min));
  set("MENUITEM_LOADSAVE_DATETIME_VALUE", form);

  std::snprintf(form,sizeof(form),"%d - %d:%02d",int(hdr.wrldTime.day()),int(hdr.wrldTime.hour()),int(hdr.wrldTime.minute()));
  set("MENUITEM_LOADSAVE_GAMETIME_VALUE", form);

  auto mins = hdr.playTime/(60*1000);
  std::snprintf(form,sizeof(form),"%dh %dmin",int(mins/60),int(mins%60));
  set("MENUITEM_LOADSAVE_PLAYTIME_VALUE", form);
  return true;
  }

size_t GameMenu::saveSlotId(const GameMenu::Item &sel) {
  const char* prefix[2] = {"MENUITEM_LOAD_SLOT", "MENUITEM_SAVE_SLOT"};
  for(auto p:prefix) {
    if(sel.name.find(p)==0){
      size_t off=std::strlen(p);
      size_t id=0;
      for(size_t i=off;i<sel.name.size();++i){
        if('0'<=sel.name[i] && sel.name[i]<='9') {
          id=id*10+size_t(sel.name[i]-'0');
          } else {
          return size_t(-1);
          }
        }
      return id;
      }
    }
  return size_t(-1);
  }

std::string_view GameMenu::strEnum(std::string_view en, int id, std::vector<char> &out) {
  int num=0;

  size_t i=0;
  for(size_t r=0; r<en.size(); ++r)
    if(en[r]=='#'){
      i=r+1;
      break;
      }

  for(;i<en.size();++i){
    size_t b=i;
    for(size_t r=i; r<en.size(); ++r,++i)
      if(en[r]=='|')
        break;

    if(id==num) {
      size_t sz=i-b;
      out.resize(sz+1);
      std::memcpy(&out[0],&en[b],sz);
      out[sz]='\0';
      return out.data();
      }
    ++num;
    }

  for(size_t i=0; i<en.size(); ++i){
    if(en[i]=='#' || en[i]=='|'){
      out.resize(i+1);
      std::memcpy(&out[0],en.data(),i);
      out[i]='\0';
      return out.data();
      }
    }

  return "";
  }

size_t GameMenu::strEnumSize(std::string_view en) {
  if(en.empty())
    return 0;
  size_t cnt = 0;
  for(size_t i=0; i<en.size(); ++i) {
    if(en[i]=='#' || en[i]=='|') {
      cnt += en[i]=='|' ? 2 : 1;
      i++;
      for(;i<en.size();++i) {
        if(en[i]=='|')
          cnt++;
        }
      }
    }
  return cnt;
  }

void GameMenu::set(std::string_view item, const Texture2d *value) {
  for(auto& i:hItems)
    if(i.name==item) {
      i.img = value;
      return;
      }
  }

void GameMenu::set(std::string_view item, const uint32_t value) {
  set(item,string_frm(value));
  }

void GameMenu::set(std::string_view item, const int32_t value) {
  set(item,string_frm(value));
  }

void GameMenu::set(std::string_view item, const int32_t value, const int32_t max) {
  set(item,string_frm(value,"/",max));
  }

void GameMenu::set(std::string_view item, std::string_view value) {
  for(auto& i:hItems)
    if(i.name==item) {
      i.handle->text[0] = value;
      return;
      }
  }

void GameMenu::updateValues() {
  gtime time;
  if(auto w = Gothic::inst().world())
    time = w->time();

  set("MENU_ITEM_PLAYERGUILD","Debugger");
  set("MENU_ITEM_LEVEL","0");
  set("MENUITEM_AUDIO_PROVIDER_CHOICE", "OpenGothic|GothicKit (Experimental)");
  for(auto& i:hItems) {
    if(i.name=="MENU_ITEM_CONTENT_VIEWER") {
      i.visible=false;
      }
    if(i.name=="MENU_ITEM_DAY") {
      i.handle->text[0] = std::to_string(time.day());
      }
    if(i.name=="MENU_ITEM_TIME") {
      char form[64]={};
      std::snprintf(form,sizeof(form),"%d:%02d",int(time.hour()),int(time.minute()));
      i.handle->text[0] = form;
      }
    }

  if(auto s = selectedItem())
    updateSavThumb(*s);

  updateVideo();
  }

void GameMenu::setPlayer(const Npc &pl) {
  auto world = Gothic::inst().world();
  if(world==nullptr)
    return;

  auto& sc    = world->script();
  auto* gilds = sc.findSymbol("TXT_GUILDS");
  auto* tal   = sc.findSymbol("TXT_TALENTS");
  auto* talV  = sc.findSymbol("TXT_TALENTS_SKILLS");

  if(gilds==nullptr || tal==nullptr || talV==nullptr) {
    return;
    }

  set("MENU_ITEM_PLAYERGUILD", gilds->get_string(uint16_t(pl.guild())));

  set("MENU_ITEM_LEVEL",       pl.level());
  set("MENU_ITEM_EXP",         pl.experience());
  set("MENU_ITEM_LEVEL_NEXT",  pl.experienceNext());
  set("MENU_ITEM_LEARN",       pl.learningPoints());

  set("MENU_ITEM_ATTRIBUTE_1", pl.attribute(ATR_STRENGTH));
  set("MENU_ITEM_ATTRIBUTE_2", pl.attribute(ATR_DEXTERITY));
  set("MENU_ITEM_ATTRIBUTE_3", pl.attribute(ATR_MANA),      pl.attribute(ATR_MANAMAX));
  set("MENU_ITEM_ATTRIBUTE_4", pl.attribute(ATR_HITPOINTS), pl.attribute(ATR_HITPOINTSMAX));

  set("MENU_ITEM_ARMOR_11",    pl.protection(PROT_EDGE));
  set("MENU_ITEM_ARMOR_1",     pl.protection(PROT_BLUNT));
  set("MENU_ITEM_ARMOR_2",     pl.protection(PROT_POINT)); // not sure about it
  set("MENU_ITEM_ARMOR_3",     pl.protection(PROT_FIRE));
  set("MENU_ITEM_ARMOR_4",     pl.protection(PROT_MAGIC));

  const bool g2        = Gothic::inst().version().game==2;
  const int  talentMax = g2 ? TALENT_MAX_G2 : TALENT_MAX_G1;
  for(uint16_t i=0; i<talentMax; ++i) {
    auto& str = tal->get_string(i);
    if(str.empty())
      continue;

    const int sk  = pl.talentSkill(Talent(i));
    const int val = g2 ? pl.hitChance(Talent(i)) : pl.talentValue(Talent(i));

    set(string_frm("MENU_ITEM_TALENT_",i,"_TITLE"), str);
    set(string_frm("MENU_ITEM_TALENT_",i,"_SKILL"), strEnum(talV->get_string(i),sk,textBuf));
    set(string_frm("MENU_ITEM_TALENT_",i),          string_frm(val,"%"));
    }
  }
