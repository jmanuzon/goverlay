#include "overlay.h"
#include <filesystem>

namespace overlay
{

std::shared_ptr<OverlayMain> gOverlayMain;

Napi::Value start(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();

  gOverlayMain = std::make_shared<OverlayMain>();

  return env.Undefined();
}

Napi::Value stop(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();

  gOverlayMain = nullptr;

  return env.Undefined();
}

Napi::Value setEventCallback(const Napi::CallbackInfo &info)
{
  return gOverlayMain->setEventCallback(info);
}

Napi::Value setHotkeys(const Napi::CallbackInfo &info)
{
  return gOverlayMain->setHotkeys(info);
}

Napi::Value log(const Napi::CallbackInfo &info)
{
  return gOverlayMain->log(info);
}

Napi::Value addWindow(const Napi::CallbackInfo &info)
{
  return gOverlayMain->addWindow(info);
}

Napi::Value closeWindow(const Napi::CallbackInfo &info)
{
    return gOverlayMain->closeWindow(info);
}

Napi::Value sendWindowBounds(const Napi::CallbackInfo &info)
{
    return gOverlayMain->sendWindowBounds(info);
}

Napi::Value sendCommand(const Napi::CallbackInfo &info)
{
  return gOverlayMain->sendCommand(info);
}

Napi::Value sendFrameBuffer(const Napi::CallbackInfo &info)
{
  return gOverlayMain->sendFrameBuffer(info);
}

Napi::Value translateInputEvent(const Napi::CallbackInfo &info)
{
    return gOverlayMain->translateInputEvent(info);
}

const WCHAR k_inject_helper[] = L"n_ovhelper.exe";
const WCHAR k_inject_helper_x64[] = L"n_ovhelper.x64.exe";

const WCHAR k_inject_dll[] = L"n_overlay.dll";
const WCHAR k_inject_dll_x64[] = L"n_overlay.x64.dll";

struct win_scope_handle
{
  HANDLE handle = nullptr;

  win_scope_handle(HANDLE h = nullptr)
      : handle(h)
  {
  }

  ~win_scope_handle()
  {
    ::CloseHandle(this->handle);
  }

  win_scope_handle &operator=(HANDLE h)
  {
    if (this->handle)
    {
      ::CloseHandle(this->handle);
    }
    this->handle = h;
    return *this;
  }

