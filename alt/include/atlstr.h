// OpenATL - Clean-room ATL subset for WTL 10.0
// String compatibility stub

#ifndef __ATLSTR_H__
#define __ATLSTR_H__

#pragma once

// Define the guard so WTL's optional CString code activates
// WTL checks for __ATLSTR_H__ to enable CString-based overloads

#include "atlbase.h"
#include <string>
#include <algorithm>
#include <vector>
#include <cstdarg>
#include <exception>

namespace ATL {

// Minimal CString implementation for WTL compatibility
class CString {
public:
    CString() noexcept {}

    CString(const TCHAR* psz)
    {
        if (psz != NULL)
            m_str = psz;
    }

    CString(const TCHAR* psz, int nLength)
    {
        if (psz != NULL && nLength > 0)
            m_str.assign(psz, nLength);
    }

#ifdef UNICODE
    // Construct from narrow string (ANSI -> UNICODE conversion)
    CString(const char* psz)
    {
        if (psz != nullptr) {
            const HRESULT hr = AssignAnsi(psz);
            if (FAILED(hr)) {
#ifdef _ATL_NO_EXCEPTIONS
                std::terminate();
#else
                AtlThrow(hr);
#endif
            }
        }
    }
#endif

    HRESULT AssignAnsi(std::string_view text) noexcept {
        return OpenATL::Unicode::AnsiToUtf16(text, m_str);
    }
    HRESULT AssignUtf8(std::string_view text) noexcept {
        return OpenATL::Unicode::Utf8ToUtf16(text, m_str);
    }
    HRESULT ToUtf8(std::string& output) const noexcept {
        return OpenATL::Unicode::Utf16ToUtf8(m_str, output);
    }

    CString(const CString& strSrc) : m_str(strSrc.m_str) {}
    CString(CString&& strSrc) noexcept : m_str(std::move(strSrc.m_str)) {}

    CString(TCHAR ch, int nRepeat = 1)
    {
        m_str.assign(nRepeat, ch);
    }

    ~CString() {}

    CString& operator=(const CString& strSrc)
    {
        m_str = strSrc.m_str;
        return *this;
    }

    CString& operator=(CString&& strSrc) noexcept
    {
        m_str = std::move(strSrc.m_str);
        return *this;
    }

    CString& operator=(const TCHAR* psz)
    {
        if (psz != NULL)
            m_str = psz;
        else
            m_str.clear();
        return *this;
    }

    CString& operator=(TCHAR ch)
    {
        m_str = ch;
        return *this;
    }

    operator LPCTSTR() const noexcept { return m_str.c_str(); }

    int GetLength() const noexcept { return (int)m_str.length(); }
    LPCTSTR GetString() const noexcept { return m_str.c_str(); }
    bool IsEmpty() const noexcept { return m_str.empty(); }
    void Empty() { m_str.clear(); }
    void Preallocate(int chars) {
        if (chars > 0) m_str.reserve(static_cast<size_t>(chars));
    }
    void Truncate(int chars) {
        if (chars < 0) chars = 0;
        if (static_cast<size_t>(chars) < m_str.size())
            m_str.resize(static_cast<size_t>(chars));
    }
    void Append(LPCTSTR text, int length = -1) {
        if (!text) return;
        if (length < 0) m_str.append(text);
        else if (length > 0)
            m_str.append(text, static_cast<size_t>(length));
    }
    int Remove(TCHAR character) {
        const size_t previous = m_str.size();
        m_str.erase(std::remove(m_str.begin(), m_str.end(), character), m_str.end());
        return static_cast<int>(previous - m_str.size());
    }

    TCHAR GetAt(int nIndex) const { return m_str.at(nIndex); }
    void SetAt(int nIndex, TCHAR ch) { m_str[nIndex] = ch; }
    TCHAR operator[](int nIndex) const { return m_str[nIndex]; }

    CString& operator+=(const CString& strSrc) { m_str += strSrc.m_str; return *this; }
    CString& operator+=(const TCHAR* psz) { if (psz) m_str += psz; return *this; }
    CString& operator+=(TCHAR ch) { m_str += ch; return *this; }

