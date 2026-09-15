#include "gamepadbindings.h"
#include "movementresponse.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <sstream>
#include <unordered_map>

namespace {
using Action = GamepadBindings::Action;
using Context = GamepadBindings::Context;
constexpr const char* actionNames[] = {
  "Interact", "Back", "Jump", "Journal", "Inventory", "Pause", "Sneak", "Walk", "LockTarget",
  "DrawSheathe", "EquipmentWheel", "Map", "HealthPotion", "ManaPotion", "CharacterStats",
  "FirstPerson", "LookBehind", "QuickSave", "QuickLoad", "AttackForward", "AttackLeft",
  "AttackRight", "Block", "Finish", "Accept", "Up", "Down", "Left", "Right",
  "PreviousPage", "NextPage", "Cancel", "LeftPanel", "RightPanel", "TakeStack", "Drop",
  "Spell3", "Spell4", "Spell5", "Spell6", "Spell7", "Spell8", "Spell9", "Spell10", "DeleteSave",
  "AdjustLeft", "AdjustRight", "SystemWheel",
  "QuickUp", "QuickDown", "QuickLeft", "QuickRight",
  "WheelUp", "WheelDown", "WheelLeft", "WheelRight",
  "AssignUp", "AssignDown", "AssignLeft", "AssignRight", "RenameSave", "Run"
  };
static_assert(std::size(actionNames)==size_t(Action::Count));
constexpr const char* actionLabels[] = {
  "Use / pick up", "Back", "Jump", "Journal", "Inventory", "Menu", "Sneak", "Walk", "Target lock",
  "Draw / sheathe", "Equipment wheel", "Map", "Health potion", "Mana potion", "Character stats",
  "First person", "Look behind", "Quicksave", "Quickload", "Attack", "Attack left",
  "Attack right", "Block", "Finishing blow", "Accept / use", "Up", "Down", "Left", "Right",
  "Previous page", "Next page", "Cancel", "Left inventory pane", "Right inventory pane", "Transfer stack", "Drop item",
  "Spell slot 3", "Spell slot 4", "Spell slot 5", "Spell slot 6", "Spell slot 7", "Spell slot 8", "Spell slot 9", "Spell slot 10",
  "Delete save", "Decrease slider", "Increase slider", "System wheel",
  "Quick slot up", "Quick slot down", "Quick slot left", "Quick slot right",
  "Up slot wheel", "Down slot wheel", "Left slot wheel", "Right slot wheel",
  "Assign up (hold modifier first)", "Assign down (hold modifier first)",
  "Assign left (hold modifier first)", "Assign right (hold modifier first)", "Rename save", "Run (hold)"
  };
static_assert(std::size(actionLabels)==size_t(Action::Count));
std::string trim(std::string s) {
  const auto first = s.find_first_not_of(" \t\r\n");
  if(first==std::string::npos)
    return {};
  return s.substr(first,s.find_last_not_of(" \t\r\n")-first+1);
  }
bool repeatable(Action a) {
  return a==Action::Up || a==Action::Down || a==Action::Left || a==Action::Right ||
         a==Action::AdjustLeft || a==Action::AdjustRight;
  }
}

