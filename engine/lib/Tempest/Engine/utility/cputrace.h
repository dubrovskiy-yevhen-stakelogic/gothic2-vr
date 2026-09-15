#pragma once

namespace Tempest {

// Scoped, opt-in CPU markers for platform tracing tools.
// Scopes must be nested and destroyed on the thread that created them.
class CpuTrace final {
  public:
    explicit CpuTrace(const char* name);
    ~CpuTrace();
    CpuTrace(const CpuTrace&) = delete;
    CpuTrace& operator=(const CpuTrace&) = delete;

    static void setEnabled(bool enabled);

  private:
    bool active = false;
  };

}
