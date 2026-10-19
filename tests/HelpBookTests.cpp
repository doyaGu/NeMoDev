#include "ui/help/HelpBook.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

static void Check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
static std::string Text(const std::vector<vdev::HelpBlock> &blocks) {
    std::string text;
    for (const auto &block : blocks) {
        for (const auto &span : block.spans) text += span.text;
        text += Text(block.children);
    }
    return text;
}
int main(int argc, char **argv) {
    try {
        Check(argc >= 2, "Missing help fixture");
        vdev::HelpBook book;
        std::string error;
        const bool loaded = book.Load(argv[1], error);
        Check(loaded, error.c_str());
        Check(book.Home() == "index.html", "Default topic was lost");
        Check(book.Contents().size() == 1 && book.Contents()[0].topic == "index.html", "Contents were lost");
        const auto *topic = book.Find("TOPIC%20TWO.HTML#inputs");
        Check(topic && topic->title == "Key Event BB", "CHM case or escaped path lookup failed");
        Check(topic->appearance.background == 0xffffffff && topic->blocks[0].appearance.size == 16.0f &&
              (topic->blocks[0].appearance.flags & vdev::HelpAppearance::Bold), "Resolved presentation was lost");
        Check(Text(topic->blocks).find(u8"Caf\u00e9 \u4e2d\u6587") != std::string::npos, "Help UTF-8 text was changed");
        const auto results = book.Search("EVENT pressed");
        Check(results.size() == 1 && results.front() == topic, "Full-text search did not combine its terms");
        Check(book.Search("parameter").size() == 1, "Inline formatting split a search word");
        Check(book.Search("absent term").empty() && book.Search("").empty(), "Search returned false matches");
        const auto image = std::filesystem::u8path(book.ImagePath("images/diagram.png"));
        Check(!image.empty() && std::filesystem::equivalent(image, std::filesystem::u8path(argv[1]) / "images" / "Diagram.PNG"),
              "CHM image case lookup failed");
        Check(book.ImagePath("../outside.png").empty() && book.ImagePath("https://example.com/a.png").empty(), "Help image escaped the book");
        Check(!book.Find("absent.html"), "Missing topic appeared valid");
        const auto invalid = std::filesystem::u8path(argv[1]) / "invalid";
        std::filesystem::create_directory(invalid);
        { std::ofstream file(invalid / "help.ndh", std::ios::binary); file << "NDEVHLP2\xff\xff\xff\xff"; }
        Check(!book.Load(invalid.u8string(), error) && !error.empty(), "Malformed content appeared valid");
        Check(book.Find("topic two.html") == topic, "Failed load replaced the previous book");
        Check(!book.Load((invalid / "missing").u8string(), error), "Missing optional content appeared installed");
        if (argc > 2) {
            vdev::HelpBook original;
            const bool originalLoaded = original.Load(argv[2], error);
            Check(originalLoaded, error.c_str());
            const auto *key = original.Find("behaviors/Controllers/Key Event.html");
            Check(key && Text(key->blocks).find("Activates one output") != std::string::npos, "Original Building Block description was lost");
            Check(Text(key->blocks).find("Key State:") != std::string::npos, "Original parameter description was lost");
            Check(original.Find("behaviors/3d transformations/rotate.html"), "Original Rotate help lookup failed");
            Check(!original.Search("parameter links").empty(), "Original full-text index is empty");
        }
        std::cout << "Help topics, markup, paths, search and failed loads passed\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
