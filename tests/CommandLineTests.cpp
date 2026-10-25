#include "ui/app/CommandLine.h"
#include "core/runtime/TextEncoding.h"

#include <filesystem>
#include <iostream>

int main() {
    int failures = 0;
    auto check = [&](bool condition, const char *message) {
        if (!condition) { std::cerr << message << '\n'; ++failures; }
    };
    vdev::CommandLine ordinary;
    const std::wstring composition = L"Compositions/\u4e2d\u6587 scene.cmo";
    ordinary.Parse({L"NeMoDev", L"--runtime", L"SDK folder/Bin", composition});
    check(ordinary.badOption.empty(), "Runtime override and composition path must be accepted");
    const auto expected = std::filesystem::current_path() / "Compositions" /
        std::filesystem::u8path(u8"\u4e2d\u6587 scene.cmo");
    check(std::filesystem::u8path(vdev::WideToUtf8(ordinary.fileToOpen)) == expected,
          "The composition path must stay intact");
    for (const wchar_t *option : {L"--capture", L"--ui-tests", L"--tool", L"--play", L"--drop-block", L"--isolated"}) {
        vdev::CommandLine regression;
        regression.Parse({L"NeMoDev", option, L"argument"});
        check(regression.badOption == option, "Production must reject regression options");
    }
    vdev::CommandLine missing;
    missing.Parse({L"NeMoDev", L"--runtime"});
    check(missing.badOption == L"--runtime", "A missing runtime argument must fail");
    return failures ? 1 : 0;
}