  operator bool() const
  {
    return !!handle;
  }
};

enum class window_search_mode
{
  INCLUDE_MINIMIZED = 0,
  EXCLUDE_MINIMIZED = 1
};

struct window_hook_info
{
  HWND hwnd;
  std::wstring title;
  DWORD processId;
  DWORD threadId;
};

static inline bool file_exists(const std::wstring& file)
{
  return std::filesystem::exists(file);
}


static bool check_window_valid(HWND window, window_search_mode mode)
{
  DWORD styles, ex_styles;
  RECT rect;

  if (!IsWindowVisible(window) || (mode == window_search_mode::EXCLUDE_MINIMIZED && IsIconic(window)))
    return false;

  GetClientRect(window, &rect);
  styles = (DWORD)GetWindowLongPtr(window, GWL_STYLE);
  ex_styles = (DWORD)GetWindowLongPtr(window, GWL_EXSTYLE);

  if (ex_styles & WS_EX_TOOLWINDOW)
    return false;
  if (styles & WS_CHILD)
    return false;
  if (mode == window_search_mode::EXCLUDE_MINIMIZED && (rect.bottom == 0 || rect.right == 0))
    return false;

  return true;
}

static inline HWND next_window(HWND window, window_search_mode mode)
{
  while (true)
  {
    window = GetNextWindow(window, GW_HWNDNEXT);
    if (!window || check_window_valid(window, mode))
      break;
  }

  return window;
}

static inline HWND first_window(window_search_mode mode)
{
  HWND window = GetWindow(GetDesktopWindow(), GW_CHILD);
  if (!check_window_valid(window, mode))
    window = next_window(window, mode);
  return window;
}

static void get_window_title(std::wstring &name, HWND hwnd)
{
  int len = GetWindowTextLengthW(hwnd);
  if (!len)
    return;
  name.resize(len);
  GetWindowTextW(hwnd, const_cast<wchar_t *>(name.c_str()), len + 1);
}

static bool fill_window_info(window_hook_info &info, HWND hwnd)
{
  wchar_t wname[MAX_PATH];
  win_scope_handle process;
  DWORD processId = 0;
  DWORD threadId = GetWindowThreadProcessId(hwnd, &processId);
  if (!threadId)
    return false;

  if (threadId == GetCurrentProcessId())
    return false;

  process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
  if (!process)
  {
    // std::cout << "err:" << GetLastError() << std::endl;
    return false;
  }

  if (!GetProcessImageFileNameW(process.handle, wname, MAX_PATH))
    return false;

  info.hwnd = hwnd;
  info.processId = processId;
  info.threadId = threadId;

  get_window_title(info.title, hwnd);

  return true;
}

static inline bool is_64bit_windows()
{
#ifdef _WIN64
  return true;
#else
  BOOL x86 = false;
  bool success = !!IsWow64Process(GetCurrentProcess(), &x86);
  return success && !!x86;
#endif
}

static inline bool is_64bit_process(HANDLE process)
{
  BOOL x86 = true;
  if (is_64bit_windows())
  {
    bool success = !!IsWow64Process(process, &x86);
    if (!success)
    {
      return false;
    }
  }

  return !x86;
}

static bool is_64bit_process(DWORD processId)
{
  win_scope_handle process = OpenProcess(
      PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
      false, processId);
  if (!process)
  {
    return false;
  }
  return is_64bit_process(process.handle);
}

static std::wstring get_inject_helper_path(bool x64, bool isAsar = false)
{

  std::filesystem::path base{};
  if (isAsar)
  {
    base = Windows::moduleRootDirPath();
  }
  else
  {
    base = Windows::moduleDirPath();
  }

  std::filesystem::path helper{};
  if (x64)
  {
    if (isAsar)
    {
      helper = base / "resources" / k_inject_helper_x64;
    }
    else
    {
      helper = base / k_inject_helper_x64;
    }
  }
  else
  {
    if (isAsar)
    {
      helper = base / "resources" / k_inject_helper;
    }
    else
    {
      helper = base / k_inject_helper;
    }
  }

  return helper;
}

static std::wstring get_inject_dll_path(bool x64, bool isAsar = false)
{
  std::filesystem::path base{};
  if (isAsar)
  {
    base = Windows::moduleRootDirPath();
  }
  else
  {
    base = Windows::moduleDirPath();
  }

  std::filesystem::path dll{};
  if (x64)
  {
    if (isAsar)
    {
      dll = base / "resources" / k_inject_dll_x64;
    }
    else
    {
      dll = base / k_inject_dll_x64;
    }
  }
  else
  {
    if (isAsar)
    {
      dll = base / "resources" / k_inject_dll;
    }
    else
    {
      dll = base / k_inject_dll;
    }
  }
  return dll;
}

static bool inject_process( HWND window, bool x64, const std::wstring& dll, const std::wstring& helper)
{
  std::wstring args = std::to_wstring((std::uint32_t)(UINT_PTR)window) + L" \"" + dll + L"\"";
  return Windows::createProcess(helper, args);
}

static int getScaleFactor()
{
  HMONITOR hMonitor = MonitorFromWindow(NULL, MONITOR_DEFAULTTOPRIMARY);

  DEVICE_SCALE_FACTOR scaleFactor;
  
  if (GetScaleFactorForMonitor(hMonitor, &scaleFactor) == S_OK)
  {
      return static_cast<int>(scaleFactor);
  }

  return 0;
}

Napi::Value getTopWindows(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();

  std::vector<window_hook_info> windows;

  auto window = first_window(window_search_mode::INCLUDE_MINIMIZED);
  while (window)
  {
    window_hook_info info = {0};
    if (fill_window_info(info, window))
    {
      windows.push_back(info);
    }
    window = next_window(window, window_search_mode::INCLUDE_MINIMIZED);
  }

  auto arr = Napi::Array::New(env, windows.size());
  for (auto i = 0; i != windows.size(); ++i)
  {
    const auto &info = windows[i];
    auto infoObject = Napi::Object::New(env);

    infoObject.Set("windowId", Napi::Value::From(env, (std::uint32_t)(std::uint64_t)info.hwnd));
    infoObject.Set("processId", Napi::Value::From(env, (std::uint32_t)info.processId));
    infoObject.Set("threadId", Napi::Value::From(env, (std::uint32_t)info.threadId));
    infoObject.Set("title", Napi::Value::From(env, Windows::toUtf8(info.title)));

    arr.Set(i, infoObject);
  }

  return arr;
}

Napi::Value injectProcess(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();

  if (info.Length() != 1)
  {
    Napi::TypeError::New(env, "Wrong number of arguments").ThrowAsJavaScriptException();
    return env.Null();
  }

  Napi::Object object = info[0].ToObject();
  const std::uint32_t windowId = object.Get("windowId").ToNumber().Uint32Value();
  const std::uint32_t processId = object.Get("processId").ToNumber().Uint32Value();


  const bool x64 = is_64bit_process(processId);

  std::wstring helper_path = get_inject_helper_path(x64, true);
  if (!file_exists(helper_path))
  {
    helper_path = get_inject_helper_path(x64, false);
  }

  std::wstring dll_path = get_inject_dll_path(x64, true);
  if (!file_exists(dll_path))
  {
    dll_path = get_inject_dll_path(x64, false);
  }

  const bool injected = inject_process((HWND)(UINT_PTR)windowId, x64, dll_path, helper_path);

  auto result = Napi::Object::New(env);

  result.Set("injectHelper", Napi::Value::From(env, Windows::toUtf8(helper_path)));
  result.Set("injectDll", Napi::Value::From(env, Windows::toUtf8(dll_path)));
  result.Set("injectSucceed", Napi::Value::From(env, injected));
  return Napi::Value::From(env, result);
}

Napi::Number getWindowsScaleFactor(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();
  const int scaleFactor = getScaleFactor();
  return Napi::Number::New(env, static_cast<float>(scaleFactor)/100);
}

} // namespace overlay

