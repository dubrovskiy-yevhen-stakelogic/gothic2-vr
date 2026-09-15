#include "cputrace.h"

#if defined(__ANDROID__)
#include <android/trace.h>
#include <atomic>

namespace {
std::atomic_bool traceEnabled{false};
}
#endif

using namespace Tempest;

CpuTrace::CpuTrace(const char* name) {
#if defined(__ANDROID__)
  active = traceEnabled.load(std::memory_order_relaxed) && ATrace_isEnabled();
  if(active)
    ATrace_beginSection(name);
#else
  (void)name;
#endif
  }

CpuTrace::~CpuTrace() {
#if defined(__ANDROID__)
  if(active)
    ATrace_endSection();
#endif
  }

void CpuTrace::setEnabled(bool enabled) {
#if defined(__ANDROID__)
  traceEnabled.store(enabled,std::memory_order_relaxed);
#else
  (void)enabled;
#endif
  }
