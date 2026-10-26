// The capture mode's startup actions (StartupOptions, CommandLine.h): what
// the command line asks done once the composition is open, and in the first
// frames. None of it runs in an ordinary session.
#ifndef VIRTOOLSDEV_UI_TESTS
#error Regression startup actions must only be compiled in the UI test executable.
#endif
#include "ui/app/EditorApp.h"
#include "ui/schematic/Snapshots.h"


#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>

#include "imgui.h"
#include "imgui_internal.h"

#include "ui/controls/DevBitmaps.h"
#include "ui/controls/DevTheme.h"
#include "core/document/DocumentSaver.h"
#include "core/document/FileCommands.h"
#include "ui/controls/FileDialogs.h"
#include "core/document/ObjectPictures.h"
#include "core/schematic/SchematicEdits.h"
#include "core/runtime/TextEncoding.h"

namespace vdev {

void EditorApp::RunStartupActions(bool opened) {
    if (!m_Startup.merges.empty() || !m_Startup.imports.empty()) {
        for (const auto &merge : m_Startup.merges) MergeComposition(merge.second, merge.first);
        for (const auto &import : m_Startup.imports)
            ImportFile(static_cast<Document::ImportMode>(import.second), import.first);
        m_Startup.merges.clear();
        m_Startup.imports.clear();
        m_StartupAfterLoads = opened ? 2 : 1;
        return;
    }
    for (const std::string &tool : m_Startup.tools) OpenStartupTool(tool);
    if (!m_Startup.layoutTool.empty() || !m_Startup.layoutView.empty())
        m_Layout.StartupOptions(m_Startup.layoutTool, m_Startup.layoutView);
    m_Layout.debugData = m_Startup.layoutDebug;
    if (!m_Startup.dropBlock.empty()) {
        const BuildingBlockEntry *found = nullptr;
        for (const auto &entry : m_State.catalog.Entries())
            if (entry.name == m_Startup.dropBlock) found = &entry;
        if (found) m_Layout.StartupDrop(found->guid, m_Startup.dropX, m_Startup.dropY);
        else m_State.Log(LogLevel::Error, "--drop-block: unknown Building Block " + m_Startup.dropBlock);
    }
    if (opened || m_Startup.newComposition) {
        for (CK_ID script : m_State.visibleScripts) {
            if (m_Startup.openScripts)
                if (ScriptSession *session = m_State.document->Session(script)) session->SetOpen(true);
            if (m_Startup.expandGraphs)
                EditScriptData(*m_State.document, script, "Expand graphs", [](InterfaceData &data) {
                    for (auto &record : data.behaviors)
                        if (record.isBehaviorGraph) SetBehaviorFolded(data, record.id, false);
                });
        }
        if (!m_Startup.insertBlock.empty() && !m_State.visibleScripts.empty()) InsertStartupBlock();
        if (m_Startup.fit) m_FitFrames = 3;
        for (const auto &script : m_State.document->Scripts()) {
            const std::string name = AnsiToUtf8(script.name);
            for (const auto &closed : m_Startup.closeScripts)
                if (closed == name)
                    if (ScriptSession *session = m_State.document->Session(script.id)) session->SetOpen(false);
            for (const auto &zoom : m_Startup.zoomScripts)
                if (zoom.first == name) m_Canvas.SetScriptZoom(script.id, zoom.second);
        }
        if (m_Startup.messages) m_Canvas.ShowMessageExplorer();
        if (m_Startup.expandLevel) m_Panels.ExpandLevelTree();
        for (const auto &name : m_Startup.creates) {
            static const std::map<std::string, LayoutObject> kinds = {
                {"camera", LayoutObject::Camera}, {"light", LayoutObject::Light}, {"frame", LayoutObject::Frame3D},
                {"curve", LayoutObject::Curve}, {"grid", LayoutObject::Grid}, {"2d", LayoutObject::Frame2D},
                {"material", LayoutObject::Material}, {"texture", LayoutObject::Texture}, {"portal", LayoutObject::Portal}};
            auto kind = kinds.find(name);
            if (kind != kinds.end()) CreateObject(kind->second);
            else m_State.Log(LogLevel::Warning, "--create: unknown kind " + name);
        }
        // After --create: the objects made can be selected.
        if (!m_Startup.layoutSelect.empty() || !m_Startup.layoutGroup.empty() || !m_Startup.layoutCommands.empty())
            m_Layout.StartupSelection(m_Startup.layoutSelect, m_Startup.layoutGroup, m_Startup.layoutCommands);
        for (const auto &name : m_Startup.setups) {
            CKContext *context = m_State.document->Context();
            // "@attributes", "@hierarchy", "@addattribute": the managers.
            if (name == "@attributes") {
                m_State.showAttributeManager = true;
                continue;
            }
            if (name == "@hierarchy") {
                m_State.showHierarchy = true;
                continue;
            }
            // "@attributepage": the Setups after it open on their Attribute page.
            if (name == "@attributepage") {
                m_Setups.attributesFirst = true;
                continue;
            }
            if (name == "@addattribute") {
                m_Managers.RequestAddAttribute({}, true);
                continue;
            }
            CKObject *object = context->GetObjectByName(const_cast<CKSTRING>(name.c_str()));
            // "#grid", "#camera", ...: the first object of that class.
            static const std::map<std::string, CK_CLASSID> classes = {
                {"#grid", CKCID_GRID}, {"#camera", CKCID_CAMERA}, {"#light", CKCID_LIGHT}, {"#material", CKCID_MATERIAL},
                {"#texture", CKCID_TEXTURE}, {"#mesh", CKCID_MESH}, {"#2d", CKCID_2DENTITY}, {"#3d", CKCID_3DOBJECT},
                {"#curve", CKCID_CURVE}, {"#character", CKCID_CHARACTER}, {"#sound", CKCID_WAVESOUND},
                {"#array", CKCID_DATAARRAY}, {"#group", CKCID_GROUP}, {"#level", CKCID_LEVEL}, {"#scene", CKCID_SCENE},
                {"#sprite3d", CKCID_SPRITE3D}, {"#frame", CKCID_3DENTITY}, {"#sprite", CKCID_SPRITE},
                {"#place", CKCID_PLACE}, {"#bodypart", CKCID_BODYPART}, {"#portal", CKCID_3DENTITY},
                {"#animation", CKCID_KEYEDANIMATION}, {"#objectanimation", CKCID_OBJECTANIMATION},
                {"#midi", CKCID_MIDISOUND}, {"#spritetext", CKCID_SPRITETEXT}, {"#patchmesh", CKCID_PATCHMESH},
                {"#curvepoint", CKCID_CURVEPOINT}, {"#kinematicchain", CKCID_KINEMATICCHAIN}, {"#layer", CKCID_LAYER},
                {"#parameter", CKCID_PARAMETERLOCAL}, {"#parameterout", CKCID_PARAMETEROUT},
                {"#parameterin", CKCID_PARAMETERIN}, {"#operation", CKCID_PARAMETEROPERATION},
                {"#behavior", CKCID_BEHAVIOR}, {"#behaviorio", CKCID_BEHAVIORIO}, {"#behaviorlink", CKCID_BEHAVIORLINK},
                {"#synchro", CKCID_SYNCHRO}, {"#state", CKCID_STATE}, {"#criticalsection", CKCID_CRITICALSECTION},
                {"#interfaceobjects", CKCID_INTERFACEOBJECTMANAGER}};
            auto cid = classes.find(name);
            if (!object && cid != classes.end()) {
                const XObjectPointerArray &list = context->GetObjectListByType(cid->second, cid->second == CKCID_3DENTITY ? FALSE : TRUE);
                // #frame: plain 3D entities; #portal: those flagged as portals.
                for (int i = 0; i < list.Size() && !object; ++i)
                    if (list[i] && !list[i]->IsPrivate() &&
                        (cid->first != "#portal" || (CK3dEntity::Cast(list[i])->GetFlags() & CK_3DENTITY_PORTAL)) &&
                        (cid->first != "#frame" || !(CK3dEntity::Cast(list[i])->GetFlags() & CK_3DENTITY_PORTAL)))
                        object = list[i];
            }
            if (object)
                m_Setups.Open(object->GetID());
            else
                m_State.Log(LogLevel::Warning, "--setup: no object named " + AnsiToUtf8(name));
        }
        if (m_Startup.playFrames > 0 && m_State.debugger) {
            m_State.showDebugger = true;
            m_State.debugger->trace = true;
            m_State.debugger->maxFrames = m_Startup.playFrames;
            m_State.debugger->Play();
        }
        if (m_Startup.menuAt) m_Canvas.RequestMenuAt(ImVec2(m_Startup.menuX, m_Startup.menuY));
        if (!m_Startup.editParameters.empty()) {
            // The first behavior or operation of that name, in a Script.
            CKContext *context = m_State.document->Context();
            std::function<bool(CKBehavior *, CK_ID)> find = [&](CKBehavior *graph, CK_ID script) {
                for (int i = 0; i < graph->GetSubBehaviorCount(); ++i) {
                    CKBehavior *child = graph->GetSubBehavior(i);
                    if (!child) continue;
                    if (child->GetName() && m_Startup.editParameters == child->GetName()) {
                        m_StartupParametersScript = script;
                        m_StartupParameters = child->GetID();
                        return true;
                    }
                    if (find(child, script)) return true;
                }
                for (int i = 0; i < graph->GetParameterOperationCount(); ++i) {
                    CKParameterOperation *operation = graph->GetParameterOperation(i);
                    if (operation && operation->GetName() && m_Startup.editParameters == operation->GetName()) {
                        m_StartupParametersScript = script;
                        m_StartupParameters = operation->GetID();
                        return true;
                    }
                }
                return false;
            };
            bool found = false;
            for (const auto &script : m_State.document->Scripts())
                if (CKBehavior *behavior = CKBehavior::Cast(context->GetObject(script.id)))
                    if ((found = find(behavior, script.id))) break;
            if (!found) m_State.Log(LogLevel::Error, "--edit-parameters: nothing named " + AnsiToUtf8(m_Startup.editParameters));
        }
        if (!m_Startup.showScript.empty()) {
            bool found = false;
            for (const auto &script : m_State.document->Scripts()) {
                if (AnsiToUtf8(script.name) != m_Startup.showScript) continue;
                m_Panels.ShowScript(script.id);
                found = true;
                break;
            }
            if (!found) m_State.Log(LogLevel::Error, "--show-script: no Script named " + m_Startup.showScript);
        }
    }
}

// --insert-block: exercises the topology commands for --capture checks.
void EditorApp::InsertStartupBlock() {
    const CK_ID script = m_State.visibleScripts.front();
    const BuildingBlockEntry *found = nullptr;
    for (const auto &entry : m_State.catalog.Entries())
        if (entry.name == m_Startup.insertBlock) found = &entry;
    ScriptSession *session = m_State.document->Session(script);
    if (!found || !session) {
        m_State.Log(LogLevel::Error, "--insert-block: unknown Building Block " + m_Startup.insertBlock);
        return;
    }
    const InterfaceData &data = session->Data();
    const Point position(data.header.hStartPos + 120.0f, data.rootBehavior.rect.vPos + data.header.vStartPos);
    const EditResult inserted =
        InsertBuildingBlock(*m_State.document, script, script, found->guid, position);
    if (!inserted.ok) {
        m_State.Log(LogLevel::Error, "--insert-block: " + AnsiToUtf8(inserted.message));
        return;
    }
    CKBehavior *root = CKBehavior::Cast(m_State.document->Context()->GetObject(script));
    CKBehaviorIO *start = root && root->GetInputCount() ? root->GetInput(0) : nullptr;
    const EditResult linked = CreateBehaviorLink(*m_State.document, script,
                                                 LinkEndpoint(start ? start->GetID() : 0, 0, ENDPOINT_START_BIN),
                                                 LinkEndpoint(inserted.created, 0, ENDPOINT_BIN));
    m_State.Log(linked.ok ? LogLevel::Info : LogLevel::Error,
                "--insert-block: inserted " + m_Startup.insertBlock + (linked.ok ? ", linked from Start" : ", " + AnsiToUtf8(linked.message)));
}

// --tool NAME (capture checks).
void EditorApp::OpenStartupTool(const std::string &name) {
    if (name.compare(0, 5, "prefs") == 0) {
        int page = 0;
        if (name.size() > 6) page = std::atoi(name.c_str() + 6);
        m_Tools.OpenGeneralPreferences(page);
    } else if (name == "plugins") {
        m_Tools.OpenInstalledPlugins();
    } else if (name == "paths") {
        m_Tools.OpenPathManager();
    } else if (name == "unused") {
        m_Tools.OpenUnusedObjects();
    } else if (name == "versiontest") {
        m_Tools.RunVersionTest();
    } else if (name == "profiler") {
        m_Tools.showProfiler = true;
    } else if (name == "eventlog") {
        m_ShowLog = true;
    } else if (name == "watch") {
        // The Script locals of the first Script, as Paste after a copy would add them.
        std::vector<CK_ID> locals;
        CKContext *context = m_State.HasDocument() ? m_State.document->Context() : nullptr;
        for (const ScriptInfo &script : m_State.HasDocument() ? m_State.document->Scripts() : std::vector<ScriptInfo>())
            if (CKBehavior *behavior = context ? CKBehavior::Cast(context->GetObject(script.id)) : nullptr) {
                for (int i = 0; i < behavior->GetLocalParameterCount(); ++i)
                    if (CKParameterLocal *local = behavior->GetLocalParameter(i))
                        if (!local->IsMyselfParameter()) locals.push_back(local->GetID());
                if (!locals.empty()) break;
            }
        m_Tools.showParameterDebugger = true;
        m_Tools.WatchParameters(locals);
    } else if (name == "about") {
        m_Tools.OpenAbout();
    } else if (name == "linkprops") {
        m_Tools.OpenLinkProperties();
    } else if (name == "debugger") {
        m_State.showDebugger = true;
    } else if (name == "explorer") {
        ShowSchematicExplorer();
    } else if (name == "breakpointconfig" || name == "breakpoints" || name == "invalidlinks") {
        // The Schematic in front (it draws them), then once the start's focus settled.
        m_State.events.Post(FocusSchematic{});
        m_PendingStartupDialog = name;
    } else if (name == "webpage") {
        m_OpenWebPage = true;
    } else if (name == "saveversion") {
        SaveVersion();
    } else if (name == "revert") {
        RevertToSaved();
    } else if (name.compare(0, 11, "printpages:") == 0) {
        m_PendingPrintPages = name.substr(11);
    } else if (name.compare(0, 6, "print:") == 0) {
        m_PendingPrintDialog = name.substr(6);
    } else {
        m_State.Log(LogLevel::Warning, "--tool: unknown window " + name);
    }
}

// --tool breakpoints / breakpointconfig / invalidlinks (capture checks).
void EditorApp::OpenStartupDialog(const std::string &name) {
    if (name == "breakpointconfig" || name == "breakpoints") {
        // A breakpoint on the first Script's first behavior when there is none.
        CKContext *context = m_State.HasDocument() ? m_State.document->Context() : nullptr;
        CKBehavior *script = context && !m_State.visibleScripts.empty()
                                 ? CKBehavior::Cast(context->GetObject(m_State.visibleScripts.front()))
                                 : nullptr;
        if (m_State.debugger && m_State.debugger->Breakpoints().empty() && script && script->GetSubBehaviorCount() > 0)
            ToggleBreakpoint(*m_State.document, script->GetID(), EXTRA_DATA_BEHAVIOR, script->GetSubBehavior(0)->GetID());
        if (name == "breakpoints") m_Canvas.OpenBreakpointList();
        else m_Canvas.ConfigureBreakpoints();
    } else if (name == "invalidlinks") {
        // A link of the first Script listed as invalid (the explorer's look).
        if (m_State.invalidLinks.empty() && !m_State.visibleScripts.empty())
            m_State.invalidLinks.push_back({m_State.visibleScripts.front(), m_State.visibleScripts.front(), -1, 0, true, "capture"});
        m_Canvas.ShowInvalidLinks();
    }
}

// The per-frame actions, once the start's window focus has settled (a focus
// change closes popups).
void EditorApp::StartupFrame(bool settled) {
    if (!settled) {
    } else if (m_StartupParameters) {
        // --edit-parameters once the focus settled (a focus change closes popups).
        m_Canvas.OpenParametersOf(m_StartupParametersScript, m_StartupParameters);
        m_StartupParameters = 0;
    } else if (!m_Startup.editValue.empty() && m_StartupValueState == 0 && m_State.HasDocument()) {
        CKContext *context = m_State.document->Context();
        CKParameterManager *manager = context->GetParameterManager();
        // The type's name, or its GUID as "0xd1,0xd2".
        CKParameterType type = manager->ParameterNameToType(const_cast<CKSTRING>(m_Startup.editValue.c_str()));
        unsigned int d1 = 0, d2 = 0;
        if (type < 0 && std::sscanf(m_Startup.editValue.c_str(), "%x,%x", &d1, &d2) == 2)
            type = manager->ParameterGuidToType(CKGUID(d1, d2));
        const std::string name = type >= 0 && manager->ParameterTypeToName(type) ? manager->ParameterTypeToName(type) : "";
        CKParameterOut *parameter =
            type >= 0 ? context->CreateCKParameterOut(const_cast<CKSTRING>(name.c_str()), manager->ParameterTypeToGuid(type), TRUE)
                      : nullptr;
        const bool added = parameter && m_StartupValue.Add(parameter, name);
        if (parameter) context->DestroyObject(parameter); // the box edits its own copy
        if (added) {
            ImGui::OpenPopup("Edit Parameter###StartupValue");
            m_StartupValueState = 1;
        } else {
            m_State.Log(LogLevel::Error, "--edit-value: no type named " + AnsiToUtf8(m_Startup.editValue));
            m_StartupValueState = 2;
        }
    }
    if (m_StartupValueState == 1 &&
        EditParameterValueDialog("Edit Parameter###StartupValue", "Edit Parameter", m_StartupValue) != DialogAction::None) {
        m_StartupValue.Clear();
        m_StartupValueState = 2;
    }
}

} // namespace vdev
