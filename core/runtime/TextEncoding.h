#pragma once

#include <functional>
#include <string>
#include <stdexcept>

namespace vdev {

class TextEncodingError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// CK names and parameter strings retain their original bytes. Display and edits
// use the Windows ACP or the current C runtime character set on other platforms.
// The UI and file paths use UTF-8. AnsiToUtf8 reports invalid bytes and replaces
// them only for display; the caller must keep the original CK bytes.
// Utf8ToAnsi rejects unrepresentable or invalid text with TextEncodingError,
// sets lossy when supplied, and reports it to the handler before throwing.
std::string AnsiToUtf8(const std::string &text);
std::string Utf8ToAnsi(const std::string &text, bool *lossy = nullptr);
std::wstring AnsiToWide(const std::string &text);
std::string WideToAnsi(const std::wstring &text, bool *lossy = nullptr);
std::string WideToUtf8(const std::wstring &text);
std::wstring Utf8ToWide(const std::string &text);

// Reports display replacements and rejected writes as UTF-8 for the editor log.
void SetLossyTextHandler(std::function<void(const std::string &text)> handler);

} // namespace vdev
