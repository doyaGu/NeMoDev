// VirtoolsDevTests: the editor's core on the Virtools runtime.
//
// Without arguments, the default suite's cases in order; --case NAME runs
// one, --filter TEXT those whose name contains it, --list names them. A
// failed CHECK stops its case, which is reported, and the next one runs.
// Topology cases: each edit, its undo and its redo must restore the exact CK
// topology (TopologyFingerprint) and display data (InterfaceDataDiff); a
// refused edit must leave both unchanged; no edit may add a display/runtime
// mismatch. The modes on files (--layout, --tools, ..., a sample alone for
// the random edits on a Dev-saved Script, a save, an independent reload and
// a no-edit round trip) are in VirtoolsDevTestSupport.h.

#include "VirtoolsDevTestSupport.h"
#include "VirtoolsRuntimeCheck.h"

using namespace vdevtest;

namespace {

// SKIPPED (VirtoolsRuntimeCheck.h, CTest's SKIP_RETURN_CODE): the runtime or
// a sample is missing (data/ is not all in the repository).

// The default suite's names (VirtoolsDevCases.def), without the runtime for --list.
const char *const CASES[] = {
#define VIRTOOLSDEV_CASE(name) #name,
#include "VirtoolsDevCases.def"
#undef VIRTOOLSDEV_CASE
};

// The modes and how many arguments each takes (0: one or more files).
struct Mode {
    const char *name;
    int argc;
};
const Mode MODES[] = {{"--list", 2},          {"--case", 3},           {"--filter", 3},     {"--fuzz", 4},
                      {"--create-save", 4},   {"--new-merge", 4},      {"--tools", 4},      {"--duplicates", 3},
                      {"--layout-interface", 4}, {"--object-pictures", 4}, {"--file-menu", 4}, {"--links", 0},
                      {"--layout", 4},        {"--layout-objects", 4}, {"--probe", 3},
                      {"--sprite-text-save", 4}, {"--sprite-text-check", 4}};

// No arguments, a sample and an output folder, or a mode with its own:
// anything else (an option mistyped, one missing its argument) is refused,
// not taken for the default suite.
bool ValidArguments(int argc, char **argv) {
    if (argc == 1) return true;
    if (std::strncmp(argv[1], "--", 2) != 0) return argc == 3;
    for (const Mode &mode : MODES)
        if (std::string(argv[1]) == mode.name) return mode.argc ? argc == mode.argc : argc >= 3;
    return false;
}

// CHECK itself: a failed check stops the case with its expression and place.
void Checks() {
    CHECK(1 + 1 == 2);
    bool thrown = false;
    try {
        CHECK(1 + 1 == 3);
    } catch (const CheckFailure &failure) {
        thrown = failure.what.find("1 + 1 == 3") != std::string::npos &&
                 failure.what.find("VirtoolsDevTests.cpp") != std::string::npos;
    }
    CHECK(thrown);
    auto serialized = [](unsigned char poison) {
        std::unique_ptr<CKStateChunk, decltype(&DeleteCKStateChunk)> chunk(CreateCKStateChunk(CKCID_OBJECT, nullptr), &DeleteCKStateChunk);
        chunk->StartWrite();
        std::memset(chunk->LockWriteBuffer(64), poison, 64 * sizeof(int));
        chunk->WriteIdentifier(0x1234);
        char payload[] = {1, 2, 3};
        chunk->WriteString("A");
        chunk->WriteBuffer(sizeof(payload), payload);
        chunk->WriteBufferNoSize(sizeof(payload), payload);
        chunk->CloseChunk();
        chunk->StartRead();
        const char *data = static_cast<const char *>(chunk->LockReadBuffer());
        return std::string(data, chunk->GetDataSize());
    };
    CHECK(serialized(0xA5) == serialized(0x5A)); // Padding must not serialize allocator contents.
    std::cout << "Checks passed\n";
}

// The mode the arguments ask for, else the default suite's cases (suite:
// --case or --filter).
int Run(int argc, char **argv, bool suite, CkRuntimeHost &host, BuildingBlockCatalog &catalog,
        const std::vector<CKGUID> &prototypes) {
    int result = 0;
    if (argc == 4 && std::string(argv[1]) == "--create-save") {
        result = CreateSaveFile(host, argv[2], argv[3]);
    } else if (argc == 4 && std::string(argv[1]) == "--sprite-text-save") {
        result = SpriteTextSaveFile(host, argv[2], argv[3]);
    } else if (argc == 4 && std::string(argv[1]) == "--sprite-text-check") {
        result = SpriteTextCheckFile(host, argv[2], argv[3]);
    } else if (argc == 4 && std::string(argv[1]) == "--new-merge") {
        result = NewMergeFile(host, argv[2], argv[3]);
    } else if (argc == 4 && std::string(argv[1]) == "--tools") {
        result = ToolsFile(host, argv[2], argv[3]);
    } else if (argc == 3 && std::string(argv[1]) == "--duplicates") {
        result = DuplicatesFile(host, argv[2]);
    } else if (argc == 4 && std::string(argv[1]) == "--layout-interface") {
        result = LayoutInterfaceFile(host, argv[2], argv[3]);
    } else if (argc == 4 && std::string(argv[1]) == "--object-pictures") {
        result = ObjectPicturesFile(host, argv[2], argv[3]);
    } else if (argc == 4 && std::string(argv[1]) == "--file-menu") {
        result = FileMenuFile(host, argv[2], argv[3]);
    } else if (argc >= 3 && std::string(argv[1]) == "--links") {
        result = LinksFiles(host, std::vector<std::string>(argv + 2, argv + argc));
    } else if (argc == 4 && std::string(argv[1]) == "--layout") {
        result = LayoutFile(host, argv[2], argv[3]);
    } else if (argc == 4 && std::string(argv[1]) == "--layout-objects") {
        result = LayoutObjectsFile(host, argv[2], argv[3]);
    } else if (argc == 3 && std::string(argv[1]) == "--probe") {
        // Diagnostic: behaviors whose prototype name contains a text
        // (SCHEMATIC_PROBE_NAME), with their locals, inputs and flags.
        Document document(host);
        std::string openError;
        if (!document.Open(argv[2], &openError)) {
            std::cerr << openError << std::endl;
            return 1;
        }
        const char *needle = std::getenv("SCHEMATIC_PROBE_NAME");
        CKContext *context = host.Context();
        const int count = context->GetObjectsCountByClassID(CKCID_BEHAVIOR);
        CK_ID *ids = context->GetObjectsListByClassID(CKCID_BEHAVIOR);
        auto text = [](CKParameter *parameter) {
            std::string value;
            if (!parameter) return value;
            const int size = parameter->GetStringValue(nullptr, FALSE);
            if (size > 0 && size < 100000) {
                value.resize(size);
                parameter->GetStringValue(&value[0], FALSE);
                value.resize(std::strlen(value.c_str()));
            }
            return value.substr(0, 300);
        };
        for (int i = 0; i < count; ++i) {
            CKBehavior *behavior = CKBehavior::Cast(context->GetObject(ids[i]));
            const char *prototype = behavior ? behavior->GetPrototypeName() : nullptr;
            if (!behavior || !prototype || (needle && !std::strstr(prototype, needle))) continue;
            std::cout << "behavior " << behavior->GetID() << " '" << (behavior->GetName() ? behavior->GetName() : "")
                      << "' prototype '" << prototype << "' flags " << std::hex << behavior->GetFlags() << std::dec
                      << std::endl;
            for (int l = 0; l < behavior->GetLocalParameterCount(); ++l) {
                CKParameterLocal *local = behavior->GetLocalParameter(l);
                std::cout << "  local '" << (local->GetName() ? local->GetName() : "") << "' type "
                          << local->GetType() << " setting " << behavior->IsLocalParameterSetting(l) << " = "
                          << text(local) << std::endl;
            }
            for (int l = 0; l < behavior->GetInputParameterCount(); ++l) {
                CKParameterIn *input = behavior->GetInputParameter(l);
                std::cout << "  pin '" << (input->GetName() ? input->GetName() : "") << "' = "
                          << text(input->GetRealSource()) << std::endl;
            }
            for (int l = 0; l < behavior->GetOutputParameterCount(); ++l)
                std::cout << "  pout '" << (behavior->GetOutputParameter(l)->GetName() ? behavior->GetOutputParameter(l)->GetName() : "")
                          << "'" << std::endl;
        }
        document.Close();
    } else if (argc == 4 && std::string(argv[1]) == "--fuzz") {
        // Extra seeds: VirtoolsDevTests --fuzz <seeds> <steps>
        for (int seed = 1; seed <= std::atoi(argv[2]); ++seed)
            SyntheticFuzz(host, prototypes, static_cast<unsigned>(seed), std::atoi(argv[3]));
    } else if (argc == 3 && !suite) {
        result = SampleFile(host, prototypes, argv[1], argv[2]);
    } else {
        // The default suite's cases, in CASES' order (CTest registers each
        // alone, tests/CMakeLists.txt).
        const std::vector<std::pair<std::string, std::function<void()>>> cases = {
            {"Checks", [] { Checks(); }},
            {"TextEncoding", [] { TextEncodingChecks(); }},
            {"NativePaths", [&] { NativePathChecks(host); }},
            {"CompositionReadCompatibility", [&] { CompositionReadCompatibility(host); }},
            {"ObjectFileOpen", [&] { ObjectFileOpen(host); }},
            {"OpenFailurePreservation", [&] { OpenFailurePreservation(host); }},
            {"Resources", [] { ResourceChecks(); }},
            {"NewSceneEnvironment", [&] { NewSceneEnvironment(host); }},
            {"Catalogs", [&] { Catalogs(host.Context()); }},
            {"Edits", [&] { Edits(host); }},
            {"GraveyardCollection", [&] { GraveyardCollection(host); }},
            {"UndoRoundTrips", [&] { UndoRoundTrips(host, prototypes); }},
            {"ViewProjections", [&] { ViewProjections(host); }},
            {"LayoutGestures", [&] { LayoutGestures(host); }},
            {"BatchCommands", [&] { BatchCommands(host, prototypes); }},
            {"ObjectPropertyUndo", [&] { ObjectPropertyUndo(host); }},
            {"ObjectEditFailures", [&] { ObjectEditFailures(host); }},
            {"ObjectEditCancellation", [&] { ObjectEditCancellation(host); }},
            {"SaveFailures", [&] { SaveFailures(host); }},
            {"SaveCallbacks", [&] { SaveCallbacks(host); }},
            {"InitialConditionsScenes", [&] { InitialConditionsScenes(host); }},
            {"InitialConditionsFailures", [&] { InitialConditionsFailures(host); }},
            {"ParameterValueFailures", [&] { ParameterValueFailures(host); }},
            {"CkArrays", [&] { CkArrays(host); }},
            {"SetupActions", [&] { SetupActions(host); }},
            {"DeterministicEdits", [&] { DeterministicEdits(host, prototypes); }},
            {"SyntheticFuzz", [&] { SyntheticFuzz(host, prototypes); }},
            {"ThisParameter", [&] { ThisParameter(host); }},
            {"UnreadableChunks", [&] { UnreadableChunks(host, prototypes); }},
            {"ExportedParameters", [&] { ExportedParameters(host, prototypes); }},
            {"CrossGraphLinks", [&] { CrossGraphLinks(host, prototypes); }},
            {"ReplaceBehaviors", [&] { ReplaceBehaviors(host, prototypes); }},
            {"OperationCommands", [&] { OperationCommands(host); }},
            {"LinkAndExportCommands", [&] { LinkAndExportCommands(host, prototypes); }},
            {"LevelManagerCommands", [&] { LevelManagerCommands(host); }},
            {"SetupManagerCommands", [&] { SetupManagerCommands(host); }},
            {"CreateAndSceneCommands", [&] { CreateAndSceneCommands(host); }},
            {"ScriptDebugger", [&] { ScriptDebugger(host, prototypes); }},
            {"EditorCommands", [&] { EditorCommands(host, prototypes); }},
            {"BuildingBlockCallbacks", [&] { BuildingBlockCallbacks(host, catalog); }},
            {"GuardedCalls", [&] { GuardedCalls(host); }},
            // Last: ClearAll drops the level the others use.
            {"InputViewport", [&] { InputViewport(host); }},
            {"ManagersAfterClearAll", [&] { ManagersAfterClearAll(host); }},
        };
        // The table binds VirtoolsDevCases.def's names, in its order.
        bool listed = cases.size() == std::size(CASES);
        for (size_t i = 0; listed && i < cases.size(); ++i) listed = cases[i].first == CASES[i];
        if (!listed) {
            std::cerr << "the case table and VirtoolsDevCases.def differ\n";
            return 2;
        }
        const std::string pattern = suite && argc == 3 ? argv[2] : "";
        const bool exact = suite && std::string(argv[1]) == "--case";
        int ran = 0;
        std::vector<std::string> failed;
        for (const auto &entry : cases) {
            if (!pattern.empty() && (exact ? entry.first != pattern : entry.first.find(pattern) == std::string::npos))
                continue;
            ++ran;
            try {
                entry.second();
            } catch (const CheckFailure &failure) {
                std::cerr << "FAILED " << entry.first << ": " << failure.what << '\n';
                failed.push_back(entry.first);
            }
        }
        if (!ran) {
            std::cerr << "no case " << pattern << '\n';
            result = 2;
        } else if (!failed.empty()) {
            std::cerr << failed.size() << " of " << ran << " case(s) failed\n";
            result = 1;
        } else {
            std::cout << (pattern.empty() ? std::string("VirtoolsDevTests") : pattern) << " passed (" << ran
                      << " case(s))\n";
        }
    }
    return result;
}

} // namespace

