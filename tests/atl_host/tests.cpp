#include <atlhost.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <algorithm>
bool RunHostLifecycleTests();
#if defined(OPENATL_ORIGINAL_COMMAP_CLASSES)
bool RunMicrosoftCommapClassTests();
bool RunMicrosoftSafeArrayTests();
#endif

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "ACTIVEX_FAIL line=%d: %s hr=%08lx\n", __LINE__, #x, static_cast<unsigned long>(::GetLastError())); return false; } } while (0)
namespace {
inline constexpr IID EventIID = {0x56b852d1,0xe47c,0x48a6,{0x94,0x1b,0xde,0xae,0x8c,0x6d,0x08,0xfb}};
size_t DialogBytesFind(const std::vector<BYTE>& bytes, const void* value, size_t size) {
    const auto first = static_cast<const BYTE*>(value);
    return static_cast<size_t>(std::search(bytes.begin(), bytes.end(), first, first + size) - bytes.begin());
}
bool dialogTemplates() {
    using ATL::detail::AxDialogTemplate;
    using ATL::detail::BuildAxDialogTemplate;
    const HINSTANCE instance = ATL::_AtlBaseModule.GetResourceInstance();
    const WCHAR clsid[] = L"{8856F961-340A-11D0-A96B-00C04FD705A2}";
    const WCHAR sibling[] = L"resource sibling";
    const WORD staticClass[] = {0xFFFF, 0x82};
    for (WORD id : {WORD(602), WORD(606), WORD(607)}) {
        HRSRC resource = ::FindResourceW(instance, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(5));
        CHECK(resource);
        const DWORD size = ::SizeofResource(instance, resource);
        auto original = static_cast<const BYTE*>(::LockResource(::LoadResource(instance, resource)));
        CHECK(original && size > 8);
        const std::vector<BYTE> snapshot(original, original + size);
        AxDialogTemplate built;
        CHECK(BuildAxDialogTemplate(original, size, built) == ERROR_SUCCESS && built.converted == (id != 602));
        CHECK(std::memcmp(original, snapshot.data(), size) == 0 && reinterpret_cast<UINT_PTR>(built.Get()) % 4 == 0);
        AxDialogTemplate again;
        CHECK(BuildAxDialogTemplate(built.storage.data(), built.size, again) == ERROR_SUCCESS && !again.converted);
        CHECK(again.size == built.size && std::memcmp(again.storage.data(), built.storage.data(), built.size) == 0);
        if (id == 602) {
            CHECK(built.size == size && std::memcmp(built.storage.data(), original, size) == 0);
            continue;
        }
        const auto converted = reinterpret_cast<const BYTE*>(built.storage.data());
        std::vector<BYTE> bytes(converted, converted + built.size);
        const size_t cls = DialogBytesFind(snapshot, clsid, sizeof(clsid));
        const size_t oldStatic = DialogBytesFind(snapshot, staticClass, sizeof(staticClass));
        const size_t newStatic = DialogBytesFind(bytes, staticClass, sizeof(staticClass));
        const size_t fixed = id == 606 ? 24 : 18;
        CHECK(cls < size && oldStatic >= fixed && oldStatic < size && newStatic >= fixed && newStatic < bytes.size());
        CHECK(std::memcmp(original, converted, cls) == 0); // Header/font and first fixed record.
        CHECK(size - oldStatic == bytes.size() - newStatic);
        CHECK(std::memcmp(original + oldStatic - fixed, converted + newStatic - fixed, size - oldStatic + fixed) == 0);
        for (size_t cut = 0; cut <= size - 8; ++cut) {
            CHECK(BuildAxDialogTemplate(original, cut, again) == ERROR_INVALID_DATA && again.storage.empty() && !again.converted && !again.size);
        }
        std::vector<BYTE> bad = snapshot;
        const WORD tooMany = 3;
        std::memcpy(bad.data() + (id == 606 ? 16 : 8), &tooMany, sizeof(tooMany));
        CHECK(BuildAxDialogTemplate(bad.data(), bad.size(), again) == ERROR_INVALID_DATA);
        bad = snapshot; const WCHAR invalidHex = L'G';
        std::memcpy(bad.data() + cls + sizeof(WCHAR), &invalidHex, sizeof(invalidHex));
        CHECK(BuildAxDialogTemplate(bad.data(), bad.size(), again) == ERROR_INVALID_DATA);
        bad = snapshot; const WORD inlineData = 4;
        std::memcpy(bad.data() + cls + sizeof(clsid) + sizeof(WORD), &inlineData, sizeof(inlineData));
        CHECK(BuildAxDialogTemplate(bad.data(), bad.size(), again) == ERROR_NOT_SUPPORTED);
        // Creation data for ordinary controls remains opaque. Standard counts
        // include their size word; extended counts exclude it (Win32 contract).
        const size_t label = DialogBytesFind(snapshot, sibling, sizeof(sibling));
        CHECK(label < size);
        const size_t extra = label + sizeof(sibling);
        bad.assign(snapshot.begin(), snapshot.begin() + extra + sizeof(WORD));
        const WORD payloadSize = id == 606 ? 3 : 5;
        std::memcpy(bad.data() + extra, &payloadSize, sizeof(payloadSize));
        const BYTE payload[] = {0xA1, 0xB2, 0xC3};
        bad.insert(bad.end(), payload, payload + sizeof(payload));
        CHECK(BuildAxDialogTemplate(bad.data(), bad.size(), again) == ERROR_SUCCESS && again.converted);
        CHECK(std::memcmp(reinterpret_cast<const BYTE*>(again.storage.data()) + again.size - sizeof(payload), payload, sizeof(payload)) == 0);
        if (id == 606) {
            bad = snapshot; const WORD unsupportedVersion = 2;
            std::memcpy(bad.data(), &unsupportedVersion, sizeof(unsupportedVersion));
            CHECK(BuildAxDialogTemplate(bad.data(), bad.size(), again) == ERROR_INVALID_DATA);
        }
    }
    for (WORD id : {WORD(610), WORD(611), WORD(612), WORD(614), WORD(619)}) {
        const HRSRC dialog = ::FindResourceW(instance, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(5));
        const HRSRC init = ::FindResourceW(instance, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(240));
        CHECK(dialog && init);
        const auto bytes = static_cast<const BYTE*>(::LockResource(::LoadResource(instance, dialog)));
        const auto initBytes = static_cast<const BYTE*>(::LockResource(::LoadResource(instance, init)));
        const DWORD size = ::SizeofResource(instance, dialog), initSize = ::SizeofResource(instance, init);
        const std::vector<BYTE> snapshot(initBytes, initBytes + initSize);
        AxDialogTemplate built;
        CHECK(BuildAxDialogTemplate(bytes, size, built, initBytes, initSize) == ERROR_SUCCESS && built.converted);
        CHECK(built.initialization.size() == (id == 614 ? 2U : 1U));
        CHECK(built.initialization[0].id == 711 && built.initialization[0].stream.size() == (id == 619 ? 5U : 4U));
        CHECK(built.initialization[0].license == ((id == 612 || id == 619) ? L"OpenATL-test-license" : L""));
        CHECK(std::memcmp(initBytes, snapshot.data(), initSize) == 0);
        for (size_t cut = 0; cut + 3 < initSize; ++cut) {
            CHECK(BuildAxDialogTemplate(bytes, size, built, initBytes, cut) == ERROR_INVALID_DATA && built.storage.empty() && built.initialization.empty());
        }
        std::vector<BYTE> bad = snapshot;
        const DWORD oversized = MAXDWORD;
        std::memcpy(bad.data() + 4, &oversized, sizeof(oversized));
        CHECK(BuildAxDialogTemplate(bytes, size, built, bad.data(), bad.size()) == ERROR_INVALID_DATA);
        bad = snapshot; std::memcpy(bad.data() + 8, &oversized, sizeof(oversized));
        CHECK(BuildAxDialogTemplate(bytes, size, built, bad.data(), bad.size()) == ERROR_INVALID_DATA);
        bad = snapshot; const WORD unmatched = 799;
        std::memcpy(bad.data(), &unmatched, sizeof(unmatched));
        CHECK(BuildAxDialogTemplate(bytes, size, built, bad.data(), bad.size()) == ERROR_INVALID_DATA);
        bad = snapshot; const WORD binding = 0x37A;
        std::memcpy(bad.data() + 2, &binding, sizeof(binding));
        CHECK(BuildAxDialogTemplate(bytes, size, built, bad.data(), bad.size()) == ERROR_NOT_SUPPORTED);
        bad = snapshot; bad.push_back(1);
        CHECK(BuildAxDialogTemplate(bytes, size, built, bad.data(), bad.size()) == ERROR_INVALID_DATA);
        if (id == 614) {
            // Resource order may differ from dialog order; initialization follows
            // the dialog's child order, including for reentrant controls.
            std::swap_ranges(bad.begin(), bad.begin() + 16, bad.begin() + 16);
            bad.resize(snapshot.size());
            CHECK(BuildAxDialogTemplate(bytes, size, built, bad.data(), bad.size()) == ERROR_SUCCESS);
            CHECK(built.initialization[0].id == 711 && built.initialization[1].id == 712);
        }
    }
    for (WORD id : {WORD(620), WORD(621), WORD(622), WORD(625), WORD(627)}) {
        const HRSRC dialog = ::FindResourceW(instance, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(5));
        const HRSRC init = ::FindResourceW(instance, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(240));
        CHECK(dialog && init);
        const auto bytes = static_cast<const BYTE*>(::LockResource(::LoadResource(instance, dialog)));
        const auto initBytes = static_cast<const BYTE*>(::LockResource(::LoadResource(instance, init)));
        const DWORD size = ::SizeofResource(instance, dialog), initSize = ::SizeofResource(instance, init);
        const std::vector<BYTE> snapshot(initBytes, initBytes + initSize);
        const size_t minimum = id == 622 ? 26 : id == 627 ? 18 : id == 625 ? 14 : 10;
        CHECK(initSize >= minimum && initSize - minimum <= 3);
        AxDialogTemplate built;
        CHECK(BuildAxDialogTemplate(bytes, size, built, initBytes, initSize) == ERROR_SUCCESS && built.converted);
        CHECK(built.initialization.size() == ((id == 622 || id == 627) ? 2U : 1U));
        const auto& first = built.initialization[0];
        CHECK(first.id == 711 && first.stream.empty() && first.license.empty());
        CHECK(first.kind == (id == 625 ? ATL::detail::AxDialogInit::Kind::Stream : ATL::detail::AxDialogInit::Kind::New));
        if (id == 622) {
            CHECK(built.initialization[1].id == 712 && built.initialization[1].kind == ATL::detail::AxDialogInit::Kind::Stream);
            CHECK(built.initialization[1].stream.size() == 4 && built.initialization[1].stream[0] == 42);
        }
        CHECK(std::memcmp(initBytes, snapshot.data(), initSize) == 0);
        for (size_t cut = 0; cut < minimum; ++cut) {
            CHECK(BuildAxDialogTemplate(bytes, size, built, initBytes, cut) == ERROR_INVALID_DATA && built.storage.empty() && built.initialization.empty());
        }
        std::vector<BYTE> bad = snapshot;
        const DWORD oversized = MAXDWORD;
        std::memcpy(bad.data() + 4, &oversized, sizeof(oversized));
        CHECK(BuildAxDialogTemplate(bytes, size, built, bad.data(), bad.size()) == ERROR_INVALID_DATA);
        bad = snapshot; const WORD ordinary = 714;
        std::memcpy(bad.data(), &ordinary, sizeof(ordinary));
        CHECK(BuildAxDialogTemplate(bytes, size, built, bad.data(), bad.size()) == ERROR_INVALID_DATA);
        if (id == 620) {
            bad = snapshot; const WORD stream = 0x376;
            std::memcpy(bad.data() + 2, &stream, sizeof(stream));
            CHECK(BuildAxDialogTemplate(bytes, size, built, bad.data(), bad.size()) == ERROR_INVALID_DATA);
        }
    }
    for (WORD id : {WORD(631), WORD(632), WORD(633), WORD(636)}) {
        const HRSRC dialog = ::FindResourceW(instance, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(5));
        const HRSRC init = ::FindResourceW(instance, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(240));
        CHECK(dialog && init);
        const auto bytes = static_cast<const BYTE*>(::LockResource(::LoadResource(instance, dialog)));
        const auto initBytes = static_cast<const BYTE*>(::LockResource(::LoadResource(instance, init)));
        const DWORD size = ::SizeofResource(instance, dialog), initSize = ::SizeofResource(instance, init);
        const std::vector<BYTE> snapshot(initBytes, initBytes + initSize);
        const size_t minimum = id == 632 ? 2614 : id == 633 ? 2598 : id == 636 ? 5146 : 2574;
        CHECK(initSize >= minimum && initSize - minimum <= 3);
        AxDialogTemplate built;
        CHECK(BuildAxDialogTemplate(bytes, size, built, initBytes, initSize) == ERROR_SUCCESS && built.converted);
        CHECK(built.initialization.size() == (id == 633 ? 3U : id == 636 ? 2U : 1U));
        CHECK(built.initialization[0].id == 711 && built.initialization[0].kind == ATL::detail::AxDialogInit::Kind::Storage);
        CHECK(built.initialization[0].stream.size() == 2560);
        CHECK(built.initialization[0].license == (id == 632 ? L"OpenATL-test-license" : L""));
        if (id == 633) {
            CHECK(built.initialization[1].id == 712 && built.initialization[1].kind == ATL::detail::AxDialogInit::Kind::New);
            CHECK(built.initialization[2].id == 713 && built.initialization[2].kind == ATL::detail::AxDialogInit::Kind::Stream);
        }
        CHECK(std::memcmp(initBytes, snapshot.data(), initSize) == 0);
        for (size_t cut = 0; cut < minimum; ++cut) {
            CHECK(BuildAxDialogTemplate(bytes, size, built, initBytes, cut) == ERROR_INVALID_DATA && built.storage.empty() && built.initialization.empty());
        }
        std::vector<BYTE> bad = snapshot; const DWORD oversized = MAXDWORD;
        std::memcpy(bad.data() + 4, &oversized, sizeof(oversized));
        CHECK(BuildAxDialogTemplate(bytes, size, built, bad.data(), bad.size()) == ERROR_INVALID_DATA);
        bad = snapshot; std::memcpy(bad.data() + 8, &oversized, sizeof(oversized));
        CHECK(BuildAxDialogTemplate(bytes, size, built, bad.data(), bad.size()) == ERROR_INVALID_DATA);
    }
    for (WORD id : {WORD(637), WORD(638)}) {
        const HRSRC dialog = ::FindResourceW(instance, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(5));
        const HRSRC init = ::FindResourceW(instance, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(240));
        CHECK(dialog && init);
        const auto bytes = static_cast<const BYTE*>(::LockResource(::LoadResource(instance, dialog)));
        const auto initBytes = static_cast<const BYTE*>(::LockResource(::LoadResource(instance, init)));
        const DWORD size = ::SizeofResource(instance, dialog), initSize = ::SizeofResource(instance, init);
        const std::vector<BYTE> snapshot(initBytes, initBytes + initSize);
        AxDialogTemplate built;
        CHECK(BuildAxDialogTemplate(bytes, size, built, initBytes, initSize) == ERROR_SUCCESS && !built.converted);
        CHECK(built.size == size && !std::memcmp(bytes, built.storage.data(), size) && built.initialization.size() == 6);
        CHECK(built.initialization[0].id == 711 && built.initialization[0].kind == ATL::detail::AxDialogInit::Kind::ListString);
        CHECK(built.initialization[1].id == 711 && !std::strcmp(reinterpret_cast<const char*>(built.initialization[1].stream.data()), "alpha"));
        CHECK(built.initialization[2].id == 712 && built.initialization[5].stream.size() == 1);
        const size_t minimum = 74; // Six headers, their counted bytes and the terminator.
        CHECK(initSize >= minimum && initSize - minimum <= 3);
        for (size_t cut = 0; cut < minimum; ++cut) {
            CHECK(BuildAxDialogTemplate(bytes, size, built, initBytes, cut) == ERROR_INVALID_DATA && built.storage.empty() && built.initialization.empty());
        }
        CHECK(!std::memcmp(initBytes, snapshot.data(), initSize));
        auto bad = snapshot; const WORD unknown = 799;
        std::memcpy(bad.data(), &unknown, sizeof(unknown));
        CHECK(BuildAxDialogTemplate(bytes, size, built, bad.data(), bad.size()) == ERROR_INVALID_DATA);
        bad = snapshot; const WORD list = 0x401;
        std::memcpy(bad.data() + 2, &list, sizeof(list)); // Conflicting record kinds for one ID.
        CHECK(BuildAxDialogTemplate(bytes, size, built, bad.data(), bad.size()) == ERROR_INVALID_DATA);
    }
    return true;
}
bool DialogStringContents(HWND window) {
    const HWND list = ::GetDlgItem(window, 711), combo = ::GetDlgItem(window, 712);
    CHECK(list && combo && ::SendMessageW(list, LB_GETCOUNT, 0, 0) == 2 && ::SendMessageW(combo, CB_GETCOUNT, 0, 0) == 4);
    WCHAR text[32]{};
    CHECK(::SendMessageW(list, LB_GETTEXT, 0, reinterpret_cast<LPARAM>(text)) == 5 && !::wcscmp(text, L"alpha"));
    CHECK(::SendMessageW(list, LB_GETTEXT, 1, reinterpret_cast<LPARAM>(text)) == 4 && !::wcscmp(text, L"beta"));
    const bool upper = (::GetWindowLongPtrW(combo, GWL_STYLE) & CBS_UPPERCASE) != 0;
    CHECK(::SendMessageW(combo, CB_GETLBTEXT, 0, reinterpret_cast<LPARAM>(text)) == 3 && !::wcscmp(text, upper ? L"LOW" : L"low"));
    CHECK(::SendMessageW(combo, CB_GETLBTEXT, 1, reinterpret_cast<LPARAM>(text)) == 4 && !::wcscmp(text, upper ? L"HIGH" : L"high"));
    const char encoded[] = {static_cast<char>(0xC3), static_cast<char>(0xA9), 0};
    WCHAR expected[16]{};
    const int count = ::MultiByteToWideChar(CP_ACP, 0, encoded, 2, expected, 15);
    CHECK(count > 0);
    if (upper) CHECK(::CharUpperBuffW(expected, count) == static_cast<DWORD>(count));
    CHECK(::SendMessageW(combo, CB_GETLBTEXT, 2, reinterpret_cast<LPARAM>(text)) == count && !::wcscmp(text, expected));
    CHECK(::SendMessageW(combo, CB_GETLBTEXT, 3, reinterpret_cast<LPARAM>(text)) == 0 && !text[0]);
    CHECK(::SendMessageW(combo, CB_GETCURSEL, 0, 0) == CB_ERR);
    return true;
}
struct FreeDialogState { bool modal = false, checkStrings = false, good = true; int initialized = 0; HWND window = nullptr; };
INT_PTR CALLBACK FreeDialogProc(HWND window, UINT message, WPARAM, LPARAM parameter) {
    if (message != WM_INITDIALOG) return FALSE;
    auto state = reinterpret_cast<FreeDialogState*>(parameter);
    ++state->initialized; state->window = window;
    if (state->checkStrings) state->good = DialogStringContents(window);
    if (state->modal) ::EndDialog(window, 83);
    return TRUE;
}
class PlainDialog : public ATL::CAxDialogImpl<PlainDialog> {
public:
    enum { IDD = 602 };
    int resource = IDD;
    int GetIDD() const { return resource; }
    bool modal = false, modeRejected = false;
    bool good = true;
    int initialized = 0, finalized = 0;
    LPARAM parameter = 0;
    LRESULT Init(UINT, WPARAM, LPARAM value, BOOL&) {
        ++initialized; parameter = value;
        if (resource == 637 || resource == 638) good = DialogStringContents(m_hWnd);
        if (modal) { modeRejected = !DestroyWindow(); EndDialog(73); }
        return TRUE;
    }
    void OnFinalMessage(HWND) override { ++finalized; }
    BEGIN_MSG_MAP(PlainDialog)
        MESSAGE_HANDLER(WM_INITDIALOG, Init)
    END_MSG_MAP()
};
class InitFailureDialog : public ATL::CAxDialogImpl<InitFailureDialog> {
public:
    enum { IDD = 602 };
    int initialized = 0, finalized = 0;
    HRESULT AdviseSinkMap(bool advise) noexcept { return advise ? E_ACCESSDENIED : S_OK; }
    LRESULT Init(UINT, WPARAM, LPARAM, BOOL&) { ++initialized; return TRUE; }
    void OnFinalMessage(HWND) override { ++finalized; }
    BEGIN_MSG_MAP(InitFailureDialog)
        MESSAGE_HANDLER(WM_INITDIALOG, Init)
    END_MSG_MAP()
};
struct DialogRetirement { int initialized = 0, finalized = 0, destroyed = 0; bool good = true; };
class CloseFailureDialog : public ATL::CAxDialogImpl<CloseFailureDialog> {
public:
    enum { IDD = 602 };
    bool fail = false, preserved = false;
    HRESULT AdviseSinkMap(bool advise) noexcept { return !advise && fail ? E_ACCESSDENIED : S_OK; }
    LRESULT Init(UINT, WPARAM, LPARAM, BOOL&) {
        if (m_bModal) {
            fail = true; preserved = !EndDialog(1) && ::IsWindow(this->m_hWnd);
            fail = false; EndDialog(82);
        }
        return TRUE;
    }
    BEGIN_MSG_MAP(CloseFailureDialog)
        MESSAGE_HANDLER(WM_INITDIALOG, Init)
    END_MSG_MAP()
};
class DeletingDialog : public ATL::CAxDialogImpl<DeletingDialog> {
    DialogRetirement& state_;
    bool modal_;
    int resource_;
public:
    enum { IDD = 602 };
    DeletingDialog(DialogRetirement& state, bool modal, int resource = IDD) : state_(state), modal_(modal), resource_(resource) {}
    int GetIDD() const { return resource_; }
    ~DeletingDialog() { ++state_.destroyed; }
    LRESULT Init(UINT, WPARAM, LPARAM, BOOL&) {
        ++state_.initialized;
        if (resource_ != IDD) state_.good = DialogStringContents(m_hWnd);
        if (modal_) EndDialog(91); else DestroyWindow();
        return TRUE;
    }
    void OnFinalMessage(HWND) override { ++state_.finalized; delete this; }
    BEGIN_MSG_MAP(DeletingDialog)
        MESSAGE_HANDLER(WM_INITDIALOG, Init)
    END_MSG_MAP()
};
bool plainDialogs() {
    const HINSTANCE instance = ATL::_AtlBaseModule.GetResourceInstance();
    FreeDialogState free;
    HWND window = ATL::AtlAxCreateDialog(instance, MAKEINTRESOURCEW(602), nullptr, FreeDialogProc, reinterpret_cast<LPARAM>(&free));
    CHECK(window && free.window == window && free.initialized == 1 && ::GetDlgItem(window, 702));
    CHECK(::DestroyWindow(window));
    free.modal = true;
    CHECK(ATL::AtlAxDialogBox(instance, L"OPENATL_PLAIN", nullptr, FreeDialogProc, reinterpret_cast<LPARAM>(&free)) == 83 && free.initialized == 2);
    CHECK(!ATL::AtlAxCreateDialog(instance, nullptr, nullptr, FreeDialogProc, 0) && ::GetLastError() == ERROR_INVALID_PARAMETER);
    CHECK(ATL::AtlAxDialogBox(instance, MAKEINTRESOURCEW(999), nullptr, FreeDialogProc, 0) == -1 && ::GetLastError() != ERROR_SUCCESS);
    free = {};
    window = ATL::AtlAxCreateDialog(instance, MAKEINTRESOURCEW(605), nullptr, FreeDialogProc, reinterpret_cast<LPARAM>(&free));
    CHECK(window && free.initialized == 1 && ::DestroyWindow(window));
    free.modal = true;
    CHECK(ATL::AtlAxDialogBox(instance, MAKEINTRESOURCEW(605), nullptr, FreeDialogProc, reinterpret_cast<LPARAM>(&free)) == 83 && free.initialized == 2);
    for (WORD id : {WORD(604), WORD(609)}) {
        CHECK(!ATL::AtlAxCreateDialog(instance, MAKEINTRESOURCEW(id), nullptr, FreeDialogProc, 0) && ::GetLastError() == ERROR_NOT_SUPPORTED);
        CHECK(ATL::AtlAxDialogBox(instance, MAKEINTRESOURCEW(id), nullptr, FreeDialogProc, 0) == -1 && ::GetLastError() == ERROR_NOT_SUPPORTED);
    }
    for (WORD id : {WORD(615), WORD(616), WORD(617), WORD(618)}) {
        const DWORD error = id == 617 ? ERROR_NOT_SUPPORTED : ERROR_INVALID_DATA;
        CHECK(!ATL::AtlAxCreateDialog(instance, MAKEINTRESOURCEW(id), nullptr, FreeDialogProc, 0) && ::GetLastError() == error);
        CHECK(ATL::AtlAxDialogBox(instance, MAKEINTRESOURCEW(id), nullptr, FreeDialogProc, 0) == -1 && ::GetLastError() == error);
    }
    for (WORD id : {WORD(623), WORD(624), WORD(626)}) {
        const DWORD error = id == 623 ? ERROR_NOT_SUPPORTED : ERROR_INVALID_DATA;
        CHECK(!ATL::AtlAxCreateDialog(instance, MAKEINTRESOURCEW(id), nullptr, FreeDialogProc, 0) && ::GetLastError() == error);
        CHECK(ATL::AtlAxDialogBox(instance, MAKEINTRESOURCEW(id), nullptr, FreeDialogProc, 0) == -1 && ::GetLastError() == error);
    }
    CHECK(!ATL::AtlAxCreateDialog(instance, MAKEINTRESOURCEW(635), nullptr, FreeDialogProc, 0) && ::GetLastError() == ERROR_INVALID_DATA);
    CHECK(ATL::AtlAxDialogBox(instance, MAKEINTRESOURCEW(635), nullptr, FreeDialogProc, 0) == -1 && ::GetLastError() == ERROR_INVALID_DATA);
    PlainDialog plain; plain.modal = true;
    CHECK(plain.GetIDD() == 602 && plain.GetDialogProc());
    CHECK(plain.DoModal(nullptr, 1234) == 73 && plain.initialized == 1 && plain.finalized == 1 && plain.parameter == 1234 && plain.modeRejected && !plain.m_hWnd);
    plain.modal = false; RECT ignored{};
    CHECK(plain.Create(nullptr, ignored, 5678) && plain.parameter == 5678 && plain.initialized == 2);
    CHECK(!plain.Create(nullptr) && ::GetLastError() == ERROR_ALREADY_EXISTS);
    CHECK(!plain.EndDialog(0) && ::IsWindow(plain.m_hWnd));
    CHECK(plain.DestroyWindow() && plain.finalized == 2 && !plain.m_hWnd);
    InitFailureDialog failed;
    CHECK(!failed.Create(nullptr) && ::GetLastError() == ERROR_ACCESS_DENIED && !failed.initialized && failed.finalized == 1 && !failed.m_hWnd);
    CHECK(failed.DoModal(nullptr) == -1 && ::GetLastError() == ERROR_ACCESS_DENIED && !failed.initialized && failed.finalized == 2 && !failed.m_hWnd);
    CloseFailureDialog retry;
    CHECK(retry.Create(nullptr)); retry.fail = true;
    CHECK(!retry.DestroyWindow() && ::GetLastError() == ERROR_ACCESS_DENIED && ::IsWindow(retry.m_hWnd));
    retry.fail = false; CHECK(retry.DestroyWindow() && !retry.m_hWnd);
    CHECK(retry.DoModal(nullptr) == 82 && retry.preserved && !retry.m_hWnd);
    DialogRetirement modeless, modal;
    CHECK(!(new DeletingDialog(modeless, false))->Create(nullptr));
    CHECK(modeless.initialized == 1 && modeless.finalized == 1 && modeless.destroyed == 1);
    CHECK((new DeletingDialog(modal, true))->DoModal(nullptr) == 91);
    CHECK(modal.initialized == 1 && modal.finalized == 1 && modal.destroyed == 1);
    for (WORD id : {WORD(637), WORD(638)}) {
        PlainDialog strings; strings.resource = id;
        CHECK(strings.Create(nullptr, 1773) && strings.good && strings.parameter == 1773 && strings.initialized == 1);
        CHECK(strings.DestroyWindow() && strings.finalized == 1);
        strings.modal = true;
        CHECK(strings.DoModal(nullptr, 1774) == 73 && strings.good && strings.parameter == 1774 && strings.initialized == 2 && strings.finalized == 2);
        strings.modal = false; strings.resource = 639;
        CHECK(!strings.Create(nullptr) && ::GetLastError() == ERROR_INVALID_DATA && strings.initialized == 2);
        strings.resource = id;
        CHECK(strings.Create(nullptr) && strings.good && strings.DestroyWindow() && strings.initialized == 3);
    }
    for (WORD id : {WORD(639), WORD(640), WORD(641), WORD(642), WORD(643), WORD(644), WORD(645), WORD(646)}) {
        const DWORD error = id == 641 || id == 645 || id == 646 ? ERROR_NOT_SUPPORTED : ERROR_INVALID_DATA;
        CHECK(!ATL::AtlAxCreateDialog(instance, MAKEINTRESOURCEW(id), nullptr, FreeDialogProc, 0) && ::GetLastError() == error);
        CHECK(ATL::AtlAxDialogBox(instance, MAKEINTRESOURCEW(id), nullptr, FreeDialogProc, 0) == -1 && ::GetLastError() == error);
    }
    free = {}; free.checkStrings = true;
    window = ATL::AtlAxCreateDialog(instance, L"OPENATL_STRINGS", nullptr, FreeDialogProc, reinterpret_cast<LPARAM>(&free));
    CHECK(window && free.good && free.initialized == 1 && ::DestroyWindow(window));
    free.modal = true;
    CHECK(ATL::AtlAxDialogBox(instance, L"OPENATL_STRINGS", nullptr, FreeDialogProc, reinterpret_cast<LPARAM>(&free)) == 83 && free.good && free.initialized == 2);
    DialogRetirement stringModeless, stringModal;
    CHECK(!(new DeletingDialog(stringModeless, false, 637))->Create(nullptr));
    CHECK(stringModeless.good && stringModeless.initialized == 1 && stringModeless.finalized == 1 && stringModeless.destroyed == 1);
    CHECK((new DeletingDialog(stringModal, true, 638))->DoModal(nullptr) == 91);
    CHECK(stringModal.good && stringModal.initialized == 1 && stringModal.finalized == 1 && stringModal.destroyed == 1);
    return true;
}
class Source : public IConnectionPointContainer, public IConnectionPoint {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        if (iid == IID_IUnknown || iid == IID_IConnectionPointContainer) *value = static_cast<IConnectionPointContainer*>(this);
        else if (iid == IID_IConnectionPoint) *value = static_cast<IConnectionPoint*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE EnumConnectionPoints(IEnumConnectionPoints**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE FindConnectionPoint(REFIID iid, IConnectionPoint** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        return iid == EventIID ? QueryInterface(IID_IConnectionPoint, reinterpret_cast<void**>(value)) : CONNECT_E_NOCONNECTION;
    }
    HRESULT STDMETHODCALLTYPE GetConnectionInterface(IID* value) override { if (!value) return E_POINTER; *value = EventIID; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetConnectionPointContainer(IConnectionPointContainer** value) override {
        return QueryInterface(IID_IConnectionPointContainer, reinterpret_cast<void**>(value));
    }
    HRESULT STDMETHODCALLTYPE Advise(IUnknown* value, DWORD* cookie) override {
        if (!value || !cookie) return E_POINTER;
        if (sink) return CONNECT_E_ADVISELIMIT;
        HRESULT hr = value->QueryInterface(EventIID, reinterpret_cast<void**>(&sink));
        if (SUCCEEDED(hr)) { *cookie = 42; ++advises; }
        return hr;
    }
    HRESULT STDMETHODCALLTYPE Unadvise(DWORD cookie) override {
        if (cookie != 42 || !sink) return CONNECT_E_NOCONNECTION;
        sink.Release(); ++unadvises; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EnumConnections(IEnumConnections**) override { return E_NOTIMPL; }
    HRESULT Fire(DISPID id, DISPPARAMS& params, UINT* argument = nullptr) {
        ATL::CComPtr<IDispatch> retained = sink;
        return retained ? retained->Invoke(id, IID_NULL, 0, DISPATCH_METHOD, &params, nullptr, nullptr, argument) : CONNECT_E_NOCONNECTION;
    }
    ULONG refs = 1;
    int advises = 0, unadvises = 0;
private:
    ATL::CComPtr<IDispatch> sink;
};
class Sink : public ATL::IDispEventSimpleImpl<7, Sink, &EventIID> {
public:
    inline static ATL::_ATL_FUNC_INFO scalar = {CC_STDCALL, VT_EMPTY, 3, {VT_I4, VT_BOOL, VT_BSTR}};
    inline static ATL::_ATL_FUNC_INFO ref = {CC_STDCALL, VT_EMPTY, 1, {VT_VARIANT | VT_BYREF}};
    inline static ATL::_ATL_FUNC_INFO throwing = {CC_STDCALL, VT_EMPTY, 0, {}};
    inline static ATL::_ATL_FUNC_INFO doubleRef = {CC_STDCALL, VT_EMPTY, 1, {VT_BYREF | VT_R4}};
    BEGIN_SINK_MAP(Sink)
        SINK_ENTRY_INFO(7, EventIID, 1, Scalar, &scalar)
        SINK_ENTRY_INFO(7, EventIID, 2, Ref, &ref)
        SINK_ENTRY_INFO(7, EventIID, 3, Throw, &throwing)
        SINK_ENTRY_INFO(7, EventIID, 4, Double, &doubleRef)
    END_SINK_MAP()
    void __stdcall Scalar(long number, VARIANT_BOOL enabled, BSTR text) {
        good = number == 123 && enabled == VARIANT_TRUE && text && std::wcscmp(text, L"event\u6771\u4eac") == 0;
        ++calls;
    }
    void __stdcall Ref(VARIANT* value) { value->lVal += 1; }
    void __stdcall Throw() { throw std::runtime_error("handler failure"); }
    void __stdcall Double(double*) { ++doubleCalls; }
    int doubleCalls = 0;
    int calls = 0;
    bool good = false;
};
bool events() {
    ATL::CComPtr<Source> source; source.Attach(new Source);
    ATL::CComPtr<Source> other; other.Attach(new Source);
    auto unknown = static_cast<IConnectionPointContainer*>(source.p);
    {
        ATL::CComPtr<IConnectionPoint> secondary;
        CHECK(source->FindConnectionPoint(EventIID, &secondary) == S_OK);
        ATL::CComQIPtr<IUnknown> identity(secondary.p);
        CHECK(identity.p == static_cast<IUnknown*>(unknown));
        {
            ATL::CComQIPtr<IUnknown, &IID_IConnectionPoint> explicitlySelected(unknown);
            CHECK(static_cast<void*>(explicitlySelected.p) == static_cast<void*>(secondary.p));
        }
        const ULONG references = source->refs;
        identity = secondary.p;
        CHECK(identity.p == static_cast<IUnknown*>(unknown) && source->refs == references);
        ATL::CComQIPtr<IUnknown> fromSmartPointer = secondary;
        CHECK(fromSmartPointer.p == identity.p);
        fromSmartPointer = secondary;
        CHECK(fromSmartPointer.p == identity.p && source->refs == references + 1);
    }
    CHECK(source->refs == 1);
    {
        Sink sink;
        CHECK(sink.DispEventAdvise(unknown) == S_OK);
        CHECK(sink.DispEventAdvise(unknown) == CONNECT_E_ADVISELIMIT);
        ATL::CComVariant args[3] = {ATL::CComVariant(L"event\u6771\u4eac"), ATL::CComVariant(VARIANT_TRUE), ATL::CComVariant(123L)};
        args[1].vt = VT_BOOL; args[1].boolVal = VARIANT_TRUE;
        DISPPARAMS params = {args, nullptr, 3, 0};
        CHECK(source->Fire(1, params) == S_OK && sink.good && sink.calls == 1);
        Sink::scalar.pVarTypes[2] = VT_I4;
        CHECK(source->Fire(1, params) == E_INVALIDARG && sink.calls == 1);
        Sink::scalar.pVarTypes[2] = VT_BSTR;
        params.cNamedArgs = 1;
        CHECK(source->Fire(1, params) == DISP_E_NONAMEDARGS);
        params.cNamedArgs = 0;
        CHECK(source->Fire(99, params) == DISP_E_MEMBERNOTFOUND);
        params.cArgs = 2; CHECK(source->Fire(1, params) == DISP_E_BADPARAMCOUNT); params.cArgs = 3;
        args[2] = L"not a number";
        UINT argument = 99;
        CHECK(source->Fire(1, params, &argument) == DISP_E_TYPEMISMATCH && argument == 2 && sink.calls == 1);
        ATL::CComVariant value(40L);
        VARIANTARG reference{}; reference.vt = VT_VARIANT | VT_BYREF; reference.pvarVal = &value;
        DISPPARAMS byref = {&reference, nullptr, 1, 0};
        CHECK(source->Fire(2, byref) == S_OK && value.lVal == 41);
        reference.pvarVal = nullptr;
        CHECK(source->Fire(2, byref) == DISP_E_TYPEMISMATCH);
        DISPPARAMS empty{};
        CHECK(source->Fire(3, empty) == DISP_E_EXCEPTION);
        alignas(double) float number = 2.0f;
        VARIANTARG floating{}; floating.vt = VT_BYREF | VT_R4; floating.pfltVal = &number;
        DISPPARAMS floatingParams = {&floating, nullptr, 1, 0};
        CHECK(source->Fire(4, floatingParams) == E_INVALIDARG && sink.doubleCalls == 0);
        double wide = 2.0;
        Sink::doubleRef.pVarTypes[0] = VT_BYREF | VT_R8;
        floating.vt = VT_BYREF | VT_R8; floating.pdblVal = &wide;
        CHECK(source->Fire(4, floatingParams) == S_OK && sink.doubleCalls == 1);
        CHECK(sink.DispEventUnadvise(static_cast<IConnectionPointContainer*>(other.p)) == CONNECT_E_NOCONNECTION);
        CHECK(sink.DispEventUnadvise(unknown) == S_OK && source->unadvises == 1);
        CHECK(source->Fire(1, params) == CONNECT_E_NOCONNECTION);
        CHECK(sink.DispEventAdvise(unknown) == S_OK);
    }
    CHECK(source->advises == 2 && source->unadvises == 2 && source->refs == 1);
    return true;
}
class BrowserSink : public ATL::CWindow, public ATL::IDispEventImpl<1, BrowserSink, &DIID_DWebBrowserEvents2> {
public:
    explicit BrowserSink(HWND parent) : ATL::CWindow(parent) {}
    BEGIN_SINK_MAP(BrowserSink)
        SINK_ENTRY_EX(1, DIID_DWebBrowserEvents2, 259, &BrowserSink::Document)
    END_SINK_MAP()
    void __stdcall Document(IDispatch*, VARIANT* value) { if (value && value->vt == VT_BSTR) ++documents; }
    int documents = 0;
};
class ControlDialog : public ATL::CAxDialogImpl<ControlDialog>,
                      public ATL::IDispEventImpl<701, ControlDialog, &DIID_DWebBrowserEvents2> {
public:
    enum { IDD = 601 };
    int resource = IDD;
    int GetIDD() noexcept { return resource; }
    bool modal = false, good = true;
    int initialized = 0, finalized = 0, documents = 0;
    LPARAM parameter = 0;
    ATL::CComPtr<IWebBrowser2> control;
    BEGIN_SINK_MAP(ControlDialog)
        SINK_ENTRY_EX(701, DIID_DWebBrowserEvents2, 259, &ControlDialog::Document)
    END_SINK_MAP()
    LRESULT Init(UINT, WPARAM, LPARAM value, BOOL&) {
        ++initialized; parameter = value;
        good = GetDlgControl(701, IID_IWebBrowser2, reinterpret_cast<void**>(&control.p)) == S_OK;
        if (resource == 606 || resource == 607 || resource == 630) {
            WCHAR name[32]{}, text[32]{};
            const HWND host = ::GetDlgItem(this->m_hWnd, 701);
            good = good && ::GetClassNameW(host, name, 32) && std::wcscmp(name, L"AtlAxWin140") == 0;
            good = good && (::GetWindowLongPtrW(host, GWL_EXSTYLE) & WS_EX_CLIENTEDGE) != 0;
            good = good && ::GetDlgCtrlID(host) == 701 && ::GetDlgItemTextW(this->m_hWnd, 704, text, 32) && std::wcscmp(text, L"resource sibling") == 0;
            if (resource != 607) good = good && ::GetWindowContextHelpId(host) == 273;
        }
        good = good && ::SetTimer(this->m_hWnd, 1, 4500, nullptr) != 0;
        ::PostMessageW(this->m_hWnd, WM_APP + 81, 0, 0);
        return TRUE;
    }
    void Finish(int result) {
        if (modal) { if (!EndDialog(result)) { good = false; ::EndDialog(this->m_hWnd, -2); } }
        else if (!DestroyWindow()) { good = false; ::DestroyWindow(this->m_hWnd); }
    }
    LRESULT Start(UINT, WPARAM, LPARAM, BOOL&) {
        ATL::CComVariant url(L"about:blank#openatl-dialog"), empty;
        if (!good || control->Navigate2(&url, &empty, &empty, &empty, &empty) != S_OK) { good = false; Finish(-2); }
        return 0;
    }
    LRESULT Timeout(UINT, WPARAM, LPARAM, BOOL&) { good = false; Finish(-2); return 0; }
    LRESULT Destroy(UINT, WPARAM, LPARAM, BOOL&) { control.Release(); return 0; }
    void Document(IDispatch*, VARIANT* value) {
        if (!value || value->vt != VT_BSTR) return;
        ++documents; ::KillTimer(this->m_hWnd, 1);
        if (modal) Finish(73);
    }
    void OnFinalMessage(HWND) override { ++finalized; }
    BEGIN_MSG_MAP(ControlDialog)
        MESSAGE_HANDLER(WM_INITDIALOG, Init)
        MESSAGE_HANDLER(WM_APP + 81, Start)
        MESSAGE_HANDLER(WM_TIMER, Timeout)
        MESSAGE_HANDLER(WM_DESTROY, Destroy)
    END_MSG_MAP()
};
class MissingControlDialog : public ATL::CAxDialogImpl<MissingControlDialog> {
public:
    enum { IDD = 603 };
    int resource = IDD;
    int GetIDD() noexcept { return resource; }
    int initialized = 0;
    LRESULT Init(UINT, WPARAM, LPARAM, BOOL&) { ++initialized; return TRUE; }
    BEGIN_MSG_MAP(MissingControlDialog)
        MESSAGE_HANDLER(WM_INITDIALOG, Init)
    END_MSG_MAP()
};
bool controlDialogs(ATL::CAtlModule& module) {
    for (WORD resource : {WORD(601), WORD(606), WORD(607), WORD(630)}) {
        ControlDialog modeless;
        modeless.resource = resource;
        CHECK(modeless.Create(nullptr, 312) && modeless.initialized == 1 && modeless.parameter == 312);
        CHECK(modeless.AdviseSinkMap(true) == CONNECT_E_ADVISELIMIT);
        const ULONGLONG deadline = ::GetTickCount64() + 6000;
        while (!modeless.documents && modeless.m_hWnd && ::GetTickCount64() < deadline) {
            MSG message{};
            while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (!modeless.IsDialogMessage(&message)) { ::TranslateMessage(&message); ::DispatchMessageW(&message); }
            }
            ::MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
        }
        CHECK(modeless.good && modeless.documents && modeless.DestroyWindow() && modeless.finalized == 1 && !modeless.m_hWnd);
        CHECK(modeless.AdviseSinkMap(false) == S_OK && module.GetLockCount() == 0);
        ControlDialog modal; modal.modal = true; modal.resource = resource;
        CHECK(modal.DoModal(nullptr, 728) == 73 && modal.good && modal.documents && modal.initialized == 1 && modal.parameter == 728 && modal.finalized == 1 && !modal.m_hWnd);
        CHECK(module.GetLockCount() == 0);
    }
    for (WORD resource : {WORD(603), WORD(608)}) {
        MissingControlDialog missing;
        missing.resource = resource;
        CHECK(!missing.Create(nullptr) && !missing.initialized && !missing.m_hWnd && module.GetLockCount() == 0);
        CHECK(missing.DoModal(nullptr) == -1 && !missing.initialized && !missing.m_hWnd && module.GetLockCount() == 0);
    }
    FreeDialogState named;
    const HWND window = ATL::AtlAxCreateDialog(ATL::_AtlBaseModule.GetResourceInstance(), L"OPENATL_CLSID", nullptr, FreeDialogProc, reinterpret_cast<LPARAM>(&named));
    CHECK(window && named.initialized == 1);
    ATL::CWindow container(window); ATL::CComPtr<IWebBrowser2> control;
    CHECK(container.GetDlgControl(701, IID_IWebBrowser2, reinterpret_cast<void**>(&control.p)) == S_OK);
    control.Release(); CHECK(::DestroyWindow(window) && module.GetLockCount() == 0);
    return true;
}
void BrowserPhase(int cycle, const char* phase) noexcept {
    // Keep real IWebBrowser2 admission and its original 25s CTest budget.
    // If a hosted Windows runner stalls inside a synchronous OLE call, log
    // the exact phase before it rather than replacing the real control.
    std::fprintf(stderr, "ACTIVEX_BROWSER cycle=%d phase=%s ticks=%llu\n",
                 cycle, phase, static_cast<unsigned long long>(::GetTickCount64()));
    std::fflush(stderr);
}
bool browser() {
    BrowserPhase(-1, "start");
    ATL::CAtlModule module;
    for (int cycle = 0; cycle < 3; ++cycle) {
        BrowserPhase(cycle, "begin");
        CHECK(module.GetLockCount() == 0);
        struct Parent {
            HWND window = ::CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 320, 200,
                nullptr, nullptr, ATL::_AtlBaseModule.GetModuleInstance(), nullptr);
            ~Parent() { if (window) ::DestroyWindow(window); }
        } parent;
        CHECK(parent.window);
        ATL::CWindow container(parent.window);
        ATL::CAxWindow2 window;
        RECT rect{0, 0, 320, 200};
        CHECK(window.Create(parent.window, &rect, nullptr, WS_CHILD, 0, 1U) != nullptr);
        BrowserPhase(cycle, "window-created");
        ATL::CComPtr<IUnknown> raw;
        CHECK(ATL::AtlAxGetControl(window, &raw) == E_NOINTERFACE && !raw);
        BrowserPhase(cycle, "creating-webbrowser-control");
        CHECK(window.CreateControlLic(L"about:blank") == S_OK);
        BrowserPhase(cycle, "webbrowser-created");
        CHECK(module.GetLockCount() == 1);
        ATL::CComPtr<IWebBrowser2> browser;
        CHECK(container.GetDlgControl(1, IID_IWebBrowser2, reinterpret_cast<void**>(&browser.p)) == S_OK);
        void* missing = reinterpret_cast<void*>(1);
        CHECK(container.GetDlgControl(99, IID_IUnknown, &missing) == E_FAIL && !missing);
        CHECK(container.GetDlgControl(1, EventIID, &missing) == E_NOINTERFACE && !missing);
        CHECK(container.GetDlgControl(1, IID_IUnknown, nullptr) == E_POINTER);
        ATL::CComPtr<IAxWinAmbientDispatch> ambient;
        CHECK(window.QueryHost(&ambient) == S_OK);
        CHECK(ambient->put_DocHostFlags(DOCHOSTUIFLAG_NO3DBORDER | DOCHOSTUIFLAG_THEME) == S_OK);
        ATL::CComPtr<IDocHostUIHandler> ui;
        CHECK(window.QueryHost(&ui) == S_OK);
        DOCHOSTUIINFO info{}; info.cbSize = sizeof(info);
        CHECK(ui->GetHostInfo(&info) == S_OK && (info.dwFlags & DOCHOSTUIFLAG_THEME));
        BrowserSink sink(parent.window);
        CHECK(ATL::AtlAdviseSinkMap(&sink, true) == S_OK);
        ATL::CComVariant url(L"about:blank#openatl"), empty;
        BrowserPhase(cycle, "navigating");
        CHECK(browser->Navigate2(&url, &empty, &empty, &empty, &empty) == S_OK);
        BrowserPhase(cycle, "awaiting-document-complete");
        const ULONGLONG deadline = ::GetTickCount64() + 6000;
        while (!sink.documents && ::GetTickCount64() < deadline) {
            MSG msg{};
            while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { ::TranslateMessage(&msg); ::DispatchMessageW(&msg); }
            ::MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
        }
        CHECK(sink.documents > 0);
        BrowserPhase(cycle, "document-complete");
        ATL::CComPtr<IDispatch> document;
        CHECK(browser->get_Document(&document) == S_OK);
        ATL::CComQIPtr<IHTMLDocument2> html = document;
        CHECK(html.p != nullptr);
        html.Release(); html = document;
        CHECK(html.p != nullptr);
        CHECK(ATL::AtlAdviseSinkMap(&sink, false) == S_OK);
        ATL::CComPtr<IOleObject> object;
        CHECK(browser->QueryInterface(IID_IOleObject, reinterpret_cast<void**>(&object)) == S_OK);
        ATL::CComPtr<IAxWinHostWindow> host;
        CHECK(window.QueryHost(&host) == S_OK);
        HRESULT foreign = S_OK;
        HWND handle = window;
        std::thread reader([&] { ATL::CComPtr<IUnknown> value; foreign = ATL::AtlAxGetControl(handle, &value); });
        reader.join(); CHECK(foreign == RPC_E_WRONG_THREAD);
        BrowserPhase(cycle, "destroying-window");
        CHECK(window.DestroyWindow()); window.m_hWnd = nullptr;
        missing = reinterpret_cast<void*>(1);
        CHECK(container.GetDlgControl(1, IID_IUnknown, &missing) == E_FAIL && !missing);
        BrowserPhase(cycle, "window-destroyed");
        ATL::CComPtr<IOleClientSite> site;
        CHECK(object->GetClientSite(&site) == S_OK && !site);
        raw.Release();
        CHECK(host->QueryControl(IID_IUnknown, reinterpret_cast<void**>(&raw)) == CO_E_OBJNOTCONNECTED && !raw);
        CHECK(module.GetLockCount() == 1);
        BrowserPhase(cycle, "end");
    }
    CHECK(module.GetLockCount() == 0);
    BrowserPhase(-1, "complete");
    return controlDialogs(module);
}
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    bool (*test)() = nullptr;
    if (std::strcmp(argv[1], "events") == 0) test = events;
    else if (std::strcmp(argv[1], "browser") == 0) test = browser;
    else if (std::strcmp(argv[1], "lifecycle") == 0) test = RunHostLifecycleTests;
    else return 2;
    ULONG_PTR token = 0;
    if (::CoGetContextToken(&token) != CO_E_NOTINITIALIZED || !ATL::AtlAxWinInit() ||
        ::CoGetContextToken(&token) != CO_E_NOTINITIALIZED) return 1;
    if (!dialogTemplates() || !plainDialogs() || ::CoGetContextToken(&token) != CO_E_NOTINITIALIZED) return 1;
    HRESULT hr = ::OleInitialize(nullptr); if (FAILED(hr)) return 1;
    bool ok = true;
#if defined(OPENATL_ORIGINAL_COMMAP_CLASSES)
    if (test == events) ok = RunMicrosoftCommapClassTests() && RunMicrosoftSafeArrayTests();
#endif
    if (ok) ok = test();
    ::OleUninitialize();
    ok = ok && ::CoGetContextToken(&token) == CO_E_NOTINITIALIZED;
    if (ok) std::puts("ACTIVEX_OK");
    return ok ? 0 : 1;
}