std::string GamepadBindings::defaults() {
  return R"ini(; OpenGothic controller bindings. Restart after editing; updates preserve this file.
; Xbox names: L3/R3 mean pressing the sticks. None unbinds an action.
; Chords use +, alternatives use commas, and Hold: delays an action until held.
[Controller]
Enabled=1
ExplorationModifier=LB
HoldMs=400
RepeatDelayMs=350
RepeatMs=150
CameraAssist=1

[Gameplay]
Interact=A
Back=B
Jump=X
Journal=Y,LB+DpadRight
Inventory=View
Pause=Menu
SystemWheel=Hold:Menu
Sneak=L3
Walk=None
LockTarget=R3
DrawSheathe=RB
EquipmentWheel=None
Map=None
HealthPotion=None
ManaPotion=None
QuickUp=DpadUp
QuickDown=DpadDown
QuickLeft=DpadLeft
QuickRight=DpadRight
WheelUp=Hold:DpadUp
WheelDown=Hold:DpadDown
WheelLeft=Hold:DpadLeft
WheelRight=Hold:DpadRight
CharacterStats=LB+DpadLeft
FirstPerson=LB+DpadUp
LookBehind=LB+DpadDown
QuickSave=LB+Menu
QuickLoad=LB+View

[ClassicMelee]
AttackForward=Y
AttackLeft=X
AttackRight=B
Block=A,LT
Finish=None

[ModernMelee]
AttackForward=RT
Block=LT
Finish=None

[Ranged]
AttackForward=RT

[UI]
Accept=A
Back=B,View,Menu
DeleteSave=X
RenameSave=Y
Up=DpadUp,LeftStickUp
Down=DpadDown,LeftStickDown
Left=DpadLeft,LeftStickLeft
Right=DpadRight,LeftStickRight
AdjustLeft=RightStickLeft
AdjustRight=RightStickRight

[EquipmentWheel]
PreviousPage=LB
NextPage=RB
Cancel=B

[Inventory]
AssignUp=A+DpadUp
AssignDown=A+DpadDown
AssignLeft=A+DpadLeft
AssignRight=A+DpadRight
LeftPanel=LB
RightPanel=RB
TakeStack=Y
Drop=X
Spell3=LT+DpadUp
Spell4=LT+DpadRight
Spell5=LT+DpadDown
Spell6=LT+DpadLeft
Spell7=RT+DpadUp
Spell8=RT+DpadRight
Spell9=RT+DpadDown
Spell10=RT+DpadLeft

[Interaction]
Accept=A
Back=B
Up=DpadUp,LeftStickUp
Down=DpadDown,LeftStickDown
Left=DpadLeft,LeftStickLeft
Right=DpadRight,LeftStickRight

[Axes]
MovementStick=LeftStick
CameraStick=RightStick
StickDeadZone=0.20
MovementDeadZone=0.28
TouchMovementDeadZone=0.15
MovementExponent=1.5
MovementTurnSpeed=180
TouchTurnSpeed=180
WalkThreshold=0.60
TouchWalkThreshold=0.35
WalkHysteresis=0.04
TriggerPressThreshold=0.55
TriggerReleaseThreshold=0.40

[Combat]
; Face the focused NPC at the start of an unlocked melee attack.
; Distance is in Gothic world units (centimeters); this does not increase weapon reach.
MeleeAssist=1
MeleeAssistMaxAngle=90
MeleeAssistMaxDistance=300
; 0 uses the scripts' ordinary monster warning range for melee NPC focus and target lock.
; 1 restores original focus distance; 2 through 4 multiply that original distance.
; This does not change weapon reach, item pickup, or ranged/spell targeting.
MeleeFocusRangeScale=0

[TargetLock]
; Target eligibility and acquisition ranges come from Gothic's focus rules.
SwitchThreshold=0.65
SwitchResetThreshold=0.25
SwitchCooldownMs=250
CameraSmoothingSeconds=0.20
)ini";
  }

uint32_t GamepadBindings::button(std::string_view name) {
  constexpr const char* names[] = {"A","B","X","Y","LB","RB","L3","R3","Menu","View",
    "DpadUp","DpadDown","DpadLeft","DpadRight","LT","RT",
    "LeftStickUp","LeftStickDown","LeftStickLeft","LeftStickRight",
    "RightStickUp","RightStickDown","RightStickLeft","RightStickRight"};
  if(name=="Start") name="Menu";
  if(name=="Select") name="View";
  for(size_t i=0;i<std::size(names);++i)
    if(name==names[i])
      return 1u<<i;
  return 0;
  }

GamepadBindings::GamepadBindings() {
  std::istringstream input(defaults());
  load(input);
  }

void GamepadBindings::setVrMapping(const std::array<Action,6>& actions) {
  if(vrMapping && vrActions==actions)return;
  vrMapping=true;vrActions=actions;reset(previous);
  }

