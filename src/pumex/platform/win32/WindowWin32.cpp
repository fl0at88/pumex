#include <pumex/platform/win32/WindowWin32.h>

#include <pumex/Viewer.h>
#include <pumex/Surface.h>
#include <pumex/utils/Log.h>

using namespace pumex;

std::unordered_map<HWND, WindowWin32*> WindowWin32::registeredWindows;

LRESULT CALLBACK WindowWin32Proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  WindowWin32* win = WindowWin32::getWindow(hwnd);
  if ( win != nullptr )
    return win->handleWin32Messages(msg, wParam, lParam);
  else
    return ::DefWindowProc(hwnd, msg, wParam, lParam);
}

const std::string windowClassName = "Pumex_Window_class_for_Win32";

WindowWin32::WindowWin32(const WindowTraits& windowTraits)
{
  for (uint32_t i = 0; i<256; ++i)
    keyState[i] = 0x0;

  // register window class
  WNDCLASSEX wc;
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc   = WindowWin32Proc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = ::GetModuleHandle(NULL);
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszMenuName  = NULL;
    wc.lpszClassName = windowClassName.c_str();
    wc.hIconSm       = LoadIcon(NULL, IDI_WINLOGO);

  if (::RegisterClassEx(&wc) == 0)
  {
    unsigned int lastError = ::GetLastError();
    CHECK_LOG_THROW(lastError != ERROR_CLASS_ALREADY_EXISTS, "failed RegisterClassEx : " << lastError);
  }

  std::vector<DISPLAY_DEVICE> displayDevices;
  for (unsigned int deviceNum = 0; ; ++deviceNum)
  {
    DISPLAY_DEVICE displayDevice;
    displayDevice.cb = sizeof(displayDevice);

    if (!::EnumDisplayDevices(NULL, deviceNum, &displayDevice, 0)) break;
    if (displayDevice.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) continue;
    if (!(displayDevice.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)) continue;

    displayDevices.push_back(displayDevice);
  }
  CHECK_LOG_THROW(windowTraits.screenNum >= static_cast<uint32_t>(displayDevices.size()), "screenNum out of range");

  DEVMODE deviceMode;
    deviceMode.dmSize = sizeof(deviceMode);
    deviceMode.dmDriverExtra = 0;
  CHECK_LOG_THROW(!::EnumDisplaySettings(displayDevices[windowTraits.screenNum].DeviceName, ENUM_CURRENT_SETTINGS, &deviceMode), "EnumDisplaySettings failed");

  RECT rect;
  unsigned int style = 0;
  unsigned int extendedStyle = 0;
  if (windowTraits.fullscreen)
  {
    rect.left     = deviceMode.dmPosition.x;
    rect.top      = deviceMode.dmPosition.y;
    rect.right    = rect.left + deviceMode.dmPelsWidth;
    rect.bottom   = rect.top + deviceMode.dmPelsHeight;
    style         = WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    extendedStyle = WS_EX_APPWINDOW;
  }
  else
  {
    rect.left     = deviceMode.dmPosition.x + windowTraits.x;
    rect.top      = deviceMode.dmPosition.y + windowTraits.y;
    rect.right    = rect.left + windowTraits.w;
    rect.bottom   = rect.top + windowTraits.h;
    style         = WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    extendedStyle = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;
  }
  CHECK_LOG_THROW(!::AdjustWindowRectEx(&rect, style, FALSE, extendedStyle), "AdjustWindowRectEx failed" );

  _hwnd = ::CreateWindowEx(extendedStyle, windowClassName.c_str(), windowTraits.windowName.c_str(), style,
    rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
    NULL, NULL, ::GetModuleHandle(NULL), NULL);
  CHECK_LOG_THROW(_hwnd == nullptr, "CreateWindowEx failed");

  registerWindow(_hwnd, this);
  GetClientRect(_hwnd,&rect);
  width = newWidth = rect.right - rect.left;
  height = newHeight = rect.bottom - rect.top;

  ShowWindow(_hwnd, SW_SHOW);
  SetForegroundWindow(_hwnd);
  SetFocus(_hwnd);
}

WindowWin32::~WindowWin32()
{
  if (_hwnd != nullptr)
  {
    ::DestroyWindow(_hwnd);
    unregisterWindow(_hwnd);
    _hwnd = nullptr;
  }
  if (registeredWindows.empty())
    ::UnregisterClass(windowClassName.c_str(), ::GetModuleHandle(NULL));
}


std::shared_ptr<pumex::Surface> WindowWin32::createSurface(std::shared_ptr<pumex::Viewer> v, std::shared_ptr<pumex::Device> device, const pumex::SurfaceTraits& surfaceTraits)
{
  VkSurfaceKHR vkSurface;
  VkWin32SurfaceCreateInfoKHR surfaceCreateInfo = {};
    surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surfaceCreateInfo.hinstance = ::GetModuleHandle(NULL);
    surfaceCreateInfo.hwnd = _hwnd;
  VK_CHECK_LOG_THROW(vkCreateWin32SurfaceKHR(v->getInstance(), &surfaceCreateInfo, nullptr, &vkSurface), "Could not create surface");

  std::shared_ptr<pumex::Surface> result = std::make_shared<pumex::Surface>(v, shared_from_this(), device, vkSurface, surfaceTraits);
  viewer = v;
  surface = result;
  swapChainResizable = true;
  return result;
}