    friend CString operator+(const CString& a, const CString& b)
    {
        CString s;
        s.m_str = a.m_str + b.m_str;
        return s;
    }

    friend CString operator+(const CString& a, const TCHAR* b)
    {
        CString s;
        s.m_str = a.m_str;
        if (b) s.m_str += b;
        return s;
    }

    friend CString operator+(const TCHAR* a, const CString& b)
    {
        CString s;
        if (a) s.m_str = a;
        s.m_str += b.m_str;
        return s;
    }

    int Compare(const TCHAR* psz) const noexcept
    {
        return _tcscmp(m_str.c_str(), psz ? psz : _T(""));
    }

    int CompareNoCase(const TCHAR* psz) const noexcept
    {
        return _tcsicmp(m_str.c_str(), psz ? psz : _T(""));
    }

    bool operator==(const TCHAR* psz) const { return Compare(psz) == 0; }
    bool operator!=(const TCHAR* psz) const { return Compare(psz) != 0; }
    bool operator<(const TCHAR* psz) const { return Compare(psz) < 0; }
    bool operator>(const TCHAR* psz) const { return Compare(psz) > 0; }

    CString Mid(int first) const { return Mid(first, GetLength()); }
    CString Mid(int first, int count) const {
        if (first < 0) first = 0;
        if (count < 0) count = 0;
        if (static_cast<size_t>(first) >= m_str.size()) return CString();
        const auto part = m_str.substr(static_cast<size_t>(first),
                                        static_cast<size_t>(count));
        return CString(part.data(), static_cast<int>(part.size()));
    }
    CString Left(int count) const { return Mid(0, count); }
    CString Right(int count) const {
        if (count <= 0) return CString();
        if (static_cast<size_t>(count) >= m_str.size()) return *this;
        return Mid(static_cast<int>(m_str.size() - count), count);
    }

    int Find(TCHAR ch, int nStart = 0) const
    {
        auto pos = m_str.find(ch, nStart);
        return pos == std::basic_string<TCHAR>::npos ? -1 : (int)pos;
    }

    int Find(const TCHAR* psz, int nStart = 0) const
    {
        if (psz == NULL) return -1;
        auto pos = m_str.find(psz, nStart);
        return pos == std::basic_string<TCHAR>::npos ? -1 : (int)pos;
    }

    int ReverseFind(TCHAR ch) const
    {
        auto pos = m_str.rfind(ch);
        return pos == std::basic_string<TCHAR>::npos ? -1 : (int)pos;
    }

    int FindOneOf(LPCTSTR pszCharSet) const
    {
        if (!pszCharSet) return -1;
        const auto pos = m_str.find_first_of(pszCharSet);
        return pos == std::basic_string<TCHAR>::npos ? -1 : static_cast<int>(pos);
    }

    CString& MakeUpper() { for (auto& c : m_str) c = (TCHAR)_totupper(c); return *this; }
    CString& MakeLower() { for (auto& c : m_str) c = (TCHAR)_totlower(c); return *this; }

    CString& TrimLeft()
    {
        auto it = m_str.begin();
        while (it != m_str.end() && _istspace(*it)) ++it;
        m_str.erase(m_str.begin(), it);
        return *this;
    }

    CString& TrimRight()
    {
        auto it = m_str.end();
        while (it != m_str.begin() && _istspace(*(it - 1))) --it;
        m_str.erase(it, m_str.end());
        return *this;
    }

    CString& Trim() { TrimLeft(); TrimRight(); return *this; }

    LPTSTR GetBuffer(int nMinBufLength = 0)
    {
        if (nMinBufLength > GetLength())
            m_str.resize(static_cast<size_t>(nMinBufLength));
        return m_str.data(); // C++17: valid even for an empty CString
    }

    void ReleaseBuffer(int nNewLength = -1)
    {
        if (nNewLength >= 0)
            m_str.resize(nNewLength);
        else
            m_str.resize(_tcslen(m_str.c_str()));
    }