std::vector<std::string> GamepadBindings::load(std::istream& input) {
  using Values = std::unordered_map<std::string,std::string>;
  std::unordered_map<std::string,Values> values;
  std::vector<std::string> errors;
  std::string section,line;
  while(std::getline(input,line)) {
    line=trim(line.substr(0,line.find(';')));
    if(line.empty()) continue;
    if(line.front()=='[' && line.back()==']') {
      section=line.substr(1,line.size()-2);
      continue;
      }
    const auto pos=line.find('=');
    if(pos==std::string::npos || section.empty()) {
      errors.push_back("Malformed INI line: "+line);
      continue;
      }
    auto key=trim(line.substr(0,pos));
    if(values[section].contains(key))
      errors.push_back(section+"/"+key+": duplicate key; last value used");
    values[section][key]=trim(line.substr(pos+1));
    }

  constexpr const char* names[]={"Gameplay","ClassicMelee","ModernMelee","Ranged","UI","Inventory","EquipmentWheel","Interaction"};
  if(sections.empty())
    for(auto name:names) sections.push_back({name,{}});
  for(auto& dst:sections) {
    auto backup=dst.bindings;
    bool valid=true;
    for(auto& [key,value]:values[dst.name]) {
      auto name=std::find(std::begin(actionNames),std::end(actionNames),key);
      if(name==std::end(actionNames)) {
        errors.push_back(dst.name+"/"+key+": unknown action"); valid=false; continue;
        }
      const auto action=Action(name-std::begin(actionNames));
      std::erase_if(dst.bindings,[&](const Binding& b){return b.action==action;});
      if(value=="None") continue;
      std::istringstream choices(value);
      std::string choice;
      while(std::getline(choices,choice,',')) {
        Binding b; b.action=action; b.text=trim(choice);
        choice=b.text;
        if(choice.starts_with("Hold:")) { b.hold=true; choice=choice.substr(5); }
        std::istringstream chord(choice);
        std::string token;
        bool ok=true;
        while(std::getline(chord,token,'+')) {
          auto bit=button(trim(token));
          if(bit==0 || (b.mask&bit)!=0) ok=false;
          b.mask|=bit; b.trigger=bit;
          }
        if(!ok || b.mask==0 || choice.ends_with('+')) {
          errors.push_back(dst.name+"/"+key+": invalid binding "+b.text); valid=false; continue;
          }
        dst.bindings.push_back(b);
        }
      if(value.empty()) { errors.push_back(dst.name+"/"+key+": use None to unbind"); valid=false; }
      }
    for(size_t i=0;i<dst.bindings.size();++i)
      for(size_t j=i+1;j<dst.bindings.size();++j) {
        auto& a=dst.bindings[i]; auto& b=dst.bindings[j];
        if(a.mask==b.mask && a.hold==b.hold) {
          errors.push_back(dst.name+": conflicting bindings "+a.text+" / "+b.text); valid=false;
          }
        }
    if(!valid) dst.bindings=std::move(backup);
    }

  auto number=[&](const char* sec,const char* key,float fallback,float low,float high) {
    auto& v=values[sec];
    if(!v.contains(key)) return fallback;
    try {
      size_t end=0; float n=std::stof(v[key],&end);
      if(end==v[key].size() && std::isfinite(n) && n>=low && n<=high) return n;
      } catch(...) {}
    errors.push_back(std::string(sec)+"/"+key+": invalid numeric value");
    return fallback;
    };
  auto stick=[&](const char* sec,const char* key,bool fallback,const char* normal) {
    auto& v=values[sec];
    if(!v.contains(key)) return fallback;
    if(v[key]=="LeftStick" || v[key]=="RightStick") return v[key]!=normal;
    errors.push_back(std::string(sec)+"/"+key+": expected LeftStick or RightStick");
    return fallback;
    };
  options.enabled=number("Controller","Enabled",options.enabled?1.f:0.f,0,1)!=0;
  options.cameraAssist=number("Controller","CameraAssist",options.cameraAssist?1.f:0.f,0,1)!=0;
  options.holdMs=uint64_t(number("Controller","HoldMs",float(options.holdMs),150,2000));
  options.repeatDelayMs=uint64_t(number("Controller","RepeatDelayMs",float(options.repeatDelayMs),100,2000));
  options.repeatMs=uint64_t(number("Controller","RepeatMs",float(options.repeatMs),50,1000));
  if(values["Controller"].contains("ExplorationModifier")) {
    const auto bit=button(values["Controller"]["ExplorationModifier"]);
    if(bit!=0) options.explorationModifier=bit;
    else errors.push_back("Controller/ExplorationModifier: unknown button");
    }
  options.swapMovement=stick("Axes","MovementStick",options.swapMovement,"LeftStick");
  options.swapCamera=stick("Axes","CameraStick",options.swapCamera,"RightStick");
  options.deadZone=number("Axes","StickDeadZone",options.deadZone,0,0.8f);
  options.movementDeadZone=number("Axes","MovementDeadZone",options.movementDeadZone,0,0.8f);
  options.touchMovementDeadZone=number("Axes","TouchMovementDeadZone",options.touchMovementDeadZone,0,0.8f);
  options.movementExponent=number("Axes","MovementExponent",options.movementExponent,1,4);
  options.movementTurnSpeed=number("Axes","MovementTurnSpeed",options.movementTurnSpeed,45,720);
  options.touchTurnSpeed=number("Axes","TouchTurnSpeed",options.touchTurnSpeed,45,720);
  options.walkThreshold=number("Axes","WalkThreshold",options.walkThreshold,0.1f,1);
  options.touchWalkThreshold=number("Axes","TouchWalkThreshold",options.touchWalkThreshold,0.1f,1);
  options.walkHysteresis=number("Axes","WalkHysteresis",options.walkHysteresis,0,0.2f);
  options.triggerPress=number("Axes","TriggerPressThreshold",options.triggerPress,0.05f,1);
  options.triggerRelease=number("Axes","TriggerReleaseThreshold",options.triggerRelease,0,0.95f);
  if(options.triggerRelease>=options.triggerPress) {
    errors.push_back("Axes: trigger release must be below press threshold; restored defaults");
    options.triggerPress=0.55f; options.triggerRelease=0.40f;
    }
  options.switchThreshold=number("TargetLock","SwitchThreshold",options.switchThreshold,0.1f,1);
  options.switchReset=number("TargetLock","SwitchResetThreshold",options.switchReset,0,0.9f);
  if(options.switchReset>=options.switchThreshold) {
    errors.push_back("TargetLock: reset must be below switch threshold; restored defaults");
    options.switchThreshold=0.65f; options.switchReset=0.25f;
    }
  options.switchCooldownMs=uint64_t(number("TargetLock","SwitchCooldownMs",float(options.switchCooldownMs),0,2000));
  options.cameraSmoothing=number("TargetLock","CameraSmoothingSeconds",options.cameraSmoothing,0.01f,2);
  options.meleeAssist=number("Combat","MeleeAssist",options.meleeAssist?1.f:0.f,0,1)!=0;
  options.meleeAssistMaxAngle=number("Combat","MeleeAssistMaxAngle",options.meleeAssistMaxAngle,0,180);
  options.meleeAssistMaxDistance=number("Combat","MeleeAssistMaxDistance",options.meleeAssistMaxDistance,0,1000);
  options.meleeFocusRangeScale=number("Combat","MeleeFocusRangeScale",options.meleeFocusRangeScale,0,4);
  if(options.meleeFocusRangeScale>0.f && options.meleeFocusRangeScale<1.f) {
    errors.push_back("Combat: MeleeFocusRangeScale must be 0 or between 1 and 4; restored automatic range");
    options.meleeFocusRangeScale=0.f;
    }
  for(auto& [sec,v]:values) {
    std::string_view allowed;
    if(sec=="Controller") allowed="|Enabled|ExplorationModifier|HoldMs|RepeatDelayMs|RepeatMs|CameraAssist|";
    if(sec=="Axes") allowed="|MovementStick|CameraStick|StickDeadZone|MovementDeadZone|TouchMovementDeadZone|MovementExponent|MovementTurnSpeed|TouchTurnSpeed|WalkThreshold|TouchWalkThreshold|WalkHysteresis|TriggerPressThreshold|TriggerReleaseThreshold|";
    if(sec=="TargetLock") allowed="|SwitchThreshold|SwitchResetThreshold|SwitchCooldownMs|CameraSmoothingSeconds|";
    if(sec=="Combat") allowed="|MeleeAssist|MeleeAssistMaxAngle|MeleeAssistMaxDistance|MeleeFocusRangeScale|";
    if(!allowed.empty()) for(auto& [key,value]:v) {
      (void)value;
      if(allowed.find("|"+key+"|")==std::string_view::npos)
        errors.push_back(sec+"/"+key+": unknown option");
      }
    if(std::find(std::begin(names),std::end(names),sec)==std::end(names) && sec!="Controller" && sec!="Axes" && sec!="TargetLock" && sec!="Combat")
      errors.push_back(sec+": unknown section");
    }
  reset();
  return errors;
  }

