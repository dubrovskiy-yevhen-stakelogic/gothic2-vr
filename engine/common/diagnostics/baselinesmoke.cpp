#include "baselinesmoke.h"

#include "commandline.h"
#include "gothic.h"
#include "game/gamescript.h"
#include "game/inventory.h"
#include "game/questlog.h"
#include "world/world.h"
#include "world/objects/npc.h"
#include "world/objects/item.h"

#include <Tempest/SystemApi>
#include <Tempest/Log>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>

namespace {
using Buffer = rapidjson::StringBuffer;
using Writer = rapidjson::Writer<Buffer>;
void string(Writer& w, const std::string_view value) {
  w.String(value.data(), rapidjson::SizeType(value.size()));
}
}

void BaselineSmoke::preflight(int argc, const char** argv) {
  bool requested = false;
  std::filesystem::path retail;
  std::filesystem::path fixture;
  for(int i=1; i<argc; ++i) {
    if(std::string_view(argv[i])=="-baseline-smoke") requested = true;
    if(std::string_view(argv[i])=="-baseline-gameplay") {
      requested = true;
      if(++i>=argc) throw std::runtime_error("-baseline-gameplay requires a saved fixture");
      fixture = argv[i];
      }
    if(std::string_view(argv[i])=="-g" && i+1<argc) retail = argv[++i];
    }
  if(!requested) return;
#if defined(__ANDROID__) || defined(__IOS__)
  throw std::runtime_error("baseline smoke currently supports desktop only");
#endif
  if(retail.empty()) throw std::runtime_error("explicit -g is required");
  const auto source = std::filesystem::canonical(retail);
  const auto output = std::filesystem::canonical(std::filesystem::current_path());
  if(!fixture.empty()) {
    fixture = std::filesystem::canonical(fixture);
    if(!std::filesystem::is_regular_file(fixture) || std::filesystem::file_size(fixture)==0)
      throw std::runtime_error("gameplay fixture must be a nonempty save file");
    if(std::filesystem::equivalent(fixture.parent_path(),output))
      throw std::runtime_error("gameplay fixture must remain outside the writable test directory");
    }
  // Walk canonical ancestors, using filesystem equivalence for Windows case
  // and junction aliases. Never create even a log file inside purchased data.
  for(auto p=output; !p.empty(); p=p.parent_path()) {
    if(std::filesystem::equivalent(p, source))
      throw std::runtime_error("working directory must be outside the retail installation");
    if(p==p.parent_path()) break;
    }
  for(const auto& entry: std::filesystem::directory_iterator(output)) {
    auto name = entry.path().filename().string();
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if(std::filesystem::is_symlink(entry.symlink_status()) ||
       (entry.is_regular_file() && std::filesystem::hard_link_count(entry.path())>1))
      throw std::runtime_error("smoke working directory must not contain file links");
    const bool save = name.rfind("save_slot_",0)==0 && name.size()>=4 && name.substr(name.size()-4)==".sav";
    if(name.rfind("baseline-smoke",0)==0 || save)
      throw std::runtime_error("smoke needs a fresh directory without previous diagnostic files or saves");
    }
}

BaselineSmoke::BaselineSmoke() : enabled(CommandLine::inst().baselineSmoke()), started(std::chrono::steady_clock::now()), lastReport(started) {
  if(enabled && !CommandLine::inst().baselineFixture().empty()) gameplay = std::make_unique<GameplayProbe>();
  if(enabled) report("running", "waiting for a rendered, saveable new game");
}

void BaselineSmoke::poll() {
  if(!enabled || stage==Done) return;
  const auto load = Gothic::inst().checkLoading();
  if(load==Gothic::LoadState::FailedLoad || load==Gothic::LoadState::FailedSave)
    finish(false, "engine reported load/save failure");
  else if(std::chrono::steady_clock::now()-started>std::chrono::seconds(900))
    finish(false, "900 second timeout; inspect window, dialogue and log.txt");
  else if(std::chrono::steady_clock::now()-lastReport>std::chrono::seconds(5)) {
    lastReport = std::chrono::steady_clock::now();
    report("running", "awaiting next integration step; inspect live state fields");
    }
}

