#include "cputrace.h"
#include <iostream>
#include <string>
#include <vector>

static bool recording = false;
static std::vector<std::string> events;
bool ATrace_isEnabled() { return recording; }
void ATrace_beginSection(const char* name) { events.emplace_back(name); }
void ATrace_endSection() { events.emplace_back("end"); }

int main() {
  using Tempest::CpuTrace;
  recording = true;
  { CpuTrace off("disabled by default"); }
  if(!events.empty()) return 1;
  CpuTrace::setEnabled(true);
  recording = false;
  { CpuTrace off("no platform capture"); }
  if(!events.empty()) return 2;
  recording = true;
  try {
    CpuTrace outer("outer");
    CpuTrace inner("inner");
    CpuTrace::setEnabled(false);
    throw 42;
    }
  catch(int) {}
  if(events!=std::vector<std::string>{"outer","inner","end","end"}) return 3;
  { CpuTrace off("disabled again"); }
  if(events.size()!=4) return 4;
  std::cout << "Disabled tracing, nested scopes, exception unwinding and mid-scope disable passed\n";
  }