Napi::Object Init(Napi::Env env, Napi::Object exports)
{
	  exports.Set(Napi::String::New(env, "getTopWindows"), Napi::Function::New(env, overlay::getTopWindows));
	  exports.Set(Napi::String::New(env, "injectProcess"), Napi::Function::New(env, overlay::injectProcess));

    exports.Set(Napi::String::New(env, "start"), Napi::Function::New(env, overlay::start));
    exports.Set(Napi::String::New(env, "stop"), Napi::Function::New(env, overlay::stop));
    exports.Set(Napi::String::New(env, "setEventCallback"), Napi::Function::New(env, overlay::setEventCallback));
    exports.Set(Napi::String::New(env, "setHotkeys"), Napi::Function::New(env, overlay::setHotkeys));
    exports.Set(Napi::String::New(env, "log"), Napi::Function::New(env, overlay::log));
    exports.Set(Napi::String::New(env, "sendCommand"), Napi::Function::New(env, overlay::sendCommand));
    exports.Set(Napi::String::New(env, "addWindow"), Napi::Function::New(env, overlay::addWindow));
    exports.Set(Napi::String::New(env, "closeWindow"), Napi::Function::New(env, overlay::closeWindow));
    exports.Set(Napi::String::New(env, "sendWindowBounds"), Napi::Function::New(env, overlay::sendWindowBounds));
    exports.Set(Napi::String::New(env, "sendFrameBuffer"), Napi::Function::New(env, overlay::sendFrameBuffer));
    exports.Set(Napi::String::New(env, "translateInputEvent"), Napi::Function::New(env, overlay::translateInputEvent));
    exports.Set(Napi::String::New(env, "getWindowsScaleFactor"), Napi::Function::New(env, overlay::getWindowsScaleFactor));

    return exports;
}

NODE_API_MODULE(NODE_GYP_MODULE_NAME, Init)