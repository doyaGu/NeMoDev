#ifdef _WIN32
#include "core/runtime/TextEncoding.h"

#include <windows.h>

namespace vdev {

namespace {

std::function<void(const std::string &)> &LossyHandler() {
    static std::function<void(const std::string &)> handler;
    return handler;
}

std::wstring ToWide(const std::string &text, UINT from) {
    if (text.empty()) return std::wstring();
    const int length = MultiByteToWideChar(from, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) return std::wstring();
    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(from, 0, text.data(), static_cast<int>(text.size()), &wide[0], length);
    return wide;
}

std::string FromWide(const std::wstring &wide, UINT to, bool *lossy) {
    if (lossy) *lossy = false;
    if (wide.empty()) return std::string();
    // The ANSI code page: no best fit (another letter, another file), the
    // default character for the others, which tells the loss.
    const DWORD flags = to == CP_UTF8 ? 0 : WC_NO_BEST_FIT_CHARS;
    BOOL usedDefault = FALSE;
    BOOL *used = to == CP_UTF8 ? nullptr : &usedDefault;
    const int length =
        WideCharToMultiByte(to, flags, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) return std::string();
    std::string text(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(to, flags, wide.data(), static_cast<int>(wide.size()), &text[0], length, nullptr, used);
    if (usedDefault) {
        if (lossy) *lossy = true;
        if (LossyHandler()) LossyHandler()(FromWide(wide, CP_UTF8, nullptr));
    }
    return text;
}

} // namespace

std::string AnsiToUtf8(const std::string &text) {
    if (text.empty()) return text;
    const std::wstring wide = ToWide(text, CP_ACP);
    return wide.empty() ? text : FromWide(wide, CP_UTF8, nullptr);
}

std::string Utf8ToAnsi(const std::string &text, bool *lossy) {
    if (lossy) *lossy = false;
    if (text.empty()) return text;
    const std::wstring wide = ToWide(text, CP_UTF8);
    return wide.empty() ? text : FromWide(wide, CP_ACP, lossy); // not UTF-8: kept as it is
}

std::wstring AnsiToWide(const std::string &text) {
    return ToWide(text, CP_ACP);
}

std::string WideToAnsi(const std::wstring &text, bool *lossy) {
    return FromWide(text, CP_ACP, lossy);
}

std::string WideToUtf8(const std::wstring &text) {
    return FromWide(text, CP_UTF8, nullptr);
}

std::wstring Utf8ToWide(const std::string &text) {
    return ToWide(text, CP_UTF8);
}

void SetLossyTextHandler(std::function<void(const std::string &text)> handler) {
    LossyHandler() = std::move(handler);
}

} // namespace vdev

#else
#include "core/runtime/TextEncoding.h"
#include <climits>
#include <cstring>
#include <limits>
#include <utility>
#include <SDL3/SDL_endian.h>
#include <cerrno>
#include <iconv.h>

namespace vdev {
namespace {
std::function<void(const std::string &)> &LossyHandler() {
    static std::function<void(const std::string &)> handler;
    return handler;
}
const char *LegacyEncoding() { return "CP936"; }
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
}
std::string AnsiToUtf8(const std::string &text) {
    try {
        return Convert(text, LegacyEncoding(), "UTF-8");
    } catch (const TextEncodingError &) {
        const std::string display = Convert(text, LegacyEncoding(), "UTF-8", true);
        if (LossyHandler()) LossyHandler()(display);
        return display;
    }
}
std::string Utf8ToAnsi(const std::string &text, bool *lossy) {
    if (lossy) *lossy = false;
    try {
        return Convert(text, "UTF-8", LegacyEncoding());
    } catch (const TextEncodingError &) {
        if (lossy) *lossy = true;
        if (LossyHandler()) LossyHandler()(text);
        throw; // Reject the write before it reaches CK.
    }
}
std::wstring AnsiToWide(const std::string &text) { return Utf8ToWide(AnsiToUtf8(text)); }
std::string WideToAnsi(const std::wstring &text, bool *lossy) { return Utf8ToAnsi(WideToUtf8(text), lossy); }
std::string WideToUtf8(const std::wstring &text) {
    return Convert(reinterpret_cast<const char *>(text.data()), text.size() * sizeof(wchar_t), WideEncoding(), "UTF-8");
}
std::wstring Utf8ToWide(const std::string &text) {
    const std::string bytes = Convert(text, "UTF-8", WideEncoding());
    std::wstring result(bytes.size() / sizeof(wchar_t), L'\0');
    if (!bytes.empty()) std::memcpy(result.data(), bytes.data(), bytes.size());
    return result;
}
void SetLossyTextHandler(std::function<void(const std::string &)> handler) { LossyHandler() = std::move(handler); }
} // namespace vdev

#endif