std::vector<GamepadBindings::Binding> GamepadBindings::bindings(Context context) const {
  std::vector<Binding> out;
  auto append=[&](std::string_view name) {
    for(auto& section:sections) if(section.name==name) {
      for(auto& b:section.bindings) {
        std::erase_if(out,[&](const Binding& a){return a.mask==b.mask && a.hold==b.hold;});
        out.push_back(b);
        }
      }
    };
  if(context==Context::EquipmentWheel) { append("EquipmentWheel"); return out; }
  if(context==Context::Interaction) { append("Interaction"); return out; }
  if(context==Context::UI || context==Context::Inventory) {
    append("UI");
    if(context==Context::Inventory) append("Inventory");
    return out;
    }
  append("Gameplay");
  if(context==Context::ClassicMelee) append("ClassicMelee");
  if(context==Context::ModernMelee) append("ModernMelee");
  if(context==Context::Ranged) append("Ranged");
  if(vrMapping) {
    // Quest Y is reported as View by the established OpenXR input adapter.
    const char* keys[]={"A","B","X","View","L3","R3"};uint32_t reserved=0;
    for(auto key:keys)reserved|=button(key);
    std::erase_if(out,[&](const Binding& b){return (b.mask&reserved)!=0;});
    for(size_t i=0;i<vrActions.size();++i) if(vrActions[i]!=Action::Count) {
      Binding b;b.action=vrActions[i];b.mask=b.trigger=button(keys[i]);b.text=i==3?"Y":keys[i];out.push_back(b);
    }
  }
  return out;
  }

