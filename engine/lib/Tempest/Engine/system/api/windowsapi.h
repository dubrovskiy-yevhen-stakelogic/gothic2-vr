#pragma once

#include "system/systemapi.h"

namespace Tempest {

class WindowsApi final : SystemApi {
  public:
    /// A VR session owns the render loop: keep dispatching render while the
    /// window is iconic, so minimizing the desktop mirror cannot stall the
    /// headset. Mirrors AndroidApi::setVrRenderLoop.
    static void setVrRenderLoop(bool enabled);

  private:
    WindowsApi();

    Window*  implCreateWindow(Tempest::Window* owner, uint32_t width, uint32_t height, ShowMode sm);
    Window*  implCreateWindow(Tempest::Window *owner, uint32_t width, uint32_t height) override;
    Window*  implCreateWindow(Tempest::Window *owner, ShowMode sm) override;
    void     implDestroyWindow(Window* w) override;
    void     implExit() override;

    Rect     implWindowClientRect(SystemApi::Window *w) override;
    bool     implSetAsFullscreen(SystemApi::Window *w, bool fullScreen) override;
    bool     implIsFullscreen(SystemApi::Window *w) override;

    void     implSetWindowTitle(SystemApi::Window *w, const char* utf8) override;
    float    implUiScale(SystemApi::Window *w) override;

    void     implSetCursorPosition(SystemApi::Window *w, int x, int y) override;
    void     implShowCursor(SystemApi::Window *w, CursorShape show) override;

    bool     implIsRunning() override;
    int      implExec(AppCallBack& cb) override;
    void     implProcessEvents(AppCallBack& cb) override;

    static long long windowProc(void* hWnd, uint32_t msg, const unsigned long long wParam, const long long lParam);
    static void handleKeyEvent(Tempest::Window* cb, uint32_t msg, const unsigned long long wParam, const long long lParam);

  friend class SystemApi;
  };

}
