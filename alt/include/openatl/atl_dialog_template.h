// OpenWTL: bounded Win32 dialog-template conversion, based on the documented
// standard/extended resource wire formats. No resource registration or COM init.
#ifndef OPENATL_ATL_DIALOG_TEMPLATE_H
#define OPENATL_ATL_DIALOG_TEMPLATE_H
#pragma once
#include <windows.h>
#include <objbase.h>
#include <cstring>
#include <vector>
#include <new>
#include <stdexcept>
#include <utility>
#include <string>

namespace ATL { namespace detail {
struct AxDialogInit {
    WORD id = 0;
    std::wstring name, license;
    std::vector<BYTE> stream;
};
struct AxDialogTemplate {
    std::vector<DWORD> storage; // Native dialog templates require DWORD alignment.
    size_t size = 0;
    bool converted = false;
    std::vector<AxDialogInit> initialization;
    const DLGTEMPLATE* Get() const noexcept { return reinterpret_cast<const DLGTEMPLATE*>(storage.data()); }
};
class DialogTemplateReader {
public:
    struct Field { size_t begin = 0, end = 0; bool ordinal = false; };
    const BYTE* bytes;
    size_t size, position = 0;
    DialogTemplateReader(const void* data, size_t count) noexcept : bytes(static_cast<const BYTE*>(data)), size(count) {}
    bool Skip(size_t count) noexcept {
        if (count > size - position) return false;
        position += count; return true;
    }
    template<class T> bool Read(T& value) noexcept {
        if (sizeof(T) > size - position) return false;
        std::memcpy(&value, bytes + position, sizeof(T)); position += sizeof(T); return true;
    }
    bool Align() noexcept { return Skip((4 - (position & 3)) & 3); }
    bool Array(Field& field, bool ordinal = true) noexcept {
        field = {}; field.begin = position;
        WORD word = 0;
        if (!Read(word)) return false;
        if (ordinal && word == 0xFFFF) { field.ordinal = true; if (!Read(word)) return false; }
        else while (word) { if (!Read(word)) return false; }
        field.end = position; return true;
    }
    bool Equals(const Field& field, LPCWSTR text, size_t count) const noexcept {
        if (field.ordinal || field.end - field.begin != (count + 1) * sizeof(WCHAR)) return false;
        return ::CompareStringOrdinal(reinterpret_cast<LPCWSTR>(bytes + field.begin), static_cast<int>(count),
            text, static_cast<int>(count), TRUE) == CSTR_EQUAL;
    }
};
inline DWORD BuildAxDialogTemplate(const void* data, size_t size, AxDialogTemplate& output,
    const void* initData = nullptr, size_t initSize = 0) noexcept {
    output = {};
    if (!data) return ERROR_INVALID_DATA;
    try {
        std::vector<AxDialogInit> initialization;
        if (initData || initSize) {
            if (!initData) return ERROR_INVALID_DATA;
            DialogTemplateReader init(initData, initSize);
            for (;;) {
                WORD id = 0, message = 0; DWORD length = 0, characters = 0;
                if (!init.Read(id)) return ERROR_INVALID_DATA;
                if (!id) {
                    // Resource compilers may retain up to three padding bytes.
                    if (init.size - init.position > 3) return ERROR_INVALID_DATA;
                    while (init.position < init.size) { BYTE pad = 0; if (!init.Read(pad) || pad) return ERROR_INVALID_DATA; }
                    break;
                }
                if (!init.Read(message) || !init.Read(length) || length > init.size - init.position) return ERROR_INVALID_DATA;
                if (message != 0x0376) return ERROR_NOT_SUPPORTED; // Stream persistence; storage/binding is separate.
                for (const auto& previous : initialization) if (previous.id == id) return ERROR_INVALID_DATA;
                DialogTemplateReader payload(init.bytes + init.position, length);
                if (!payload.Read(characters) || characters > (payload.size - payload.position) / sizeof(WCHAR)) return ERROR_INVALID_DATA;
                AxDialogInit record; record.id = id;
                record.license.resize(characters);
                if (characters) std::memcpy(&record.license[0], payload.bytes + payload.position, characters * sizeof(WCHAR));
                payload.position += characters * sizeof(WCHAR);
                record.stream.assign(payload.bytes + payload.position, payload.bytes + payload.size);
                initialization.push_back(std::move(record));
                init.position += length;
            }
        }
        std::vector<bool> matched(initialization.size(), false);
        std::vector<AxDialogInit> orderedInitialization;
        DialogTemplateReader reader(data, size);
        WORD version = 0, signature = 0, count = 0;
        DWORD style = 0;
        if (!reader.Read(version) || !reader.Read(signature)) return ERROR_INVALID_DATA;
        const bool extended = signature == 0xFFFF;
        if (extended) {
            if (version != 1 || !reader.Skip(8) || !reader.Read(style)) return ERROR_INVALID_DATA;
        } else {
            reader.position = 0;
            if (!reader.Read(style) || !reader.Skip(4)) return ERROR_INVALID_DATA;
        }
        if (!reader.Read(count) || !reader.Skip(8)) return ERROR_INVALID_DATA;
        if (style & DS_NOFAILCREATE) return ERROR_NOT_SUPPORTED;
        DialogTemplateReader::Field menu, windowClass, title, font;
        if (!reader.Array(menu) || !reader.Array(windowClass) || !reader.Array(title, false)) return ERROR_INVALID_DATA;
        if ((style & DS_SETFONT) && (!reader.Skip(extended ? 6 : 2) || !reader.Array(font, false))) return ERROR_INVALID_DATA;
        std::vector<BYTE> result(reader.bytes, reader.bytes + reader.position);
        bool converted = false;
        for (WORD n = 0; n < count; ++n) {
            if (!reader.Align()) return ERROR_INVALID_DATA;
            const size_t start = reader.position;
            if (!reader.Skip(extended ? 24 : 18)) return ERROR_INVALID_DATA;
            DialogTemplateReader::Field itemClass, itemTitle;
            if (!reader.Array(itemClass) || !reader.Array(itemTitle)) return ERROR_INVALID_DATA;
            WORD extra = 0;
            if (!reader.Read(extra)) return ERROR_INVALID_DATA;
            // Standard creation-data sizes include the size word; extended
            // extraCount is the number of bytes following that word.
            if (extra && ((!extended && extra < 2) || !reader.Skip(extended ? extra : extra - 2))) return ERROR_INVALID_DATA;
            WORD first = 0;
            std::memcpy(&first, reader.bytes + itemClass.begin, sizeof(first));
            const bool clsidClass = !itemClass.ordinal && first == L'{';
            const bool axClass = reader.Equals(itemClass, L"AtlAxWin140", 11) || reader.Equals(itemClass, L"AtlAxWinLic140", 14);
            if ((clsidClass || axClass) && extra) return ERROR_NOT_SUPPORTED;
            DWORD id = 0;
            if (extended) std::memcpy(&id, reader.bytes + start + 20, sizeof(id));
            else { WORD shortId = 0; std::memcpy(&shortId, reader.bytes + start + 16, sizeof(shortId)); id = shortId; }
            AxDialogInit* record = nullptr;
            for (size_t i = 0; i < initialization.size(); ++i) if (initialization[i].id == id) {
                if (matched[i] || (!clsidClass && !axClass)) return ERROR_INVALID_DATA;
                matched[i] = true; record = &initialization[i]; break;
            }
            if (result.size() > size_t(-1) - 3) return ERROR_NOT_ENOUGH_MEMORY;
            result.resize((result.size() + 3) & ~size_t(3), 0);
            if (!clsidClass && !record) {
                result.insert(result.end(), reader.bytes + start, reader.bytes + reader.position);
                continue;
            }
            if (clsidClass) {
                WCHAR clsid[39]{}; CLSID parsed{};
                if (itemClass.end - itemClass.begin != sizeof(clsid)) return ERROR_INVALID_DATA;
                std::memcpy(clsid, reader.bytes + itemClass.begin, sizeof(clsid));
                if (clsid[37] != L'}' || ::CLSIDFromString(clsid, &parsed) != S_OK) return ERROR_INVALID_DATA;
                if (itemTitle.ordinal || itemTitle.end - itemTitle.begin != sizeof(WORD)) return ERROR_NOT_SUPPORTED;
            }
            static constexpr WCHAR host[] = L"AtlAxWin140";
            result.insert(result.end(), reader.bytes + start, reader.bytes + itemClass.begin);
            const auto hostBytes = reinterpret_cast<const BYTE*>(host);
            result.insert(result.end(), hostBytes, hostBytes + sizeof(host));
            if (record) {
                const auto& name = clsidClass ? itemClass : itemTitle;
                if (name.ordinal || name.end - name.begin <= sizeof(WORD)) return ERROR_INVALID_DATA;
                record->name.resize((name.end - name.begin) / sizeof(WCHAR) - 1);
                std::memcpy(&record->name[0], reader.bytes + name.begin, record->name.size() * sizeof(WCHAR));
                orderedInitialization.push_back(std::move(*record));
                result.push_back(0); result.push_back(0); // Construct a blank host; load before user WM_INITDIALOG.
            } else result.insert(result.end(), reader.bytes + itemClass.begin, reader.bytes + itemClass.end);
            result.push_back(0); result.push_back(0); // No inline initialization.
            converted = true;
        }
        for (bool found : matched) if (!found) return ERROR_INVALID_DATA;
        result.insert(result.end(), reader.bytes + reader.position, reader.bytes + size);
        // Unchanged resources retain their exact bytes, including padding.
        const BYTE* finalBytes = converted ? result.data() : reader.bytes;
        const size_t finalSize = converted ? result.size() : size;
        if (finalSize > size_t(-1) - 3) return ERROR_NOT_ENOUGH_MEMORY;
        AxDialogTemplate candidate;
        candidate.storage.resize((finalSize + 3) / 4);
        std::memcpy(candidate.storage.data(), finalBytes, finalSize);
        candidate.size = finalSize; candidate.converted = converted;
        candidate.initialization = std::move(orderedInitialization);
        output = std::move(candidate); return ERROR_SUCCESS;
    } catch (const std::bad_alloc&) { return ERROR_NOT_ENOUGH_MEMORY; }
      catch (const std::length_error&) { return ERROR_NOT_ENOUGH_MEMORY; }
      catch (...) { return ERROR_INVALID_DATA; }
}
} }
#endif