std::string GamepadBindings::hint(Action action,Context context) const {
  std::string out;
  for(auto& b:bindings(context)) if(b.action==action) {
    if(!out.empty()) out+=" / ";
    out+=b.text;
    }
  return out.empty()?"None":out;
  }

std::vector<GamepadBindings::Hint> GamepadBindings::hints(Context context) const {
  std::vector<Hint> result;
  for(const auto& b:bindings(context)) {
    auto found=std::find_if(result.begin(),result.end(),[&](const Hint& hint){ return hint.action==b.action; });
    if(found==result.end()) {
      auto group=HintGroup::Actions;
      const auto action=b.action;
      if(action>=Action::QuickUp && action<=Action::AssignRight)
        group=HintGroup::QuickSlots;
      else if(action>=Action::Spell3 && action<=Action::Spell10)
        group=HintGroup::Spells;
      else if((action>=Action::Up && action<=Action::Right) || action==Action::Jump || action==Action::Walk ||
              action==Action::Sneak || action==Action::FirstPerson || action==Action::LookBehind ||
              action==Action::AdjustLeft || action==Action::AdjustRight)
        group=HintGroup::Movement;
      else if(action==Action::Journal || action==Action::Inventory || action==Action::Pause || action==Action::SystemWheel ||
              action==Action::QuickSave || action==Action::QuickLoad || action==Action::CharacterStats ||
              action==Action::Map || action==Action::HealthPotion || action==Action::ManaPotion)
        group=HintGroup::Shortcuts;
      result.push_back({action,b.text,actionLabels[size_t(action)],group});
      }
    else
      found->keys+=" / "+b.text;
    }
  return result;
  }

void GamepadBindings::reset(uint32_t held) {
  presses={}; previous=held; blocked=held; initialized=false;
  automaticWalking=true;
  }

std::pair<float,float> GamepadBindings::movementAxis(float x,float y,bool touch) const {
  const float deadZone=touch ? options.touchMovementDeadZone : options.movementDeadZone;
  const float magnitude=std::sqrt(x*x+y*y);
  if(magnitude<=deadZone) return {0.f,0.f};
  const float normalized=(std::min(magnitude,1.f)-deadZone)/(1.f-deadZone);
  const float scale=std::pow(normalized,options.movementExponent)/magnitude;
  return {x*scale,y*scale};
  }

std::pair<float,float> GamepadBindings::touchMovementAxis(float x,float y) const {
  return turnMovementAxis(x,y,true);
  }

std::pair<float,float> GamepadBindings::turnMovementAxis(float x,float y,bool touch) const {
  // Shift gamepad's neutral forward/back line down by 15 degrees, to 8:30 and 3:30.
  if(!touch)
    y=std::clamp(y-std::abs(x)*0.267949192f,-1.f,1.f);
  // Filter each axis independently so a strong turn cannot amplify vertical input noise.
  return {movementAxis(x,0.f,touch).first,movementAxis(0.f,y,touch).second};
  }

std::pair<float,float> GamepadBindings::targetMovementAxis(float x,float y) {
  // Gothic chooses one movement animation, so ignore noise on the weaker axis.
  // The radial dead zone has already been applied equally to both components.
  if(std::abs(x)>=std::abs(y))
    return {x,0.f};
  return {0.f,y};
  }