int main(int argc, char **argv) {
    if (!ValidArguments(argc, argv)) {
        std::cerr << "usage: VirtoolsDevTests [--list | --case NAME | --filter TEXT | --fuzz SEEDS STEPS | SAMPLE OUTPUT"
                     " | --links FILE... | --duplicates FILE | --probe FILE | --MODE FILE OUTPUT]\n";
        return 2;
    }
    if (argc == 2 && std::string(argv[1]) == "--list") {
        for (const char *name : CASES) std::cout << name << '\n';
        return 0;
    }
    // The samples of the modes: argv[2] on (--links takes several; the last
    // of the others is an output folder), argv[1] for a sample alone.
    std::vector<std::string> samples;
    const bool option = argc >= 2 && std::strncmp(argv[1], "--", 2) == 0;
    const bool suite = option && (std::string(argv[1]) == "--case" || std::string(argv[1]) == "--filter");
    if (option && argc >= 3 && std::string(argv[1]) != "--fuzz" && !suite)
        samples.assign(argv + 2, argv + (std::string(argv[1]) == "--links" || argc == 3 ? argc : argc - 1));
    else if (!option && argc == 3)
        samples.push_back(argv[1]);
    for (const std::string &sample : samples)
        if (!std::filesystem::exists(sample)) {
            std::cerr << "skipped: the sample " << sample << " is missing\n";
            return SKIPPED;
        }
    std::string error;
    if (!RuntimeAvailable(error)) {
        std::cerr << "skipped: " << error << '\n';
        return SKIPPED;
    }
    CkRuntimeHost host;
    if (!host.Start(RuntimeDirectory(), [](LogLevel level, const std::string &message) {
            if (level == LogLevel::Error) std::cerr << message << '\n';
        }, &error)) {
        std::cerr << "skipped: " << error << '\n';
        return SKIPPED;
    }
    std::cout << "CK version 0x" << std::hex << CKGetVersion() << " (SDK 0x" << CKVERSION << ")" << std::dec << '\n';
    // CKContext::CopyObjects (paste) needs a current level; a loaded file has one.
    if (!host.Context()->GetCurrentLevel())
        host.Context()->SetCurrentLevel(CKLevel::Cast(host.Context()->CreateObject(CKCID_LEVEL, (CKSTRING) "Level")));
    BuildingBlockCatalog catalog;
    catalog.Refresh();
    const std::vector<CKGUID> prototypes = LogicPrototypes(catalog, 8);
    if (prototypes.size() < 2) {
        std::cerr << "fewer than 2 logic Building Blocks: the runtime's BuildingBlocks are missing\n";
        return 1;
    }

    int result = 0;
    try {
        result = Run(argc, argv, suite, host, catalog, prototypes);
    } catch (const CheckFailure &failure) {
        // A mode on a file.
        std::cerr << "FAILED: " << failure.what << '\n';
        result = 1;
    }
    host.Stop();
    return result;
}
