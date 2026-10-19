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
static const vdev::HelpSpan *Image(const std::vector<vdev::HelpBlock> &blocks) {
    for (const auto &block : blocks) {
        for (const auto &span : block.spans) if (!span.image.empty()) return &span;
        if (const auto *image = Image(block.children)) return image;
    }
    return nullptr;
}
static void Images(const std::vector<vdev::HelpBlock> &blocks, std::vector<const vdev::HelpSpan *> &images) {
    for (const auto &block : blocks) {
        for (const auto &span : block.spans) if (!span.image.empty()) images.push_back(&span);
        Images(block.children, images);
    }
}
static bool HasAnchor(const std::vector<vdev::HelpBlock> &blocks, const std::string &name) {
    for (const auto &block : blocks) {
        for (const auto &span : block.spans) if (span.anchor == name) return true;
        if (HasAnchor(block.children, name)) return true;
    }
    return false;
}
static bool HasLink(const std::vector<vdev::HelpBlock> &blocks, const std::string &link) {
    for (const auto &block : blocks) {
        for (const auto &span : block.spans) if (span.link == link) return true;
        if (HasLink(block.children, link)) return true;
    }
    return false;
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
        const auto *diagram = Image(topic->blocks);
        Check(diagram && diagram->imageAreas.size() == 2 && diagram->imageWidth == 100 && diagram->imageHeight == 80,
              "Image dimensions or hotspots were lost");
        Check(diagram->imageAreas[0].Contains(.25f, .25f) && !diagram->imageAreas[0].Contains(.75f, .25f), "Rectangle hit testing failed");
        Check(diagram->imageAreas[1].Contains(.1f, .1f) && !diagram->imageAreas[1].Contains(.9f, .9f), "Polygon hit testing used its bounding box");
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
        { std::ofstream file(invalid / "help.ndh", std::ios::binary); file << "NDEVHLP3\xff\xff\xff\xff"; }
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
            const auto *scene = original.Find("whitepapers/SceneManagement.htm");
            Check(scene, "Original Scene Management help was lost");
            std::vector<const vdev::HelpSpan *> images;
            Images(scene->blocks, images);
            const char *states[] = {"images/active.PNG", "images/inactive.PNG", "images/nothing.png", "images/reset.png", "images/noreset.png"};
            Check(images.size() == 5, "Scene state illustrations were lost");
            for (size_t i = 0; i < images.size(); ++i) {
                Check(images[i]->image == states[i], "Scene state illustration still uses an author's local file");
                Check(images[i]->imageWidth > 0 && images[i]->imageHeight > 0 && !original.ImagePath(images[i]->image).empty(),
                      "Recovered Scene state illustration is unavailable");
            }
            const auto *explorer = original.Find("ui/7_schematic/toolbar/2_schematic_explorer.html");
            Check(explorer, "Original Schematic Explorer help was lost");
            images.clear();
            Images(explorer->blocks, images);
            bool script = false;
            for (const auto *item : images) if (item->image == "_recovered/filter_script.png") {
                script = item->imageWidth == 11 && item->imageHeight == 11 && !original.ImagePath(item->image).empty();
            }
            Check(script, "Schematic Script icon was not recovered from the original screenshot");
            const auto *logics = original.Find("behaviors/Logics/_Cat_Logics.html");
            Check(logics && HasAnchor(logics->blocks, "groups"), "Groups anchor was not recovered at its existing heading");
            const auto *alphabet = original.Find("behaviors/_Global_BB_Index.html");
            Check(alphabet, "Original alphabetical index was lost");
            for (const char *letter : {"Q", "X", "Y", "Z"}) {
                Check(!HasAnchor(alphabet->blocks, letter), "An absent alphabetical section was invented");
                Check(!HasLink(alphabet->blocks, std::string("behaviors/_Global_BB_Index.html#") + letter), "An absent alphabetical section remains clickable");
                Check(Text(alphabet->blocks).find(letter) != std::string::npos, "Original alphabet label was removed");
            }
            Check(HasLink(alphabet->blocks, "behaviors/_Global_BB_Index.html#R"), "An existing alphabetical section was disabled");
            const auto *schematic = original.Find("ui/7_schematic/0_schematic_index.html");
            Check(schematic, "Original Schematic map was lost");
            images.clear();
            Images(schematic->blocks, images);
            bool information = false;
            for (const auto *item : images) if (item->image == "images/schematic_map.png") {
                Check(item->imageAreas.size() == 35, "Valid Schematic hotspots were lost or an invalid polygon became active");
                for (const auto &area : item->imageAreas)
                    if (area.link == "ui/7_schematic/toolbar/9_sh_links_informations.html")
                        information = area.title == "Show/Hide Link Information";
            }
            Check(information, "Malformed Schematic hotspot label was not recovered from the destination title");
        }
        std::cout << "Help topics, markup, paths, search and failed loads passed\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
