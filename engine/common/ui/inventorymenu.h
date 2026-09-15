#pragma once

#include <Tempest/Widget>
#include <Tempest/Texture2d>
#include <Tempest/Timer>

#include "graphics/inventoryrenderer.h"
#include "game/inventory.h"
#include "game/quickslots.h"

class Npc;
class Item;
class Inventory;
class Interactive;
class World;
class KeyCodec;

class InventoryMenu : public Tempest::Widget {
  public:
    InventoryMenu(const KeyCodec& key);
    ~InventoryMenu();

    enum class State:uint8_t {
      Closed=0,
      Equip,
      Chest,
      Trade,
      Ransack,
      LockPicking
      };

    enum class LootMode:uint8_t {
      Normal=0,
      Stack,
      Ten,
      Hundred
      };

    enum class DrawPass:uint8_t {
      Back,
      Front
      };

    enum class WheelKind:uint8_t {
      Equipment,
      Character,
      System
      };

    void  close();
    void  open(Npc& pl);
    void  trade(Npc& pl,Npc& tr);
    bool  ransack(Npc& pl,Npc& tr);
    void  open(Npc& pl,Interactive& chest);
    State isOpen() const;
    bool  isActive() const;
    void  onWorldChanged();

    void  tick(uint64_t dt);
    void  draw(Tempest::Encoder<Tempest::CommandBuffer>& cmd);
    void  paintNumOverlay(Tempest::PaintEvent& e);
    void  controllerAction(int action);
    void  openWheel(Npc& pl, WheelKind kind=WheelKind::Equipment);
    void  openQuickWheel(Npc& pl, size_t slot);
    bool  isWheelOpen() const { return wheelActive; }
    WheelKind currentWheelKind() const { return wheelKind; }
    void  wheelMove(float x, float y, uint64_t now);
    void  wheelPage(int direction);
    size_t wheelSelection() const;
    void  setWheelPageHint(std::string hint) { wheelPageHint=std::move(hint); }
    void  beginTouchWheel(Tempest::Point origin);
    void  touchWheelMove(Tempest::Point pos, uint64_t now);

    void  keyDownEvent  (Tempest::KeyEvent&   e) override;
    void  keyRepeatEvent(Tempest::KeyEvent&   e) override;
    void  keyUpEvent    (Tempest::KeyEvent&   e) override;

  protected:
    void  paintEvent     (Tempest::PaintEvent& e) override;

    void  mouseDownEvent (Tempest::MouseEvent& event) override;
    void  mouseUpEvent   (Tempest::MouseEvent& event) override;
    void  mouseWheelEvent(Tempest::MouseEvent& event) override;

  private:
    struct Page;
    struct InvPage;
    struct TradePage;
    struct RansackPage;

    struct PageLocal final {
      size_t                  sel    = 0;
      size_t                  scroll = 0;
      };

    const KeyCodec&           keycodec;

    const Tempest::Texture2d* tex =nullptr;
    const Tempest::Texture2d* slot=nullptr;
    const Tempest::Texture2d* selT=nullptr;
    const Tempest::Texture2d* selU=nullptr;

    State                     state      =State::Closed;
    Npc*                      player     =nullptr;
    Npc*                      trader     =nullptr;
    Interactive*              chest      =nullptr;

    std::unique_ptr<Page>     pageOth, pagePl;
    PageLocal                 pageLocal[2];

    uint8_t                   page       =0;
    Tempest::Timer            takeTimer;
    size_t                    takeCount  =0;
    LootMode                  lootMode   =LootMode::Normal;
    InventoryRenderer         renderer;

    size_t                    columsCount = 5;
    int32_t                   scrollDelta = 0;
    bool                      wheelActive = false;
    WheelKind                 wheelKind = WheelKind::Equipment;
    bool                      wheelTouch = false;
    Tempest::Point             wheelTouchOrigin;
    int                       wheelHoverPage = 0;
    uint64_t                  wheelHoverSince = 0;
    bool                      wheelPageArmed = true;
    bool                      wheelCentered = true;
    size_t                    wheelPageId = 0;
    int                       wheelSelected = -1;
    std::vector<size_t>        wheelItems;
    std::string               wheelPageHint;
    QuickSlots::Kind          wheelFilter = QuickSlots::Kind::Empty;
    size_t                    assignmentItem = size_t(-1);
    size_t                    assignmentCell = size_t(-1);
    int                       assignmentDirection = -1;
    uint64_t                  assignmentUntil = 0;
    void                      drawWheel(Tempest::Painter& p, DrawPass pass);
    size_t                    wheelPageSize() const;
    void                      selectWheelSector(int selected, uint64_t now);
    size_t                    wheelSectorCount() const;
    struct WheelLayout {
      Tempest::Point center;
      int cell;
      float radius;
      float outer;
      int footer;
      };
    WheelLayout               wheelLayout() const;

    size_t                    rowsCount() const;

    Tempest::Size             slotSize() const;
    int                       infoHeight() const;
    bool                      hasSideInfo() const;
    int                       headerTop() const;
    int                       gridTop() const;
    Tempest::Rect             infoRect() const;
    size_t                    pagesCount() const;

    const Page&               activePage();
    PageLocal&                activePageSel();

    void          processMove(Tempest::KeyEvent& e);
    void          moveLeft(bool usePage);
    void          moveRight(bool usePage);
    void          moveUp();
    void          moveDown();

    void          onItemAction(uint8_t slotHint);
    void          onTakeStuff();
    void          adjustScroll();
    void          drawAll   (Tempest::Painter& p, Npc& player, DrawPass pass);
    void          drawItems (Tempest::Painter& p, DrawPass pass, const Page &inv, const PageLocal &sel, int x, int y, int wcount, int hcount);
    void          drawSlot  (Tempest::Painter& p, DrawPass pass, const Inventory::Iterator& it,
                             const Page& page, const PageLocal &sel, int x, int y, size_t id);
    void          drawGold  (Tempest::Painter& p, Npc &player, int x, int y);
    void          drawHeader(Tempest::Painter& p, std::string_view title, int x, int y);
    void          drawInfo  (Tempest::Painter& p);
  };
