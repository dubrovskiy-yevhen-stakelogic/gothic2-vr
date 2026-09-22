#pragma once

// Opt-in OpenXR probe (-vrinfo). Creates an XrInstance and queries the system,
// nothing else: no session, no Vulkan device and no window. It runs from the
// top of main() before log.txt exists, so it reports on stdout (and mirrors the
// same text into vrinfo.txt) and exits the process itself: 0 on success, 3 on
// any OpenXR failure or when the binary was built without OpenXR support.
class VrInfo final {
  public:
    static void preflight(int argc, const char** argv);
  };