    LPTSTR GetBufferSetLength(int nNewLength)
    {
        m_str.resize(static_cast<size_t>(nNewLength < 0 ? 0 : nNewLength));
        return m_str.data();
    }

    void __cdecl Format(const TCHAR* pszFormat, ...)
    {
        va_list args;
        va_start(args, pszFormat);
        FormatV(pszFormat, args);
        va_end(args);
    }

    void FormatV(const TCHAR* pszFormat, va_list args)
    {
        if (!pszFormat) { m_str.clear(); return; }
#if defined(_MSC_VER) && !defined(__clang__)
        va_list measurement = args;
#else
        va_list measurement;
        va_copy(measurement, args);
#endif
        const int length = _vsctprintf(pszFormat, measurement);
#if !defined(_MSC_VER) || defined(__clang__)
        va_end(measurement);
#endif
        if (length < 0) { m_str.clear(); return; }
        std::vector<TCHAR> buffer(static_cast<size_t>(length) + 1);
        const int written = _vstprintf_s(buffer.data(), buffer.size(),
                                        pszFormat, args);
        if (written < 0) { m_str.clear(); return; }
        m_str.assign(buffer.data(), static_cast<size_t>(written));
    }

    // In UNICODE builds Win32's LoadString macro expands this method's name
    // to LoadStringW. Do not declare both: they become duplicate methods.
    // The resource must be copied using its counted UTF-16 length; its
    // representation is not necessarily NUL-terminated and can exceed 255.
    BOOL LoadString(UINT nID)
    {
#ifdef UNICODE
        LPCWSTR resource = nullptr;
        const int length = ::LoadStringW(_AtlBaseModule.GetResourceInstance(),
                                         nID,
                                         reinterpret_cast<LPWSTR>(&resource), 0);
        if (length <= 0 || !resource)
            return FALSE;
        m_str.assign(resource, static_cast<size_t>(length));
        return TRUE;
#else
        TCHAR szBuf[256];
        int nLen = ::LoadString(_AtlBaseModule.GetResourceInstance(), nID, szBuf, 256);
        if (nLen > 0) {
            m_str.assign(szBuf, nLen);
            return TRUE;
        }
        return FALSE;
#endif
    }

    int Replace(const TCHAR* pszOld, const TCHAR* pszNew)
    {
        if (pszOld == NULL || pszNew == NULL) return 0;
        int nCount = 0;
        size_t nOldLen = _tcslen(pszOld);
        if (nOldLen == 0) return 0; // avoid endless empty-pattern replace
        size_t nNewLen = _tcslen(pszNew);
        size_t pos = 0;
        while ((pos = m_str.find(pszOld, pos)) != std::basic_string<TCHAR>::npos) {
            m_str.replace(pos, nOldLen, pszNew);
            pos += nNewLen;
            nCount++;
        }
        return nCount;
    }

    int Replace(TCHAR chOld, TCHAR chNew)
    {
        int nCount = 0;
        for (auto& c : m_str) {
            if (c == chOld) { c = chNew; nCount++; }
        }
        return nCount;
    }

    int Delete(int nIndex, int nCount = 1)
    {
        if (nIndex < 0 || nCount <= 0) return GetLength();
        if (static_cast<size_t>(nIndex) >= m_str.size()) return GetLength();
        m_str.erase(static_cast<size_t>(nIndex), static_cast<size_t>(nCount));
        return GetLength();
    }

    int Insert(int nIndex, TCHAR ch)
    {
        if (nIndex < 0) nIndex = 0;
        if (static_cast<size_t>(nIndex) > m_str.size()) nIndex = GetLength();
        m_str.insert(m_str.begin() + nIndex, ch);
        return GetLength();
    }

    int Insert(int nIndex, const TCHAR* psz)
    {
        if (psz) {
            if (nIndex < 0) nIndex = 0;
            if (static_cast<size_t>(nIndex) > m_str.size()) nIndex = GetLength();
            m_str.insert(static_cast<size_t>(nIndex), psz);
        }
        return GetLength();
    }

private:
    std::basic_string<TCHAR> m_str;
};

} // namespace ATL

#endif // __ATLSTR_H__
