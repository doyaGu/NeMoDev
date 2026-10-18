#include "core/runtime/TextEncoding.h"

#include <SDL3/SDL_stdinc.h>
#include <climits>
#include <cwchar>
#include <utility>
#ifdef _WIN32
#include <windows.h>
#endif

namespace vdev {

static std::function<void(const std::string &)> &LossyHandler() {
    static std::function<void(const std::string &)> handler;
    return handler;
}

#ifdef _WIN32
static std::wstring ToWide(const std::string &text, UINT page, bool replace = false) {
    if (text.empty()) return {};
    if (text.size() > INT_MAX) throw TextEncodingError("Text is too long");
    DWORD flags = replace ? 0 : MB_ERR_INVALID_CHARS;
    int count = MultiByteToWideChar(page, flags, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!count) throw TextEncodingError("Invalid text encoding");
    std::wstring result(static_cast<size_t>(count), L'\0');
    if (MultiByteToWideChar(page, flags, text.data(), static_cast<int>(text.size()), result.data(), count) != count)
        throw TextEncodingError("Text conversion failed");
    return result;
}

static std::string FromWide(const std::wstring &text, UINT page) {
    if (text.empty()) return {};
    if (text.size() > INT_MAX) throw TextEncodingError("Text is too long");
    DWORD flags = page == CP_UTF8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
    BOOL substituted = FALSE;
    BOOL *used = page == CP_UTF8 ? nullptr : &substituted;
    int count = WideCharToMultiByte(page, flags, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, used);
    if (!count || substituted) throw TextEncodingError("Text cannot be represented in the native character set");
    std::string result(static_cast<size_t>(count), '\0');
    if (WideCharToMultiByte(page, flags, text.data(), static_cast<int>(text.size()), result.data(), count, nullptr, used) != count || substituted)
        throw TextEncodingError("Text cannot be represented in the native character set");
    return result;
}
#endif

std::wstring AnsiToWide(const std::string &text) {
#ifdef _WIN32
    try { return ToWide(text, GetACP()); }
    catch (const TextEncodingError &) {
        std::wstring result = ToWide(text, GetACP(), true);
        if (LossyHandler()) LossyHandler()(WideToUtf8(result));
        return result;
    }
#else
    std::wstring result;
    std::mbstate_t state = {};
    bool replaced = false;
    for (size_t at = 0; at < text.size();) {
        wchar_t character = 0;
        size_t count = std::mbrtowc(&character, text.data() + at, text.size() - at, &state);
        if (count == static_cast<size_t>(-1) || count == static_cast<size_t>(-2) ||
            character < 0 || character > 0x10FFFF || (character >= 0xD800 && character <= 0xDFFF)) {
            character = 0xFFFD; count = 1; state = {}; replaced = true;
        }
        result.push_back(character);
        at += count ? count : 1;
    }
    if (replaced && LossyHandler()) LossyHandler()(WideToUtf8(result));
    return result;
#endif
}

std::string WideToAnsi(const std::wstring &text, bool *lossy) {
    if (lossy) *lossy = false;
    try {
#ifdef _WIN32
        return FromWide(text, GetACP());
#else
        std::string result;
        std::mbstate_t state = {};
        for (wchar_t character : text) {
            if (character < 0 || character > 0x10FFFF || (character >= 0xD800 && character <= 0xDFFF))
                throw TextEncodingError("Invalid wide text");
            char bytes[MB_LEN_MAX];
            size_t count = std::wcrtomb(bytes, character, &state);
            if (count == static_cast<size_t>(-1)) throw TextEncodingError("Text cannot be represented in the native character set");
            result.append(bytes, count);
        }
        if (!std::mbsinit(&state)) {
            char bytes[MB_LEN_MAX];
            size_t count = std::wcrtomb(bytes, L'\0', &state);
            if (count == static_cast<size_t>(-1)) throw TextEncodingError("Text conversion failed");
            result.append(bytes, count - 1);
        }
        return result;
#endif
    } catch (const TextEncodingError &) {
        if (lossy) *lossy = true;
        if (LossyHandler()) LossyHandler()(WideToUtf8(text));
        throw;
    }
}

std::string AnsiToUtf8(const std::string &text) {
    return WideToUtf8(AnsiToWide(text));
}

std::string Utf8ToAnsi(const std::string &text, bool *lossy) {
    if (lossy) *lossy = false;
    std::wstring wide;
    try { wide = Utf8ToWide(text); }
    catch (const TextEncodingError &) {
        if (lossy) *lossy = true;
        if (LossyHandler()) LossyHandler()("Invalid UTF-8 text");
        throw;
    }
    return WideToAnsi(wide, lossy);
}

std::string WideToUtf8(const std::wstring &text) {
#ifdef _WIN32
    return FromWide(text, CP_UTF8);
#else
    std::string result;
    for (wchar_t character : text) {
        if (character < 0 || character > 0x10FFFF || (character >= 0xD800 && character <= 0xDFFF))
            throw TextEncodingError("Invalid wide text");
        char bytes[4];
        char *end = SDL_UCS4ToUTF8(static_cast<Uint32>(character), bytes);
        result.append(bytes, end);
    }
    return result;
#endif
}

std::wstring Utf8ToWide(const std::string &text) {
#ifdef _WIN32
    return ToWide(text, CP_UTF8);
#else
    std::wstring result;
    const char *cursor = text.data();
    size_t remaining = text.size();
    while (remaining) {
        if (!*cursor) {
            result.push_back(0); ++cursor; --remaining; continue;
        }
        const char *start = cursor;
        Uint32 character = SDL_StepUTF8(&cursor, &remaining);
        if (character > 0x10FFFF || (character >= 0xD800 && character <= 0xDFFF) ||
            (character == SDL_INVALID_UNICODE_CODEPOINT &&
             (cursor - start != 3 || start[0] != '\xEF' || start[1] != '\xBF' || start[2] != '\xBD')))
            throw TextEncodingError("Invalid UTF-8 text");
        result.push_back(static_cast<wchar_t>(character));
    }
    return result;
#endif
}

void SetLossyTextHandler(std::function<void(const std::string &)> handler) {
    LossyHandler() = std::move(handler);
}

} // namespace vdev