bool WindowWin32::checkWindowMessages()
{
  MSG msg;
  while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
  {
    if (msg.message == WM_QUIT)
      return false;
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  return true;
}


LRESULT WindowWin32::handleWin32Messages(UINT msg, WPARAM wParam, LPARAM lParam)
{
  switch (msg)
  {
  case WM_CLOSE:
    viewer.lock()->setTerminate();
    break;
  case WM_DESTROY:
    ::PostQuitMessage(0);
    break;
  case WM_PAINT:
    ValidateRect(_hwnd, NULL);
    break;
  case WM_MOUSEMOVE:
  {
    float mx = GET_X_LPARAM(lParam);
    float my = GET_Y_LPARAM(lParam);
    normalizeMouseCoordinates(mx,my);
    pushMouseEvent(MouseEvent(MouseEvent::MOVE, MouseEvent::NONE, mx, my, pumex::HPClock::now()));
  }
  break;
  case WM_LBUTTONDOWN:
  case WM_MBUTTONDOWN:
  case WM_RBUTTONDOWN:
  {
    ::SetCapture(_hwnd);

    MouseEvent::Button button;
    if (msg == WM_LBUTTONDOWN)      button = MouseEvent::LEFT;
    else if (msg == WM_MBUTTONDOWN) button = MouseEvent::MIDDLE;
    else                            button = MouseEvent::RIGHT;
    pressedMouseButtons.insert(button);

    float mx = GET_X_LPARAM(lParam);
    float my = GET_Y_LPARAM(lParam);
    normalizeMouseCoordinates(mx, my);
    pushMouseEvent(MouseEvent(MouseEvent::KEY_PRESSED, button, mx, my, pumex::HPClock::now()));
  }
  break;
  case WM_LBUTTONUP:
  case WM_MBUTTONUP:
  case WM_RBUTTONUP:
  {
    MouseEvent::Button button;
    if (msg == WM_LBUTTONUP)      button = MouseEvent::LEFT;
    else if (msg == WM_MBUTTONUP) button = MouseEvent::MIDDLE;
    else                          button = MouseEvent::RIGHT;

    pressedMouseButtons.erase(button);
    if (pressedMouseButtons.empty())
      ::ReleaseCapture();

    float mx = GET_X_LPARAM(lParam);
    float my = GET_Y_LPARAM(lParam);
    normalizeMouseCoordinates(mx, my);
    pushMouseEvent(MouseEvent(MouseEvent::KEY_RELEASED, button, mx, my, pumex::HPClock::now()));
  }
  break;
  case WM_LBUTTONDBLCLK:
  case WM_MBUTTONDBLCLK:
  case WM_RBUTTONDBLCLK:
  {
    ::SetCapture(_hwnd);

    MouseEvent::Button button;
    if (msg == WM_LBUTTONDBLCLK)      button = MouseEvent::LEFT;
    else if (msg == WM_MBUTTONDBLCLK) button = MouseEvent::MIDDLE;
    else                              button = MouseEvent::RIGHT;

    pressedMouseButtons.insert(button);

    float mx = GET_X_LPARAM(lParam);
    float my = GET_Y_LPARAM(lParam);
    normalizeMouseCoordinates(mx, my);
    pushMouseEvent(MouseEvent(MouseEvent::KEY_DOUBLE_PRESSED, button, mx, my, pumex::HPClock::now()));
  }
  break;
  case WM_MOUSEWHEEL:
    break;
  case WM_MOVE:
  break;
  case WM_SIZE:
  {
    if (swapChainResizable && (wParam != SIZE_MINIMIZED))
    {
      newWidth = LOWORD(lParam);
      newHeight = HIWORD(lParam);
      auto surfaceSh = surface.lock();
      if (wParam == SIZE_MAXIMIZED)
      {
        sizeMaximized = true;
        surfaceSh->actions.addAction(std::bind(&pumex::Surface::resizeSurface, surfaceSh, newWidth, newHeight));
        width = newWidth;
        height = newHeight;
      }
      else if ( sizeMaximized && wParam == SIZE_RESTORED)
      { 
        sizeMaximized = false;
        surfaceSh->actions.addAction(std::bind(&pumex::Surface::resizeSurface, surfaceSh, newWidth, newHeight));
        width = newWidth;
        height = newHeight;
      }
    }
  }
  break;
  case WM_EXITSIZEMOVE:
  {
    if ((swapChainResizable) && ((width != newWidth) || (height != newHeight)))
    {
      auto surf = surface.lock();
      surf->actions.addAction(std::bind(&pumex::Surface::resizeSurface, surf, newWidth, newHeight));
      width = newWidth;
      height = newHeight;
    }
    break;
  }
  case WM_KEYDOWN:
  case WM_SYSKEYDOWN:
  {
    // temporary solution
    std::array<uint8_t, 256> newKeyState;
    ::GetKeyboardState(newKeyState.data());
    setKeyState(newKeyState);
  }
  break;
  case WM_KEYUP:
  case WM_SYSKEYUP:
  {
    // temporary solution
    std::array<uint8_t, 256> newKeyState;
    ::GetKeyboardState(newKeyState.data());
    setKeyState(newKeyState);
  }
  break;
  }
  return ::DefWindowProc(_hwnd, msg, wParam, lParam);
}

void WindowWin32::normalizeMouseCoordinates(float& x, float& y)
{
  RECT rect;
  GetClientRect(_hwnd, &rect);
  x = (x - rect.left) / (rect.right - rect.left);
  y = (y - rect.top) / (rect.bottom - rect.top);
}


void WindowWin32::registerWindow(HWND hwnd, pumex::WindowWin32* window)
{
  registeredWindows.insert({hwnd,window});
}

void WindowWin32::unregisterWindow(HWND hwnd)
{
  registeredWindows.erase(hwnd);
}

WindowWin32* WindowWin32::getWindow(HWND hwnd)
{
  auto it = registeredWindows.find(hwnd);
  if (it == registeredWindows.end())
    it = registeredWindows.find(nullptr);
  if (it == registeredWindows.end())
    return nullptr;
  return it->second;
}