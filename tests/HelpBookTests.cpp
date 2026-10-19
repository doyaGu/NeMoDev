#include "ui/help/HelpBook.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

static void Check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
static std::vector<unsigned char> Resource(vdev::HelpBook &book, const std::string &path) {
    std::vector<unsigned char> bytes;
    std::string error;
    Check(book.Read(vdev::HelpBook::Url(path), bytes, error), error.c_str());
    return bytes;
}
static bool PngSize(const std::vector<unsigned char> &bytes, unsigned width, unsigned height) {
    const auto number = [&](size_t at) {
        return (unsigned(bytes[at]) << 24) | (unsigned(bytes[at + 1]) << 16) | (unsigned(bytes[at + 2]) << 8) |
               bytes[at + 3];
    };
    return bytes.size() > 24 && bytes[0] == 137 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G' &&
           number(16) == width && number(20) == height;
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
        Check(topic->html.find("<h3><a name=\"inputs\"") != std::string::npos,
              "Original HTML and named anchor were lost");
        Check(topic->text.find(u8"Caf\u00e9 \u4e2d\u6587") != std::string::npos, "Help UTF-8 text was changed");
        Check(topic->html.find("coords=\"0,0,50,40\"") != std::string::npos &&
                  topic->html.find("coords=\"0,0,100,0,0,80\"") != std::string::npos,
              "Original image maps were lost");
        const auto results = book.Search("EVENT pressed");
        Check(results.size() == 1 && results.front() == topic, "Full-text search did not combine its terms");
        Check(book.Search("parameter").size() == 1, "Inline formatting split a search word");
        Check(book.Search("absent term").empty() && book.Search("").empty(), "Search returned false matches");
        Check(PngSize(Resource(book, "images/diagram.png"), 100, 80), "CHM image case lookup failed");
        std::vector<unsigned char> unchanged{1, 2, 3};
        for (const char *url : {"book://help/../outside.png", "book://help/%2e%2e/outside.png",
                                "https://example.com/a.png", "file:///outside.png"})
            Check(!book.Read(url, unchanged, error) && !error.empty() &&
                      unchanged == std::vector<unsigned char>({1, 2, 3}),
                  "A resource escaped the book or failed read replaced the caller's bytes");
        Check(!book.Find("absent.html"), "Missing topic appeared valid");
        const auto invalid = std::filesystem::u8path(argv[1]) / "invalid";
        std::filesystem::create_directory(invalid);
        {
            std::ofstream file(invalid / "help.ndh", std::ios::binary);
            file << "NDEVHLP5\xff\xff\xff\xff";
        }
        Check(!book.Load(invalid.u8string(), error) && !error.empty(), "Malformed content appeared valid");
        Check(book.Find("topic two.html") == topic, "Failed load replaced the previous book");
        Check(!book.Load((invalid / "missing").u8string(), error), "Missing optional content appeared installed");
        if (argc > 2) {
            vdev::HelpBook original;
            const bool originalLoaded = original.Load(argv[2], error);
            Check(originalLoaded, error.c_str());
            const auto *key = original.Find("behaviors/Controllers/Key Event.html");
            Check(key && key->text.find("Activates one output") != std::string::npos,
                  "Original Building Block description was lost");
            Check(key->text.find("Key State:") != std::string::npos, "Original parameter description was lost");
            Check(original.Find("behaviors/3d transformations/rotate.html"), "Original Rotate help lookup failed");
            Check(!original.Search("parameter links").empty(), "Original full-text index is empty");
            const auto *scene = original.Find("whitepapers/SceneManagement.htm");
            Check(scene, "Original Scene Management help was lost");
            const char *states[] = {"images/active.PNG", "images/inactive.PNG", "images/nothing.png",
                                    "images/reset.png", "images/noreset.png"};
            for (const auto *state : states) {
                Check(scene->html.find(state) != std::string::npos,
                      "Scene state illustration still uses an author's local file");
                Check(!Resource(original, state).empty(), "Recovered Scene state illustration is unavailable");
            }
            const auto *explorer = original.Find("ui/7_schematic/toolbar/2_schematic_explorer.html");
            Check(explorer, "Original Schematic Explorer help was lost");
            Check(explorer->html.find("_recovered/filter_script.png") != std::string::npos &&
                      PngSize(Resource(original, "_recovered/filter_script.png"), 11, 11),
                  "Schematic Script icon was not recovered from the original screenshot");
            const auto *logics = original.Find("behaviors/Logics/_Cat_Logics.html");
            Check(logics && logics->html.find("name=\"groups\"") != std::string::npos,
                  "Groups anchor was not recovered at its existing heading");
            const auto *alphabet = original.Find("behaviors/_Global_BB_Index.html");
            Check(alphabet, "Original alphabetical index was lost");
            for (const char *letter : {"Q", "X", "Y", "Z"}) {
                Check(alphabet->html.find(std::string("name=\"") + letter + "\"") == std::string::npos,
                      "An absent alphabetical section was invented");
                Check(alphabet->html.find(std::string("href=\"#") + letter + "\"") == std::string::npos,
                      "An absent alphabetical section remains clickable");
                Check(alphabet->text.find(letter) != std::string::npos, "Original alphabet label was removed");
            }
            Check(alphabet->html.find("href=\"#R\"") != std::string::npos,
                  "An existing alphabetical section was disabled");
            const auto *schematic = original.Find("ui/7_schematic/0_schematic_index.html");
            Check(schematic, "Original Schematic map was lost");
            Check(schematic->html.find("alt=\"Show/Hide Link Information\"") != std::string::npos,
                  "Malformed Schematic hotspot label was not recovered from the destination title");
            for (const char *attachment :
                 {"import/3dsmax/Max2Virtools.txt", "import/Lightwave/readme.txt", "images/vsl_display_instances.png"})
                Check(original.Find(attachment) && !original.Find(attachment)->html.empty(),
                      "An original linked resource is unreadable");
        }
        std::cout << "Help topics, markup, paths, search and failed loads passed\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