bool GamepadBindings::automaticWalk(float x,float y,bool targetRelative,bool touch) {
  const float deadZone=touch ? options.touchMovementDeadZone : options.movementDeadZone;
  const float magnitude=std::min(1.f,std::hypot(x,y));
  // Switching walk modes during a sidestep selects a different, potentially uninterruptible animation.
  if(magnitude<=deadZone || (targetRelative && targetMovementAxis(x,y).second==0.f)) {
    automaticWalking=true;
    return false;
    }
  const float threshold=touch ? options.touchWalkThreshold : options.walkThreshold;
  automaticWalking=MovementResponse::walk(magnitude,deadZone,threshold,options.walkHysteresis,automaticWalking);
  return automaticWalking;
  }

std::vector<GamepadBindings::Event> GamepadBindings::update(uint32_t buttons,Context context,uint64_t now) {
  std::vector<Event> out;
  if(initialized && context!=lastContext) {
    for(auto& p:presses) if(p.active && !p.pending)
      out.push_back({p.binding.action,Phase::Cancel,p.binding.mask});
    presses={}; blocked|=previous;
    }
  lastContext=context; initialized=true;
  blocked&=buttons;
  auto available=bindings(context);
  for(size_t i=0;i<presses.size();++i) {
    const uint32_t bit=1u<<i;
    auto& p=presses[i];
    if(p.active && (buttons&p.binding.mask)!=p.binding.mask) {
      if(p.pending) {
        if(!p.binding.hold && (buttons&bit)==0 && (buttons&(p.binding.mask^bit))==(p.binding.mask^bit)) {
          out.push_back({p.binding.action,Phase::Press,p.binding.mask});
          out.push_back({p.binding.action,Phase::Release,p.binding.mask});
          }
        } else out.push_back({p.binding.action,Phase::Release,p.binding.mask});
      p={}; blocked|=buttons&bit;
      }
    if(p.active && p.pending && p.hold.mask!=0 && now-p.started>=options.holdMs) {
      p.binding=p.hold; p.pending=false;
      out.push_back({p.binding.action,Phase::Press,p.binding.mask});
      }
    if(p.active && !p.pending && repeatable(p.binding.action) && now>=p.repeat) {
      out.push_back({p.binding.action,Phase::Repeat,p.binding.mask});
      p.repeat=now+options.repeatMs;
      }
    if((buttons&bit)==0 || (previous&bit)!=0 || (blocked&bit)!=0) continue;
    const Binding* best=nullptr;
    const Binding* hold=nullptr;
    for(auto& b:available) {
      if(b.trigger!=bit || (buttons&b.mask)!=b.mask || ((b.mask^bit)&previous)!=(b.mask^bit)) continue;
      if(!best || std::popcount(b.mask)>std::popcount(best->mask) || (b.mask==best->mask && !b.hold)) best=&b;
      }
    if(!best) continue;
    // Equally specific chords with different modifiers are ambiguous.
    // Suppress them instead of depending on INI iteration order.
    bool ambiguous=false;
    for(auto& b:available) {
      if(b.trigger==bit && b.mask!=best->mask && std::popcount(b.mask)==std::popcount(best->mask) &&
         (buttons&b.mask)==b.mask && ((b.mask^bit)&previous)==(b.mask^bit)) ambiguous=true;
      }
    if(ambiguous) { blocked|=bit; continue; }
    for(auto& b:available) if(b.mask==best->mask && b.trigger==bit && b.hold) hold=&b;
    p.binding=*best; p.started=now; p.repeat=now+options.repeatDelayMs; p.active=true;
    // Inventory actions used as assignment modifiers execute on release, unless a chord consumes them.
    const bool assignmentModifier=context==Context::Inventory &&
      std::any_of(available.begin(),available.end(),[&](const Binding& b) {
        return b.action>=Action::AssignUp && b.action<=Action::AssignRight && (b.mask&bit)!=0 && b.trigger!=bit;
        });
    p.pending=hold!=nullptr || assignmentModifier;
    if(hold) p.hold=*hold;
    // A chord consumes a pending modifier tap, including if the modifier is released first.
    for(size_t m=0;m<presses.size();++m) {
      auto& modifier=presses[m];
      if(m!=i && modifier.active && modifier.pending && ((best->mask^bit)&(1u<<m))!=0) {
        modifier={};
        blocked|=1u<<m;
        }
      }
    if(!p.pending) out.push_back({p.binding.action,Phase::Press,p.binding.mask});
    }
  previous=buttons;
  return out;
  }