std::string BaselineSmoke::snapshot() const {
  auto& g = Gothic::inst();
  auto& world = *g.world();
  auto& hero = *g.player();
  Buffer buffer;
  Writer w(buffer);
  w.StartObject();
  w.Key("world"); string(w, world.name());
  w.Key("npc_count"); w.Uint(world.npcCount());
  w.Key("script_symbols"); w.Uint64(world.script().symbolsCount());
  w.Key("hero_instance"); w.Uint(hero.instanceSymbol());
  w.Key("attributes"); w.StartArray();
  for(unsigned i=0; i<8; ++i) w.Int(hero.attribute(Attribute(i)));
  w.EndArray();
  // Aggregate stacks because serialization may reorder inventory entries.
  std::map<size_t,size_t> items;
  for(auto it=hero.inventory().iterator(Inventory::T_Inventory); it.isValid(); ++it)
    items[it->clsId()] += it.count();
  w.Key("inventory"); w.StartArray();
  for(auto [id,count]:items) {
    w.StartObject(); w.Key("instance"); w.Uint64(id); w.Key("count"); w.Uint64(count); w.EndObject();
    }
  w.EndArray();
  const auto& log = world.script().questLog();
  w.Key("quests"); w.StartArray();
  for(size_t i=0; i<log.questCount(); ++i) {
    const auto& quest = log.quest(i);
    w.StartObject();
    w.Key("name"); string(w,quest.name);
    w.Key("section"); w.Uint(unsigned(quest.section));
    w.Key("status"); w.Uint(unsigned(quest.status));
    w.Key("entries"); w.StartArray();
    for(const auto& entry:quest.entry) string(w,entry);
    w.EndArray(); w.EndObject();
    }
  w.EndArray();
  if(gameplay) {
    const auto target = gameplay->targetSnapshot();
    w.Key("combat_target"); w.RawValue(target.data(),target.size(),rapidjson::kObjectType);
    }
  w.EndObject();
  return {buffer.GetString(),buffer.GetSize()};
}

void BaselineSmoke::afterFrame(bool uiReady, bool videoActive, uint64_t dt, uint64_t phraseMs, size_t choices,
                              PlayerControl& player, const std::function<void()>& screenshot) {
  if(!enabled || stage==Done) return;
  try {
    auto& g = Gothic::inst();
    lastUiReady = uiReady;
    lastVideoActive = videoActive;
    logicMilliseconds += dt;
    lastPhraseMs = phraseMs;
    lastChoices = choices;
    if(g.checkLoading()!=Gothic::LoadState::Idle || !g.world() || !g.player()) return;
    if(videoActive) return;
    ++renderedWorldFrames;
    if(!previewCaptured && renderedWorldFrames>=120) {
      screenshot();
      previewCaptured = true;
      }
    if(stage==AwaitWorld) {
      if(!uiReady || !g.isInGameAndAlive() || g.world()->currentCs()!=nullptr) return;
      if(++worldFrames<120) return;
      if(g.world()->npcCount()==0 || g.world()->script().symbolsCount()==0)
        throw std::runtime_error("empty world or script VM");
      if(gameplay) {
        if(!gameplay->step(player)) return;
        } else {
      auto* gold = g.world()->script().goldId();
      if(!gold) throw std::runtime_error("gold instance missing from game scripts");
      const auto count = g.player()->itemCount(gold->index());
      g.player()->addItem(gold->index(),1);
      if(g.player()->itemCount(gold->index())!=count+1)
        throw std::runtime_error("inventory fixture add failed");
        }
      before = snapshot();
      screenshot();
      stage = Saving;
      report("running", gameplay ? "movement, world pickup and native duel completed; saving" : "world rendered; one gold fixture added; saving");
      g.save("baseline-smoke.sav", "Gothic II VR isolated baseline");
      if(g.checkLoading()==Gothic::LoadState::Idle)
        throw std::runtime_error("save request was rejected");
      }
    else if(stage==Saving) {
      if(!std::filesystem::exists("baseline-smoke.sav") || std::filesystem::file_size("baseline-smoke.sav")==0)
        throw std::runtime_error("save output missing or empty");
      stage = Loading;
      report("running", "save completed; reloading through normal game loader");
      g.load("baseline-smoke.sav");
      if(g.checkLoading()==Gothic::LoadState::Idle)
        throw std::runtime_error("load request was rejected");
      }
    else if(stage==Loading) {
      after = snapshot();
      if(before!=after) throw std::runtime_error("state differs after save/load; compare before and after");
      finish(true, gameplay ? "PlayerControl movement and pickup; native damage in both directions; defeated enemy and sampled RPG state survived save/load" : "world rendered; inventory fixture and sampled RPG state survived normal save/load");
      }
    }
  catch(const std::exception& e) { finish(false,e.what()); }
}

