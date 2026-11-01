// OpenATL test-only Windows DLL: thunk callback owner and bounded unload.
// The DLL is never freed while a GUI window, class or callback owns its code.
// This GUI path never initializes COM or registers a COM class. On native
// Windows the same DLL also contains the unchanged original Aggreg module.
#include <windows.h>
#include <atomic>
#include <atlwin.h>

#if !defined(OPENATL_REPOSITORY_HEADERS)
#error "Tests must use repository-owned OpenATL headers"
#endif

namespace {
struct CallbackOwner {
    HMODULE module = nullptr;
    HANDLE ready = nullptr;
    HANDLE entered = nullptr;
    HANDLE release = nullptr;
    HANDLE worker = nullptr;
    DWORD workerId = 0;
    std::atomic<HWND> hwnd{nullptr};
    std::atomic<bool> registered{false};
    volatile LONG active = 0;
    ATL::CWndProcThunk thunk;
};
CallbackOwner state;
constexpr wchar_t kClass[] = L"OpenATLUnloadProbeOwnedWindow";
constexpr UINT kBlockMessage = WM_APP + 90;

static LRESULT CALLBACK callback(HWND opaqueOwner, UINT message,
                                WPARAM wParam, LPARAM lParam) {
    auto* owner = reinterpret_cast<CallbackOwner*>(opaqueOwner);
    switch (message) {
    case WM_NCCREATE:
        return TRUE;
    case WM_CREATE:
        return 0;
    case kBlockMessage: {
        (void)wParam;
        (void)lParam;
        ::InterlockedIncrement(&owner->active);
        ::SetEvent(owner->entered);
        const DWORD wait = ::WaitForSingleObject(owner->release, 8000);
        ::InterlockedDecrement(&owner->active);
        return wait == WAIT_OBJECT_0 ? 42 : -90;
    }
    case WM_CLOSE: {
        HWND live = owner->hwnd.load();
        if (live != nullptr)
            (void)::DestroyWindow(live);
        return 0;
    }
    case WM_DESTROY:
        return 0;
    case WM_NCDESTROY:
        owner->hwnd.store(nullptr);
        ::PostQuitMessage(0);
        return 0;
    default: {
        HWND live = owner->hwnd.load();
        // During creation the object receives a few harmless setup
        // messages before CreateWindowEx returns its HWND.
        return live ? ::DefWindowProcW(live, message, wParam, lParam) : 0;
    }
    }
}

static DWORD WINAPI worker_main(void*) {
    if (!state.thunk.Init(callback, &state)) {
        ::SetEvent(state.ready);
        return 10;
    }
    WNDCLASSW type{};
    type.lpfnWndProc = state.thunk.GetWNDPROC();
    type.hInstance = state.module;
    type.lpszClassName = kClass;
    if (!::RegisterClassW(&type)) {
        ::SetEvent(state.ready);
        return 11;
    }
    state.registered.store(true);
    HWND window = ::CreateWindowExW(0, kClass, L"OpenATL unload owned window",
                                    WS_POPUP, 0, 0, 24, 24, nullptr,
                                    nullptr, state.module, nullptr);
    state.hwnd.store(window);
    ::SetEvent(state.ready);
    if (window == nullptr) {
        if (::UnregisterClassW(kClass, state.module))
            state.registered.store(false);
        return 12;
    }
    MSG msg{};
    BOOL received = 0;
    while ((received = ::GetMessageW(&msg, nullptr, 0, 0)) > 0) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    if (state.hwnd.load() != nullptr) {
        (void)::DestroyWindow(state.hwnd.load());
        state.hwnd.store(nullptr);
    }
    if (!::UnregisterClassW(kClass, state.module))
        return 13;
    state.registered.store(false);
    return received == 0 ? 0 : 14;
}

static void close_event(HANDLE& owner) {
    if (owner != nullptr) {
        (void)::CloseHandle(owner);
        owner = nullptr;
    }
}
}  // namespace

extern "C" __declspec(dllexport) BOOL WINAPI OpenATLStart(HMODULE self) {
    if (self == nullptr || state.worker != nullptr)
        return FALSE;
    state.module = self;
    state.ready = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    state.entered = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    state.release = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!state.ready || !state.entered || !state.release)
        return FALSE;
    state.worker = ::CreateThread(nullptr, 0, worker_main, nullptr,
                                  0, &state.workerId);
    if (!state.worker)
        return FALSE;
    return ::WaitForSingleObject(state.ready, 5000) == WAIT_OBJECT_0 &&
           state.hwnd.load() != nullptr &&
           state.registered.load();
}

extern "C" __declspec(dllexport) HWND WINAPI OpenATLWindow() {
    return state.hwnd.load();
}
extern "C" __declspec(dllexport) void* WINAPI OpenATLThunkAddress() {
    return state.thunk.GetCodeAddress();
}
extern "C" __declspec(dllexport) BOOL WINAPI OpenATLWaitEntered(DWORD millis) {
    return state.entered != nullptr &&
           ::WaitForSingleObject(state.entered, millis) == WAIT_OBJECT_0;
}
extern "C" __declspec(dllexport) BOOL WINAPI OpenATLReleaseCallback() {
    return state.release != nullptr && ::SetEvent(state.release);
}
extern "C" __declspec(dllexport) BOOL WINAPI OpenATLCanUnload() {
    return state.worker == nullptr && state.hwnd.load() == nullptr &&
           !state.registered.load() &&
           ::InterlockedCompareExchange(&state.active, 0, 0) == 0;
}
extern "C" __declspec(dllexport) DWORD WINAPI OpenATLStop() {
    if (state.worker == nullptr)
        return ERROR_INVALID_HANDLE;
    if (::InterlockedCompareExchange(&state.active, 0, 0) != 0)
        return ERROR_BUSY;
    HWND window = state.hwnd.load();
    if (window == nullptr || !::PostMessageW(window, WM_CLOSE, 0, 0))
        return ERROR_INVALID_WINDOW_HANDLE;
    if (::WaitForSingleObject(state.worker, 5000) != WAIT_OBJECT_0)
        return ERROR_TIMEOUT;
    DWORD threadExit = 0xFFFFFFFF;
    if (!::GetExitCodeThread(state.worker, &threadExit) || threadExit != 0)
        return ERROR_GEN_FAILURE;
    if (state.hwnd.load() != nullptr || state.registered.load())
        return ERROR_BUSY;
    (void)::CloseHandle(state.worker);
    state.worker = nullptr;
    close_event(state.ready);
    close_event(state.entered);
    close_event(state.release);
    return ERROR_SUCCESS;
}
