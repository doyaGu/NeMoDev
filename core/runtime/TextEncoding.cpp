#include "core/runtime/TextEncoding.h"
#include "VxWindowFunctions.h"
#include <climits>
#include <cstring>
#include <limits>
#include <utility>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <SDL3/SDL_endian.h>
#include <cerrno>
#include <iconv.h>
#endif

namespace vdev {
namespace {
std::function<void(const std::string &)> &LossyHandler() {
    static std::function<void(const std::string &)> handler;
    return handler;
}
#ifdef _WIN32
UINT CodePage() {
    const std::string name = VxGetTextEncoding();
    if (name == "windows-936") return 936;
    if (name == "windows-1252") return 1252;
    if (name == "UTF-8") return CP_UTF8;
    throw TextEncodingError("Unavailable CK text encoding: " + name);
}
int Length(size_t length) {
    if (length > INT_MAX) throw TextEncodingError("Text is too long to convert");
    return static_cast<int>(length);
}
std::wstring Decode(const std::string &text, UINT codepage, bool displayReplacement = false) {
    if (text.empty()) return {};
    const DWORD flags = displayReplacement ? 0 : MB_ERR_INVALID_CHARS;
    const int size = Length(text.size());
    const int length = MultiByteToWideChar(codepage, flags, text.data(), size, nullptr, 0);
    if (!length) throw TextEncodingError("Invalid text in code page " + std::to_string(codepage));
    std::wstring value(static_cast<size_t>(length), L'\0');
    if (MultiByteToWideChar(codepage, flags, text.data(), size, value.data(), length) != length)
        throw TextEncodingError("Cannot decode text in code page " + std::to_string(codepage));
    return value;
}
std::string Encode(const std::wstring &text, UINT codepage) {
    if (text.empty()) return {};
    const bool unicode = codepage == CP_UTF8;
    const DWORD flags = unicode ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
    BOOL substituted = FALSE;
    const int size = Length(text.size());
    const int length = WideCharToMultiByte(codepage, flags, text.data(), size, nullptr, 0,
                                           nullptr, unicode ? nullptr : &substituted);
    if (!length || substituted)
        throw TextEncodingError("Text cannot be represented in code page " + std::to_string(codepage));
    std::string value(static_cast<size_t>(length), '\0');
    if (WideCharToMultiByte(codepage, flags, text.data(), size, value.data(), length,
                            nullptr, unicode ? nullptr : &substituted) != length || substituted)
        throw TextEncodingError("Text cannot be represented in code page " + std::to_string(codepage));
    return value;
}
#else
const char *LegacyEncoding() {
    const std::string name = VxGetTextEncoding();
    if (name == "windows-936") return "CP936";
    if (name == "windows-1252") return "CP1252";
    if (name == "UTF-8") return "UTF-8";
    throw TextEncodingError("Unavailable CK text encoding: " + name);
}
const char *WideEncoding() {
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
    return sizeof(wchar_t) == 2 ? "UTF-16BE" : "UTF-32BE";
#else
    return sizeof(wchar_t) == 2 ? "UTF-16LE" : "UTF-32LE";
#endif
}
std::string Convert(const char *input, size_t remaining, const char *from, const char *to,
                    bool displayReplacement = false) {
    if (!remaining) return {};
    iconv_t converter = iconv_open(to, from);
    if (converter == reinterpret_cast<iconv_t>(-1))
        throw TextEncodingError(std::string("Unavailable text conversion: ") + from + " to " + to);
    struct Close { iconv_t converter; ~Close() { iconv_close(converter); } } close{converter};
    std::string value(remaining + 16, '\0');
    size_t written = 0;
    char *cursor = const_cast<char *>(input);
    while (remaining) {
        char *output = value.data() + written;
        size_t room = value.size() - written;
        const size_t result = iconv(converter, &cursor, &remaining, &output, &room);
        written = value.size() - room;
        if (result != static_cast<size_t>(-1)) {
            if (result) throw TextEncodingError(std::string("Text cannot be represented in ") + to);
            break;
        }
        if (errno == E2BIG) {
            if (value.size() > value.max_size() / 2) throw TextEncodingError("Converted text is too long");
            value.resize(value.size() * 2);
            continue;
        }
        if (displayReplacement && (errno == EILSEQ || errno == EINVAL)) {
            // Only display decoding calls this mode, and its destination is UTF-8.
            // Keep the original CK bytes at the caller; never use this result for a write.
            if (value.size() - written < 3) value.resize(value.size() * 2);
            value.replace(written, 3, "\xEF\xBF\xBD", 3);
            written += 3;
            ++cursor; --remaining;
            continue;
        }
        throw TextEncodingError(std::string("Invalid or unrepresentable text: ") + from + " to " + to);
    }
    value.resize(written);
    return value;
}
std::string Convert(const std::string &text, const char *from, const char *to, bool replacement = false) {
    return Convert(text.data(), text.size(), from, to, replacement);
}
#endif
}
std::string AnsiToUtf8(const std::string &text) {
    try {
#ifdef _WIN32
        return Encode(Decode(text, CodePage()), CP_UTF8);
#else
        return Convert(text, LegacyEncoding(), "UTF-8");
#endif
    } catch (const TextEncodingError &) {
#ifdef _WIN32
        const std::string display = Encode(Decode(text, CodePage(), true), CP_UTF8);
#else
        const std::string display = Convert(text, LegacyEncoding(), "UTF-8", true);
#endif
        if (LossyHandler()) LossyHandler()(display);
        return display;
    }
}
std::string Utf8ToAnsi(const std::string &text, bool *lossy) {
    if (lossy) *lossy = false;
    try {
#ifdef _WIN32
        return Encode(Decode(text, CP_UTF8), CodePage());
#else
        return Convert(text, "UTF-8", LegacyEncoding());
#endif
    } catch (const TextEncodingError &) {
        if (lossy) *lossy = true;
        if (LossyHandler()) LossyHandler()(text);
        throw; // Reject the write before it reaches CK.
    }
}
std::wstring AnsiToWide(const std::string &text) { return Utf8ToWide(AnsiToUtf8(text)); }
std::string WideToAnsi(const std::wstring &text, bool *lossy) { return Utf8ToAnsi(WideToUtf8(text), lossy); }
std::string WideToUtf8(const std::wstring &text) {
#ifdef _WIN32
    return Encode(text, CP_UTF8);
#else
    return Convert(reinterpret_cast<const char *>(text.data()), text.size() * sizeof(wchar_t), WideEncoding(), "UTF-8");
#endif
}
std::wstring Utf8ToWide(const std::string &text) {
#ifdef _WIN32
    return Decode(text, CP_UTF8);
#else
    const std::string bytes = Convert(text, "UTF-8", WideEncoding());
    std::wstring result(bytes.size() / sizeof(wchar_t), L'\0');
    if (!bytes.empty()) std::memcpy(result.data(), bytes.data(), bytes.size());
    return result;
#endif
}
void SetLossyTextHandler(std::function<void(const std::string &)> handler) { LossyHandler() = std::move(handler); }
} // namespace vdev