void BaselineSmoke::report(const char* status, const std::string& reason) {
  Buffer buffer;
  Writer w(buffer);
  w.StartObject();
  w.Key("schema"); w.Uint(1);
  w.Key("status"); w.String(status);
  w.Key("reason"); string(w,reason);
  w.Key("world_frames_before_save"); w.Uint(worldFrames);
  w.Key("rendered_world_frames"); w.Uint(renderedWorldFrames);
  w.Key("ui_ready"); w.Bool(lastUiReady);
  w.Key("video_active"); w.Bool(lastVideoActive);
  // Sampled only on submitted frames, not total simulated time; world_ticks is authoritative.
  w.Key("submitted_frame_dt_sum_ms"); w.Uint64(logicMilliseconds);
  w.Key("dialog_phrase_ms"); w.Uint64(lastPhraseMs);
  w.Key("dialog_choices"); w.Uint64(lastChoices);
  w.Key("world_ticks");
  if(Gothic::inst().checkLoading()==Gothic::LoadState::Idle && Gothic::inst().world()) w.Uint64(Gothic::inst().world()->tickCount()); else w.Null();
  w.Key("load_state"); w.Uint(unsigned(Gothic::inst().checkLoading()));
  w.Key("paused"); w.Bool(Gothic::inst().isPause());
  w.Key("in_dialogue");
  if(Gothic::inst().isNpcInDialogFn) w.Bool(Gothic::inst().isInDialog()); else w.Null();
  w.Key("elapsed_seconds"); w.Double(std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count());
  w.Key("before"); if(before.empty()) w.Null(); else w.RawValue(before.data(),before.size(),rapidjson::kObjectType);
  if(gameplay) {
    const auto evidence = gameplay->evidence();
    w.Key("gameplay"); w.RawValue(evidence.data(),evidence.size(),rapidjson::kObjectType);
    }
  w.Key("after"); if(after.empty()) w.Null(); else w.RawValue(after.data(),after.size(),rapidjson::kObjectType);
  w.Key("scope"); w.String("Windows flat integration only; no VR, headset, physical combat or campaign acceptance");
  w.EndObject();
  std::ofstream file("baseline-smoke.json",std::ios::binary|std::ios::trunc);
  file.write(buffer.GetString(),std::streamsize(buffer.GetSize()));
  file << '\n';
  if(!file) throw std::runtime_error("cannot write baseline report");
  Tempest::Log::i("[baseline] ",status,": ",reason);
}

void BaselineSmoke::finish(bool success, const std::string& reason) {
  stage = Done;
  passed = success;
  report(success ? "passed" : "failed",reason);
  Tempest::SystemApi::exit();
}
