#include "tests/ui/UiTests.h"
#include "core/level/SetupCommands.h"
#include "core/level/LevelCommands.h"
#include "core/graph/TopologyCommands.h"
#include "core/graph/BehaviorCommands.h"
#include "core/graph/ScriptCommands.h"
#include "core/document/BehaviorDescription.h"
#include "core/document/FileCommands.h"
#include "core/graph/ValueCommands.h"
#include "core/graph/RuntimeGraph.h"
#include "core/schematic/SchematicModelBuilder.h"
#include "core/schematic/SchematicEdits.h"
#include <cstring>
#include "core/graph/ParameterValues.h"
#include "core/catalog/ParameterUIs.h"
#include "ui/controls/DevBitmaps.h"
#include "ui/controls/DevTheme.h"
#include "ui/controls/DevControls.h"
#include "ui/setup/SetupLayout.h"
#include "ui/render/UiRenderer.h"
#include "imgui_internal.h"
#include "CKSprite3D.h"
#include "CK3dObject.h"
#include "CKMesh.h"
#include "CKTexture.h"
#include "CKMaterial.h"
#include "CKPatchMesh.h"
#include "CKGrid.h"
#include "CKSprite.h"
#include "CKCurve.h"
#include "CKCurvePoint.h"
#include "CKCharacter.h"
#include "CKBodyPart.h"
#include "CKAnimation.h"
#include "CKWaveSound.h"
#include "CKSoundManager.h"
#include "CKKeyedAnimation.h"
#include "CKObjectAnimation.h"
#include "CKMidiSound.h"
#include "CKMidiManager.h"
#include "CKSpriteText.h"
#include "CKLayer.h"
#include "CKKinematicChain.h"
#include "CKParameterLocal.h"
#include "CKParameterOut.h"
#include "CKParameterIn.h"
#include "CKParameterOperation.h"
#include "CKBehaviorIO.h"
#include "CKBehaviorLink.h"
#include "CKSynchroObject.h"
#include "CKInterfaceObjectManager.h"
#include "CKStateChunk.h"
#include <chrono>
#include <algorithm>
#include <utility>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>

namespace vdevuitest {
namespace {
struct PageExtentProbe {
    bool visible = false;
};
void Select(vdev::EditorConfiguration &config, const char *kind, const char *id, const char *provider) {
    for (auto &row : config.providers)
        if (row.kind == kind && row.id == id) {
            row.provider = provider;
            return;
        }
    config.providers.push_back({kind, id, provider});
}
void Alternate(vdev::EditorConfiguration &config) {
    Select(config, "theme", "editor.theme", "example.appearance");
    Select(config, "font", "ui.font.body", "example.appearance");
    Select(config, "image", "ui.bitmap.777", "example.appearance");
}
void CheckAppearance(ImGuiTestContext *ctx, bool alternate) {
    OnMainThread(ctx, [&] {
        const auto color = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        IM_CHECK_NO_RET(ImGui::ColorConvertFloat4ToU32(color) ==
                        (alternate ? IM_COL32(36, 54, 74, 255) : IM_COL32(176, 176, 176, 255)));
        IM_CHECK_NO_RET(ImGui::GetStyle().FontSizeBase == (alternate ? 16 : 14));
        const auto *image = vdev::DevBitmapForTests(777);
        IM_CHECK_NO_RET(image && image->NativeTexture());
        std::vector<unsigned char> pixels;
        IM_CHECK_NO_RET(vdev::MainRenderer().ReadTextureForTests(*image, pixels));
        if (alternate)
            IM_CHECK_NO_RET(pixels.size() == 17 * 19 * 4 && pixels[0] == 201 && pixels[1] == 37 && pixels[2] == 175);
        IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                        App().Layout().ViewportUploadsForTests() == 0);
    });
}
ImGuiWindow *Article(ImGuiTestContext *ctx) {
    const auto name = "//" + App().WorkspaceForTests().WindowName(vdev::HelpViewForTests());
    for (auto *child : ctx->GetWindowByRef(name.c_str())->DC.ChildWindows)
        if (std::string(child->Name).find("/article_") != std::string::npos) return child;
    return nullptr;
}
ImVec2 ReadingLine(ImGuiWindow *article) {
    for (const auto &item : App().Help().PagePaint().drawings)
        if (item.text == "struct")
            return {item.size.x, article->DC.CursorMaxPos.y - App().Help().PagePaint().size.y + item.position.y -
                                     article->InnerRect.Min.y};
    return {};
}
size_t Colored(ImGuiWindow *window, ImU32 color, bool children = false) {
    size_t count = 0;
    for (const auto &vertex : window->DrawList->VtxBuffer) count += vertex.col == color;
    if (children)
        for (auto *child : window->DC.ChildWindows) count += Colored(child, color, true);
    return count;
}
std::string SetupRef(EditorView view) {
    return "//" + App().WorkspaceForTests().WindowName(view);
}
ImGuiWindow *SetupPage(ImGuiTestContext *ctx, EditorView view) {
    for (auto *child : ctx->GetWindowByRef(SetupRef(view).c_str())->DC.ChildWindows)
        if (std::string(child->Name).find("/page_") != std::string::npos) return child;
    return nullptr;
}
void ResizeSetup(ImGuiTestContext *ctx, EditorView view, float width, bool focus = true) {
    OnMainThread(ctx, [&] {
        const auto name = App().WorkspaceForTests().WindowName(view);
        ImGui::SetWindowDock(ImGui::FindWindowByName(name.c_str()), 0, ImGuiCond_Always);
        ImGui::SetWindowPos(name.c_str(), {10, 60});
        ImGui::SetWindowSize(name.c_str(), {width, 680});
    });
    if (focus) ctx->WindowFocus(SetupRef(view).c_str());
    ctx->Yield(4);
}
ImGuiTable *SetupTable(ImGuiTestContext *ctx, EditorView view, const char *id) {
    auto *page = SetupPage(ctx, view);
    return page ? ctx->UiContext->Tables.GetByKey(page->GetID(id)) : nullptr;
}
ImGuiWindow *SetupChild(ImGuiTestContext *ctx, EditorView view, const char *id) {
    auto *page = SetupPage(ctx, view);
    if (page)
        for (auto *child : page->DC.ChildWindows)
            if (child->ChildId == page->GetID(id))
                return child;
    return nullptr;
}
void ShowSetupTable(ImGuiTestContext *ctx, EditorView view, const char *id) {
    auto *page = SetupPage(ctx, view);
    ctx->ScrollToBottom(page->ID);
    ctx->Yield(3);
    if (auto *table = SetupTable(ctx, view, id)) {
        ctx->ScrollTo(page->ID, ImGuiAxis_Y, page->Scroll.y + table->OuterRect.Min.y - page->InnerRect.Min.y);
        ctx->Yield(3);
    }
}
struct AnimationSetupObjects {
    vdev::ObjectRef first, merged, part, shared, mergedPart, entity, character;
    std::string partName, userName;
};
AnimationSetupObjects MakeSetupAnimations(ImGuiTestContext *ctx) {
    AnimationSetupObjects result;
    OnMainThread(ctx, [&] {
        auto &doc = *App().State().document;
        auto *context = doc.Context();
        auto *character =
            CKCharacter::Cast(context->CreateObject(CKCID_CHARACTER, "Setup animation character"));
        auto *entity = CKBodyPart::Cast(context->CreateObject(
            CKCID_BODYPART, ("Setup animation entity " + std::string(110, 'E')).c_str()));
        auto *first =
            CKKeyedAnimation::Cast(context->CreateObject(CKCID_KEYEDANIMATION, "Setup keyed animation"));
        auto *second =
            CKKeyedAnimation::Cast(context->CreateObject(CKCID_KEYEDANIMATION, "Setup second animation"));
        auto *a =
            CKObjectAnimation::Cast(context->CreateObject(CKCID_OBJECTANIMATION, "Setup object animation"));
        auto *b = CKObjectAnimation::Cast(context->CreateObject(CKCID_OBJECTANIMATION, "Setup second track"));
        auto *shared =
            CKObjectAnimation::Cast(context->CreateObject(CKCID_OBJECTANIMATION, "Setup shared track"));
        IM_CHECK_NO_RET(character && entity && first && second && a && b && shared);
        for (CKObject *object :
             {static_cast<CKObject *>(character), static_cast<CKObject *>(entity),
              static_cast<CKObject *>(first), static_cast<CKObject *>(second), static_cast<CKObject *>(a),
              static_cast<CKObject *>(b), static_cast<CKObject *>(shared)})
            doc.Include(object->GetID());
        IM_CHECK_NO_RET(character->AddBodyPart(entity) == CK_OK &&
                        character->SetRootBodyPart(entity) == CK_OK);
        VxVector start{0, 0, 0}, end{1, 2, 3};
        for (auto *part : {a, b}) {
            part->Set3dEntity(entity);
            part->AddPositionKey(0, &start);
            part->AddPositionKey(1, &end);
            part->SetLength(60);
        }
        IM_CHECK_NO_RET(first->AddAnimation(a) == CK_OK && second->AddAnimation(b) == CK_OK);
        first->SetLength(60);
        second->SetLength(60);
        first->LinkToFrameRate(TRUE, 30);
        first->SetSecondaryAnimationMode(static_cast<CK_SECONDARYANIMATION_FLAGS>(
            CKSECONDARYANIMATION_ONESHOT | CKSECONDARYANIMATION_DOWARP));
        IM_CHECK_NO_RET(character->AddAnimation(first) == CK_OK && character->AddAnimation(second) == CK_OK);
        auto *merged = CKKeyedAnimation::Cast(first->CreateMergedAnimation(second));
        IM_CHECK_NO_RET(merged && merged->IsMerged() && merged->GetAnimationCount() == 1);
        merged->SetMergeFactor(.2f);
        doc.Include(merged->GetID());
        auto *mergedPart = merged->GetAnimation(0);
        IM_CHECK_NO_RET(mergedPart && mergedPart->IsMerged());
        doc.Include(mergedPart->GetID());
        IM_CHECK_NO_RET(shared->ShareDataFrom(a));
        shared->Set3dEntity(entity);
        result = {doc.Reference(first->GetID()),      doc.Reference(merged->GetID()),
                  doc.Reference(a->GetID()),          doc.Reference(shared->GetID()),
                  doc.Reference(mergedPart->GetID()), doc.Reference(entity->GetID()),
                  doc.Reference(character->GetID())};
        result.partName = vdev::AnsiToUtf8(a->GetName());
        vdev::SetupDetails users;
        IM_CHECK_NO_RET(vdev::ReadSetupDetails(EditorCommands(), result.part, "object-animation", users));
        for (const auto &row : users.Values().Rows("users")) {
            const vdev::SetupFields value(row.fields);
            if (value.Object("target") == result.first)
                result.userName = value.Text("name") + " (" + value.Text("class-name") + ")";
        }
        IM_CHECK_NO_RET(!result.userName.empty());
    });
    return result;
}
struct SetupParameterObjects {
    vdev::ObjectRef script, local, output, destination, text, reference, referenced, myself;
    vdev::ObjectRef ownedInput, ownedValue, sharedInput, linkedInput, emptyInput;
    vdev::ObjectRef operation, firstOperand, secondOperand, result, operandValue, emptyOperation;
    std::string destinationName;
};
SetupParameterObjects MakeSetupParameters(ImGuiTestContext *ctx) {
    SetupParameterObjects objects;
    OnMainThread(ctx, [&] {
        using namespace vdev;
        auto &doc = *App().State().document;
        auto *context = doc.Context();
        const auto made = CreateScript(doc, context->GetCurrentLevel()->GetID(), "Setup owner " + std::string(90, 'S'));
        IM_CHECK_NO_RET(made.ok);
        auto *script = CKBehavior::Cast(context->GetObject(made.created));
        IM_CHECK_NO_RET(script);
        objects.script = doc.Reference(script->GetID());
        const auto local = [&](const char *name, CKGUID type, int y) {
            const auto added = AddLocalParameter(doc, made.created, made.created, name, type, Point(40, float(y)));
            return added.ok ? CKParameterLocal::Cast(context->GetObject(added.created)) : nullptr;
        };
        auto *integer = local("Setup local integer", CKPGUID_INT, 60);
        auto *text = local("Setup long text", CKPGUID_STRING, 110);
        auto *reference = local("Setup object reference", CKPGUID_3DENTITY, 160);
        auto *destination = local("Setup destination", CKPGUID_INT, 210);
        const auto myself = AddThisParameter(doc, made.created, made.created, Point(40, 260));
        IM_CHECK_NO_RET(integer && text && reference && destination && myself.ok);
        auto *object = context->CreateObject(CKCID_3DOBJECT, ("Referenced object " + std::string(90, 'R')).c_str());
        auto *output = script->CreateOutputParameter("Setup output", CKPGUID_INT);
        auto *owned = script->CreateInputParameter("Owned input", CKPGUID_INT);
        auto *shared = script->CreateInputParameter("Shared input", CKPGUID_INT);
        auto *linked = script->CreateInputParameter("Linked input", CKPGUID_INT);
        auto *empty = script->CreateInputParameter("Unlinked input", CKPGUID_INT);
        auto *holder = context->CreateCKParameterLocal("Owned input value", CKPGUID_INT);
        IM_CHECK_NO_RET(object && output && owned && shared && linked && empty && holder);
        int value = 7;
        integer->SetValue(&value, sizeof(value));
        value = 13;
        holder->SetValue(&value, sizeof(value));
        holder->SetOwner(owned);
        IM_CHECK_NO_RET(owned->SetDirectSource(holder) == CK_OK && shared->ShareSourceWith(owned) == CK_OK &&
                        linked->SetDirectSource(output) == CK_OK && output->AddDestination(destination) == CK_OK);
        value = 31;
        output->SetValue(&value, sizeof(value));
        const std::string longText = std::string(260, 'T') + "\nA second line";
        text->SetStringValue(longText.c_str());
        const CK_ID objectId = object->GetID();
        reference->SetValue(&objectId, sizeof(objectId));
        const auto addition = context->GetParameterManager()->OperationNameToGuid("Addition");
        const auto added = AddParameterOperation(doc, made.created, made.created, addition, CKPGUID_INT, CKPGUID_INT,
                                                 CKPGUID_INT, Point(400, 80));
        IM_CHECK_NO_RET(added.ok);
        auto *operation = CKParameterOperation::Cast(context->GetObject(added.created));
        auto *operandValue = context->CreateCKParameterLocal("Operand value", CKPGUID_INT);
        auto *emptyOperation = context->CreateObject(CKCID_PARAMETEROPERATION, "Unconfigured operation");
        IM_CHECK_NO_RET(operation && operandValue && emptyOperation && operation->GetInParameter1() &&
                        operation->GetInParameter2() && operation->GetOutParameter());
        value = 17;
        operandValue->SetValue(&value, sizeof(value));
        operandValue->SetOwner(operation->GetInParameter1());
        IM_CHECK_NO_RET(operation->GetInParameter1()->SetDirectSource(operandValue) == CK_OK &&
                        operation->GetInParameter2()->SetDirectSource(output) == CK_OK);
        for (CKObject *item :
             {object, static_cast<CKObject *>(output), static_cast<CKObject *>(owned), static_cast<CKObject *>(shared),
              static_cast<CKObject *>(linked), static_cast<CKObject *>(empty), static_cast<CKObject *>(holder),
              static_cast<CKObject *>(operandValue), emptyOperation})
            doc.Include(item->GetID());
        objects.local = doc.Reference(integer->GetID());
        objects.text = doc.Reference(text->GetID());
        objects.reference = doc.Reference(reference->GetID());
        objects.referenced = doc.Reference(object->GetID());
        objects.myself = doc.Reference(myself.created);
        objects.output = doc.Reference(output->GetID());
        objects.destination = doc.Reference(destination->GetID());
        objects.destinationName = destination->GetName();
        objects.ownedInput = doc.Reference(owned->GetID());
        objects.ownedValue = doc.Reference(holder->GetID());
        objects.sharedInput = doc.Reference(shared->GetID());
        objects.linkedInput = doc.Reference(linked->GetID());
        objects.emptyInput = doc.Reference(empty->GetID());
        objects.operation = doc.Reference(operation->GetID());
        objects.firstOperand = doc.Reference(operation->GetInParameter1()->GetID());
        objects.secondOperand = doc.Reference(operation->GetInParameter2()->GetID());
        objects.result = doc.Reference(operation->GetOutParameter()->GetID());
        objects.operandValue = doc.Reference(operandValue->GetID());
        objects.emptyOperation = doc.Reference(emptyOperation->GetID());
    });
    return objects;
}
vdev::SetupDetails SetupParameterDetails(ImGuiTestContext *ctx, vdev::ObjectRef target, const char *section) {
    return Read<vdev::SetupDetails>(ctx, [&] {
        vdev::SetupDetails data;
        vdev::ReadSetupDetails(EditorCommands(), target, section, data);
        return data;
    });
}
int SetupInteger(ImGuiTestContext *ctx, vdev::ObjectRef target) {
    return Read<int>(ctx, [&] {
        int value = 0;
        if (auto *parameter = CKParameter::Cast(App().State().document->Resolve(target)))
            parameter->GetValue(&value);
        return value;
    });
}
void EditSetupInteger(ImGuiTestContext *ctx, EditorView view, int value, float width, bool accept) {
    ctx->SetRef(SetupRef(view).c_str());
    const auto button = SetupPage(ctx, view)->GetID("Edit Value...");
    ctx->ScrollToItemY(button);
    ctx->ItemClick(button);
    ctx->Yield(3);
    const auto popup = ctx->UiContext->OpenPopupStack.back().PopupId;
    ctx->SetRef("//Edit Parameter###AttributeManagerValue");
    ctx->ItemClick("**/##number");
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
    ctx->KeyChars(std::to_string(value).c_str());
    ctx->Yield(2);
    ResizeSetup(ctx, view, width, false);
    IM_CHECK_NO_RET(!ctx->UiContext->OpenPopupStack.empty() && ctx->UiContext->OpenPopupStack.back().PopupId == popup);
    ctx->SetRef("//Edit Parameter###AttributeManagerValue");
    ctx->ItemClick(accept ? "**/OK/##dialogbutton" : "**/Cancel/##dialogbutton");
    ctx->Yield(3);
}
struct SetupBehaviorObjects {
    vdev::ObjectRef owner, script, graph, child, block, target;
    vdev::ObjectRef input, output, childInput, outerInputLink, innerInputLink, innerOutputLink, outerOutputLink;
    vdev::ObjectRef detached, emptyIO, emptyLink, partialLink;
};
SetupBehaviorObjects MakeSetupBehaviors(ImGuiTestContext *ctx) {
    SetupBehaviorObjects objects;
    OnMainThread(ctx, [&] {
        using namespace vdev;
        auto &doc = *App().State().document;
        auto *context = doc.Context();
        auto *owner = context->CreateObject(CKCID_3DOBJECT, ("Setup behavior owner " + std::string(90, 'O')).c_str());
        IM_CHECK_NO_RET(owner);
        doc.Include(owner->GetID());
        const auto script = CreateScript(doc, owner->GetID(), "Setup behavior Script " + std::string(90, 'S'));
        IM_CHECK_NO_RET(script.ok);
        const auto graph = CreateBehaviorGraph(doc, script.created, script.created,
                                               "Setup outer graph " + std::string(90, 'G'), {240, 120});
        const auto sibling = CreateBehaviorGraph(doc, script.created, script.created, "Setup sibling", {600, 120});
        IM_CHECK_NO_RET(graph.ok && sibling.ok);
        const auto child = CreateBehaviorGraph(doc, script.created, graph.created,
                                               "Setup nested graph " + std::string(90, 'N'), {280, 180});
        IM_CHECK_NO_RET(child.ok);
        CKGUID prototype;
        for (const auto &entry : App().CatalogForTests().Entries())
            if (entry.name == "Set Position" && entry.targetable)
                prototype = entry.guid;
        IM_CHECK_NO_RET(prototype != CKGUID());
        const auto block = InsertBuildingBlock(doc, script.created, graph.created, prototype, {460, 180});
        IM_CHECK_NO_RET(block.ok && SetTargetUsed(doc, script.created, block.created, true).ok);
        auto *root = CKBehavior::Cast(context->GetObject(script.created));
        auto *outer = CKBehavior::Cast(context->GetObject(graph.created));
        auto *nested = CKBehavior::Cast(context->GetObject(child.created));
        auto *base = CKBehavior::Cast(context->GetObject(block.created));
        IM_CHECK_NO_RET(root && outer && nested && base && base->GetTargetParameter());
        const auto outerIn =
            CreateBehaviorLink(doc, script.created, {root->GetInput(0)->GetID(), 0, ENDPOINT_START_BIN},
                               {graph.created, 0, ENDPOINT_BIN}, 3);
        const auto innerIn = CreateBehaviorLink(doc, script.created, {graph.created, 0, ENDPOINT_BIN},
                                                {child.created, 0, ENDPOINT_BIN}, 4);
        const auto innerOut = CreateBehaviorLink(doc, script.created, {child.created, 0, ENDPOINT_BOUT},
                                                 {graph.created, 0, ENDPOINT_BOUT}, 5);
        const auto outerOut = CreateBehaviorLink(doc, script.created, {graph.created, 0, ENDPOINT_BOUT},
                                                 {sibling.created, 0, ENDPOINT_BIN}, 6);
        IM_CHECK_NO_RET(outerIn.ok && innerIn.ok && innerOut.ok && outerOut.ok);
        outer->SetPriority(7);
        CKBehaviorLink::Cast(context->GetObject(innerIn.created))->SetActivationDelay(2);
        auto *detached = CKBehavior::Cast(context->CreateObject(CKCID_BEHAVIOR, "Unattached Setup behavior"));
        auto *emptyIO = context->CreateObject(CKCID_BEHAVIORIO, "Ownerless Setup IO");
        auto *emptyLink = context->CreateObject(CKCID_BEHAVIORLINK, "Unconnected Setup link");
        auto *partialLink = CKBehaviorLink::Cast(context->CreateObject(CKCID_BEHAVIORLINK, "Partial Setup link"));
        IM_CHECK_NO_RET(detached && emptyIO && emptyLink && partialLink);
        detached->UseGraph();
        detached->SetType(CKBEHAVIORTYPE_BEHAVIOR);
        partialLink->SetOutBehaviorIO(nested->GetInput(0));
        for (CKObject *item :
             {static_cast<CKObject *>(detached), emptyIO, emptyLink, static_cast<CKObject *>(partialLink)})
            doc.Include(item->GetID());
        objects.owner = doc.Reference(owner->GetID());
        objects.script = doc.Reference(script.created);
        objects.graph = doc.Reference(graph.created);
        objects.child = doc.Reference(child.created);
        objects.block = doc.Reference(block.created);
        objects.target = doc.Reference(base->GetTargetParameter()->GetID());
        objects.input = doc.Reference(outer->GetInput(0)->GetID());
        objects.output = doc.Reference(outer->GetOutput(0)->GetID());
        objects.childInput = doc.Reference(nested->GetInput(0)->GetID());
        objects.outerInputLink = doc.Reference(outerIn.created);
        objects.innerInputLink = doc.Reference(innerIn.created);
        objects.innerOutputLink = doc.Reference(innerOut.created);
        objects.outerOutputLink = doc.Reference(outerOut.created);
        objects.detached = doc.Reference(detached->GetID());
        objects.emptyIO = doc.Reference(emptyIO->GetID());
        objects.emptyLink = doc.Reference(emptyLink->GetID());
        objects.partialLink = doc.Reference(partialLink->GetID());
    });
    return objects;
}
struct SetupControlObjects {
    vdev::ObjectRef synchro, state, section, interfaceObjects, emptyInterface, user;
    vdev::ObjectRef synchroParameter, stateParameter, sectionParameter;
    std::vector<vdev::ObjectRef> arrived;
    std::vector<uint64_t> chunkSizes;
};
SetupControlObjects MakeSetupControls(ImGuiTestContext *ctx) {
    SetupControlObjects objects;
    OnMainThread(ctx, [&] {
        using namespace vdev;
        auto &doc = *App().State().document;
        auto *context = doc.Context();
        // Synchronization classes may reuse an existing object by name.
        const auto suffix = " " + std::to_string(doc.LiveObjects(CKCID_SYNCHRO, true).size());
        auto *synchro =
            CKSynchroObject::Cast(context->CreateObject(CKCID_SYNCHRO, ("Setup rendez-vous" + suffix).c_str()));
        auto *state = CKStateObject::Cast(context->CreateObject(CKCID_STATE, ("Setup state" + suffix).c_str()));
        auto *section = CKCriticalSectionObject::Cast(
            context->CreateObject(CKCID_CRITICALSECTION, ("Setup critical section" + suffix).c_str()));
        auto *manager = static_cast<CKInterfaceObjectManager *>(
            context->CreateObject(CKCID_INTERFACEOBJECTMANAGER, ("Setup interface objects" + suffix).c_str()));
        auto *empty = static_cast<CKInterfaceObjectManager *>(
            context->CreateObject(CKCID_INTERFACEOBJECTMANAGER, ("Empty Setup interface objects" + suffix).c_str()));
        IM_CHECK_NO_RET(synchro && state && section && manager && empty);
        for (CKObject *item :
             {static_cast<CKObject *>(synchro), static_cast<CKObject *>(state), static_cast<CKObject *>(section),
              static_cast<CKObject *>(manager), static_cast<CKObject *>(empty)})
            doc.Include(item->GetID());
        synchro->SetRendezVousNumberOfWaiters(100);
        state->LeaveState();
        for (int i = 0; i < 16; ++i) {
            const auto name = "Arrived" + suffix + " " + std::to_string(i) + " " + std::string(90, 'A');
            auto *object = CKBeObject::Cast(context->CreateObject(CKCID_3DOBJECT, name.c_str()));
            IM_CHECK_NO_RET(object && !synchro->CanIPassRendezVous(object));
            doc.Include(object->GetID());
            objects.arrived.push_back(doc.Reference(object->GetID()));
        }
        auto *holder = CKBeObject::Cast(doc.Resolve(objects.arrived[0]));
        IM_CHECK_NO_RET(section->EnterCriticalSection(holder));
        const auto script = CreateScript(doc, holder->GetID(), "Setup control Script " + std::string(90, 'S'));
        IM_CHECK_NO_RET(script.ok);
        const auto graph = CreateBehaviorGraph(doc, script.created, script.created,
                                               "Setup control user " + std::string(90, 'B'), {240, 120});
        IM_CHECK_NO_RET(graph.ok);
        auto *root = CKBehavior::Cast(context->GetObject(script.created));
        auto *user = CKBehavior::Cast(context->GetObject(graph.created));
        const auto parameter = [&](CKObject *target, CKGUID type, const char *name) {
            auto *value = root->CreateLocalParameter(name, type);
            auto *input = user->CreateInputParameter(name, type);
            if (!value || !input)
                return ObjectRef{};
            const auto id = target->GetID();
            value->SetValue(&id, sizeof(id));
            if (input->SetDirectSource(value) != CK_OK)
                return ObjectRef{};
            doc.Include(value->GetID());
            doc.Include(input->GetID());
            return doc.Reference(input->GetID());
        };
        objects.synchroParameter = parameter(synchro, CKPGUID_SYNCHRO, "Synchro user parameter");
        objects.stateParameter = parameter(state, CKPGUID_STATE, "State user parameter");
        objects.sectionParameter = parameter(section, CKPGUID_CRITICALSECTION, "Critical section user parameter");
        IM_CHECK_NO_RET(objects.synchroParameter && objects.stateParameter && objects.sectionParameter);
        manager->SetGuid(CKGUID(0x12345678, 0x23456789));
        empty->SetGuid(CKGUID());
        manager->AddStateChunk(nullptr);
        auto *blank = CreateCKStateChunk(CKCID_OBJECT, nullptr);
        IM_CHECK_NO_RET(blank);
        blank->StartWrite();
        blank->CloseChunk();
        manager->AddStateChunk(blank);
        for (int i = 0; i < 32; ++i) {
            auto *chunk = CreateCKStateChunk(CKCID_OBJECT, nullptr);
            IM_CHECK_NO_RET(chunk);
            chunk->StartWrite();
            chunk->WriteIdentifier(0x12340000 + i);
            chunk->WriteInt(i);
            chunk->CloseChunk();
            manager->AddStateChunk(chunk);
        }
        for (int i = 0; i < manager->GetChunkCount(); ++i) {
            auto *chunk = manager->GetChunk(i);
            objects.chunkSizes.push_back(chunk ? uint64_t(chunk->GetDataSize()) : UINT64_MAX);
        }
        objects.synchro = doc.Reference(synchro->GetID());
        objects.state = doc.Reference(state->GetID());
        objects.section = doc.Reference(section->GetID());
        objects.interfaceObjects = doc.Reference(manager->GetID());
        objects.emptyInterface = doc.Reference(empty->GetID());
        objects.user = doc.Reference(user->GetID());
    });
    return objects;
}
void CheckSetupRename(ImGuiTestContext *ctx, EditorView view, vdev::ObjectRef target) {
    const auto before = ObjectName(ctx, target.object);
    const auto history = App().State().History().UndoCount();
    ctx->WindowFocus(SetupRef(view).c_str());
    ctx->KeyPress(ImGuiKey_F2);
    ctx->Yield(3);
    ctx->ItemInput("//###setuprename/##name");
    ctx->KeyCharsReplace("Cancelled Setup control name");
    const auto popup = ctx->UiContext->OpenPopupStack.back().PopupId;
    ResizeSetup(ctx, view, 690, false);
    IM_CHECK_NO_RET(!ctx->UiContext->OpenPopupStack.empty() && ctx->UiContext->OpenPopupStack.back().PopupId == popup);
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(3);
    IM_CHECK_NO_RET(ObjectName(ctx, target.object) == before);
    IM_CHECK_NO_RET(App().State().History().UndoCount() == history);
    ctx->WindowFocus(SetupRef(view).c_str());
    ctx->KeyPress(ImGuiKey_F2);
    ctx->Yield(3);
    ctx->ItemInputValue("//###setuprename/##name", "Confirmed Setup control name");
    ctx->Yield(3);
    IM_CHECK_NO_RET(ObjectName(ctx, target.object) == "Confirmed Setup control name");
    IM_CHECK_NO_RET(App().State().History().UndoCount() == history + 1);
    Undo(ctx);
    IM_CHECK_NO_RET(ObjectName(ctx, target.object) == before);
    IM_CHECK_NO_RET(App().State().History().UndoCount() == history);
}
void CheckSetupUser(ImGuiTestContext *ctx, EditorView view, vdev::ObjectRef target, vdev::ObjectRef parameter,
                    vdev::ObjectRef behavior) {
    const auto data = Read<vdev::SetupDetails>(ctx, [&] {
        vdev::SetupDetails data;
        vdev::ReadSetupDetails(EditorCommands(), target, "parameter-users", data);
        return data;
    });
    IM_CHECK_NO_RET(data.Values().Rows("users").size() == 1);
    const vdev::SetupFields row(data.Values().Rows("users").front().fields);
    IM_CHECK_NO_RET(row.Object("target") == parameter && row.Object("behavior") == behavior);
    const auto history = App().State().History().UndoCount();
    ctx->SetRef(SetupRef(view).c_str());
    ShowSetupTable(ctx, view, "users");
    auto *table = SetupTable(ctx, view, "users");
    IM_CHECK_NO_RET(table && table->InnerWindow);
    ctx->ScrollToX(table->InnerWindow->ID, 0);
    ctx->Yield(2);
    const int id = int(parameter.object);
    const auto seed = ImHashData(&id, sizeof(id), table->ID);
    ctx->ItemDoubleClick(ImHashStr(row.Text("behavior-name").c_str(), 0, seed), ImGuiTestOpFlags_MoveToEdgeL);
    ctx->Yield(3);
    IM_CHECK_NO_RET(ShowsObject(ctx, behavior.object));
    IM_CHECK_NO_RET(App().State().History().UndoCount() == history);
}
ImGuiWindow *ParameterDialogValues(ImGuiTestContext *ctx, const char *ref) {
    auto *window = ctx->GetWindowByRef(ref);
    for (auto *child : window->DC.ChildWindows)
        if (child->ChildId == window->GetID("##parameter-values"))
            return child;
    return nullptr;
}
void ResizeParameterDialog(ImGuiTestContext *ctx, const char *ref, ImVec2 size) {
    auto *window = ctx->GetWindowByRef(ref);
    OnMainThread(ctx, [&] { ImGui::SetWindowSize(window->Name, size); });
    ctx->Yield(4);
}
void CheckParameterDialogBounds(ImGuiTestContext *ctx, const char *ref) {
    auto *window = ctx->GetWindowByRef(ref);
    auto *values = ParameterDialogValues(ctx, ref);
    IM_CHECK_NO_RET(window->WasActive && values && !(window->Flags & ImGuiWindowFlags_NoResize));
    const auto *viewport = ImGui::GetMainViewport();
    IM_CHECK_NO_RET(window->Pos.x >= viewport->WorkPos.x - 1 && window->Pos.y >= viewport->WorkPos.y - 1 &&
                    window->Pos.x + window->Size.x <= viewport->WorkPos.x + viewport->WorkSize.x + 1 &&
                    window->Pos.y + window->Size.y <= viewport->WorkPos.y + viewport->WorkSize.y + 1);
    for (const char *label : {"OK", "Cancel"}) {
        const auto id = ImHashStr("##dialogbutton", 0, window->GetID(label));
        const auto rect = ctx->ItemInfo(id).RectFull;
        IM_CHECK_NO_RET(rect.Min.x >= window->Pos.x && rect.Max.x <= window->Pos.x + window->Size.x &&
                        rect.Min.y >= values->Pos.y + values->Size.y && rect.Max.y <= window->Pos.y + window->Size.y);
    }
}
struct SchematicDialogObjects {
    vdev::ObjectRef script, local, block, first, last, setting;
    vdev::ElementRef element;
    vdev::EditorConfiguration original;
    std::string name;
};
SchematicDialogObjects MakeSchematicDialogs(ImGuiTestContext *ctx, bool floatBounds = false, CKGUID localType = CKPGUID_INT) {
    SchematicDialogObjects objects;
    OnMainThread(ctx, [&] {
        using namespace vdev;
        auto &doc = *App().State().document;
        const auto script = CreateScript(doc, doc.Context()->GetCurrentLevel()->GetID(),
                                          "Parameter dialog Script " + std::string(100, 'S'));
        objects.name = "Schematic local " + std::string(130, 'L');
        const auto local = AddLocalParameter(doc, script.created, script.created, objects.name, localType, {60, 160});
        CKGUID prototype;
        for (const auto &entry : App().CatalogForTests().Entries())
            if (entry.name == "Bezier Transform")
                prototype = entry.guid;
        IM_CHECK_NO_RET(script.ok && local.ok && prototype != CKGUID());
        const auto block = InsertBuildingBlock(doc, script.created, script.created, prototype, {440, 180});
        IM_CHECK_NO_RET(block.ok);
        auto *behavior = CKBehavior::Cast(doc.Context()->GetObject(block.created));
        IM_CHECK_NO_RET(behavior && behavior->GetLocalParameter(0));
        behavior->SetName(("Bezier dialog " + std::string(110, 'B')).c_str());
        behavior->GetLocalParameter(0)->SetName(("Use 4 Floats " + std::string(90, 'F')).c_str());
        if (floatBounds)
            IM_CHECK_NO_RET(EditBehaviorSettings(doc, script.created, block.created, {{0, "TRUE"}}).ok);
        auto *first = EditableValueOf(behavior->GetInputParameter(0));
        auto *last = EditableValueOf(behavior->GetInputParameter(behavior->GetInputParameterCount() - 1));
        IM_CHECK_NO_RET(first && last);
        const float initial = 2;
        first->SetValue(&initial);
        first->SetName(("First input " + std::string(90, 'X')).c_str());
        if (floatBounds) {
            last->SetValue(&initial);
            last->SetName(("Last input " + std::string(90, 'Y')).c_str());
        }
        auto *parameter = CKParameter::Cast(doc.Context()->GetObject(local.created));
        const int number = localType == CKPGUID_INT ? 7 : 0;
        parameter->SetValue(&number);
        objects.script = doc.Reference(script.created);
        objects.local = doc.Reference(local.created);
        objects.block = doc.Reference(block.created);
        objects.first = doc.Reference(first->GetID());
        objects.last = doc.Reference(last->GetID());
        objects.setting = doc.Reference(behavior->GetLocalParameter(0)->GetID());
        const auto &locals = doc.Session(script.created)->Data().rootBehavior.localParams;
        for (size_t i = 0; i < locals.size(); ++i)
            if (locals[i].id == local.created)
                objects.element = {ElementKind::LocalParam, script.created, local.created, int(i)};
        IM_CHECK_NO_RET(objects.element.object == local.created);
        auto &workspace = App().WorkspaceForTests();
        objects.original = workspace.Application().Configuration();
        auto config = objects.original;
        Alternate(config);
        std::string error;
        IM_CHECK_NO_RET(workspace.Configure(config, error));
        App().Canvas().ShowScript(script.created, objects.element);
    });
    ctx->Yield(4);
    return objects;
}
void CloseSchematicDialogs(ImGuiTestContext *ctx, const SchematicDialogObjects &objects) {
    OnMainThread(ctx, [&] {
        std::string error;
        IM_CHECK_NO_RET(!App().WorkspaceForTests().Application().HasEdits());
        IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(objects.original, error));
        IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                        App().Layout().ViewportUploadsForTests() == 0);
    });
}
vdev::EditorConfiguration SetupDialogTheme(ImGuiTestContext *ctx) {
    return Read<vdev::EditorConfiguration>(ctx, [&] {
        auto &workspace = App().WorkspaceForTests();
        const auto before = workspace.Application().Configuration();
        auto next = before;
        Alternate(next);
        std::string error;
        IM_CHECK_NO_RET(workspace.Configure(next, error));
        return before;
    });
}
EditorView OpenSetupDialogPage(ImGuiTestContext *ctx, vdev::ObjectRef target, const char *role) {
    const auto view = Read<EditorView>(ctx, [&] {
        IM_CHECK_NO_RET(EditorCommands().Command("object.open-setup", {vdev::ObjectArgument("target", target)}).status <=
                        EDITOR_NO_CHANGE);
        return App().WorkspaceForTests().Role(role);
    });
    ctx->Yield(4);
    ResizeSetup(ctx, view, 1500);
    return view;
}
void FinishSetupDialogs(ImGuiTestContext *ctx, const vdev::EditorConfiguration &configuration) {
    OnMainThread(ctx, [&] {
        auto &workspace = App().WorkspaceForTests();
        IM_CHECK_NO_RET(!workspace.Application().HasEdits());
        std::string error;
        IM_CHECK_NO_RET(workspace.Configure(configuration, error));
        IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                        App().Layout().ViewportUploadsForTests() == 0);
    });
}
ImGuiWindow *SetupDialogContent(ImGuiTestContext *ctx, const char *ref, const char *id) {
    auto *window = ctx->GetWindowByRef(ref);
    const auto childId = window->GetID(id);
    const auto find = [&](auto &&self, ImGuiWindow *parent) -> ImGuiWindow * {
        for (auto *child : parent->DC.ChildWindows) {
            if (child->ChildId == childId) return child;
            if (auto *nested = self(self, child)) return nested;
        }
        return nullptr;
    };
    return find(find, window);
}
void CheckSetupDialogBounds(ImGuiTestContext *ctx, const char *ref, const char *content,
                            const char *accept = "OK", const char *cancel = "Cancel", bool viButtons = false) {
    auto *window = ctx->GetWindowByRef(ref);
    auto *values = SetupDialogContent(ctx, ref, content);
    IM_CHECK_NO_RET(window->WasActive && values && !(window->Flags & ImGuiWindowFlags_NoResize));
    const auto *viewport = ImGui::GetMainViewport();
    IM_CHECK_NO_RET(window->Pos.x >= viewport->WorkPos.x - 1 && window->Pos.y >= viewport->WorkPos.y - 1 &&
                    window->Pos.x + window->Size.x <= viewport->WorkPos.x + viewport->WorkSize.x + 1 &&
                    window->Pos.y + window->Size.y <= viewport->WorkPos.y + viewport->WorkSize.y + 1);
    for (const char *label : {accept, cancel}) {
        const auto id = viButtons ? ImHashStr("##dialogbutton", 0, window->GetID(label)) : window->GetID(label);
        const auto rect = ctx->ItemInfo(id).RectFull;
        IM_CHECK_NO_RET(rect.Min.x >= window->Pos.x && rect.Max.x <= window->Pos.x + window->Size.x &&
                        rect.Min.y >= values->Pos.y + values->Size.y && rect.Max.y <= window->Pos.y + window->Size.y);
    }
}
struct SetupDialogGrid {
    vdev::ObjectRef object;
    EditorView view = 0;
    std::vector<int> types, indices;
    int classification = -1;
};
SetupDialogGrid MakeDialogGrid(ImGuiTestContext *ctx, bool linker, int count = 1) {
    SetupDialogGrid result;
    OnMainThread(ctx, [&] {
        using namespace vdev;
        auto &doc = *App().State().document;
        const auto made = EditorCommands().Command("object.create-grid");
        const auto *created = EditorClient::Find(made, "created");
        IM_CHECK_NO_RET(created && created->object);
        result.object = created->object;
        auto *grid = CKGrid::Cast(doc.Resolve(result.object));
        grid->SetDimensions(4, 4, 4, 4);
        for (int i = 0; i < count; ++i) {
            const auto type = RegisterGridLayerType(doc, "Dialog layer " + std::to_string(result.object.object) + " " + std::to_string(i) + std::string(90, 'L'));
            IM_CHECK_NO_RET(type.ok);
            if (linker) IM_CHECK_NO_RET(SetGridLayerTypeLinker(doc, int(type.created), true).ok);
            IM_CHECK_NO_RET(AddGridLayer(doc, grid->GetID(), int(type.created)).ok);
            result.types.push_back(int(type.created));
            for (int index = 0; index < grid->GetLayerCount(); ++index)
                if (grid->GetLayerByIndex(index)->GetType() == int(type.created)) result.indices.push_back(index);
        }
        const auto classification = RegisterGridClassification(doc, "Dialog classification " + std::to_string(result.object.object) + std::string(90, 'C'));
        IM_CHECK_NO_RET(classification.ok && result.indices.size() == size_t(count));
        result.classification = int(classification.created);
    });
    result.view = OpenSetupDialogPage(ctx, result.object, "setupgrid");
    return result;
}
void OpenSquareDialog(ImGuiTestContext *ctx, const SetupDialogGrid &grid) {
    ctx->SetRef(SetupRef(grid.view).c_str());
    ctx->ScrollToTop(SetupPage(ctx, grid.view)->ID);
    ctx->Yield(3);
    const auto bar = ctx->ItemInfo(SetupPage(ctx, grid.view)->GetID("##paintvalue")).RectFull;
    ctx->MouseMoveToPos({bar.Min.x + .1f, bar.GetCenter().y});
    ctx->MouseClick(0);
    const auto squares = ctx->ItemInfo(SetupPage(ctx, grid.view)->GetID("##squares")).RectFull;
    ctx->MouseMoveToPos({squares.Min.x + 5, squares.Min.y + 5});
    ctx->MouseDoubleClick(0);
    ctx->Yield(4);
    ctx->SetRef("//Square Value");
}
void SelectDialogGridLayers(ImGuiTestContext *ctx, const SetupDialogGrid &grid) {
    const auto data = Read<vdev::SetupDetails>(ctx, [&] {
        vdev::SetupDetails result;
        vdev::ReadSetupDetails(EditorCommands(), grid.object, "grid", result);
        return result;
    });
    ShowSetupTable(ctx, grid.view, "layers");
    auto *table = SetupTable(ctx, grid.view, "layers");
    for (size_t i = 0; i < grid.indices.size(); ++i) {
        const int row = grid.indices[i];
        std::string name;
        for (const auto &item : data.Values().Rows("types"))
            if (vdev::SetupFields(item.fields).Integer("type") == uint64_t(grid.types[i]))
                name = vdev::SetupFields(item.fields).Text("name");
        if (i) ctx->KeyDown(ImGuiMod_Ctrl);
        ctx->ItemClick(ImHashStr(name.c_str(), 0, ImHashData(&row, sizeof(row), table->ID)), 0,
                       ImGuiTestOpFlags_MoveToEdgeL);
        if (i) ctx->KeyUp(ImGuiMod_Ctrl);
    }
}
bool GraphObjectAlive(ImGuiTestContext *ctx, vdev::ObjectRef object) {
    return Read<bool>(ctx, [&] { return App().State().document->Resolve(object) != nullptr; });
}
struct GraphDialogObjects {
    SchematicDialogObjects values;
    vdev::ObjectRef graph, other, link;
    vdev::ElementRef graphElement, otherElement, linkElement;
};
GraphDialogObjects MakeGraphDialogs(ImGuiTestContext *ctx) {
    GraphDialogObjects objects;
    // Keep count-based editing assertions below the history cap while this
    // group builds several complete Scripts in one process.
    OnMainThread(ctx, [] { App().State().History().SetLimit(1000); });
    objects.values = MakeSchematicDialogs(ctx);
    OnMainThread(ctx, [&] {
        using namespace vdev;
        auto &doc = *App().State().document;
        const auto script = objects.values.script.object;
        const auto graph = CreateBehaviorGraph(
            doc, script, script, "Graph dialog " + std::to_string(script) + " " + std::string(90, 'G'), {180, 360});
        const auto other =
            CreateBehaviorGraph(doc, script, script,
                                "Replacement graph " + std::to_string(script) + " " + std::string(90, 'R'), {500, 360});
        auto *root = CKBehavior::Cast(doc.Resolve(objects.values.script));
        IM_CHECK_NO_RET(graph.ok && other.ok && root);
        const auto link = CreateBehaviorLink(doc, script, {root->GetInput(0)->GetID(), 0, ENDPOINT_START_BIN},
                                             {graph.created, 0, ENDPOINT_BIN}, 3);
        IM_CHECK_NO_RET(link.ok);
        objects.graph = doc.Reference(graph.created);
        objects.other = doc.Reference(other.created);
        objects.link = doc.Reference(link.created);
        const auto model = BuildSchematicModel(*doc.Session(script), doc.Context(), {}, true);
        for (const auto &b : model.behaviors) {
            if (b.id == graph.created) objects.graphElement = b.ref;
            if (b.id == other.created) objects.otherElement = b.ref;
        }
        for (const auto &l : model.links)
            if (l.behaviorLink && l.ref.object == link.created) objects.linkElement = l.ref;
        IM_CHECK_NO_RET(objects.graphElement.IsValid() && objects.otherElement.IsValid() &&
                        objects.linkElement.IsValid());
    });
    ctx->Yield(4);
    return objects;
}
void CloseGraphDialogs(ImGuiTestContext *ctx, const GraphDialogObjects &objects) {
    CloseSchematicDialogs(ctx, objects.values);
    OnMainThread(ctx, [] { App().State().History().SetLimit(50); });
}
void SelectGraphDialog(ImGuiTestContext *ctx, const GraphDialogObjects &objects, const vdev::ElementRef &element) {
    OnMainThread(ctx, [&] { App().Canvas().ShowScript(objects.values.script.object, element); });
    ctx->WindowFocus(SchematicRef().c_str());
    ctx->Yield(4);
    // Navigation completes in a later frame and can focus a containing graph.
    OnMainThread(ctx, [&] { App().Canvas().Navigation().selection.Set(objects.values.script.object, element); });
    ctx->Yield(2);
}
void TypeGraphDialog(ImGuiTestContext *ctx, const char *item, const char *text) {
    ctx->ItemClick(item);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
    ctx->KeyChars(text);
    ctx->Yield(2);
}
void ResizeGraphDialog(ImGuiTestContext *ctx, const char *ref, const char *content, const char *accept = "OK",
                       const char *cancel = "Cancel", bool viButtons = false, float height = 240) {
    auto *window = ctx->GetWindowByRef(ref);
    IM_CHECK_NO_RET(window && window->WasActive && !ctx->UiContext->OpenPopupStack.empty());
    if (!window || !window->WasActive || ctx->UiContext->OpenPopupStack.empty()) return;
    const auto popup = ctx->UiContext->OpenPopupStack.back().PopupId;
    for (float width : {1300.f, 430.f, 850.f, 430.f}) {
        ResizeParameterDialog(ctx, ref, {width, height});
        CheckSetupDialogBounds(ctx, ref, content, accept, cancel, viButtons);
        IM_CHECK_EQ(ctx->UiContext->OpenPopupStack.back().PopupId, popup);
    }
}
void GraphHeaderMenu(ImGuiTestContext *ctx, const GraphDialogObjects &objects, const char *item) {
    SelectGraphDialog(ctx, objects, vdev::Selection::HeaderOf(objects.values.script.object));
    const auto shown =
        Read<std::vector<vdev::SchematicCanvas::ShownScript>>(ctx, [] { return App().Canvas().ShownScripts(); });
    auto script = std::find_if(shown.begin(), shown.end(),
                               [&](const auto &s) { return s.script == objects.values.script.object; });
    IM_CHECK_NO_RET(script != shown.end());
    ctx->MouseMoveToPos(Center(script->headerMin, script->headerMax));
    ctx->MouseClick(ImGuiMouseButton_Right);
    ctx->Yield(3);
    ctx->ItemClick((std::string("//$FOCUSED/") + item).c_str());
    ctx->Yield(4);
}
ImGuiWindow *GraphDialogRows(ImGuiTestContext *ctx, const char *ref, const char *content, const char *rows) {
    auto *parent = SetupDialogContent(ctx, ref, content);
    if (parent)
        for (auto *child : parent->DC.ChildWindows)
            if (child->ChildId == ctx->GetWindowByRef(ref)->GetID(rows)) return child;
    return nullptr;
}

bool ApplicationDialogShown(ImGuiTestContext *ctx, const char *ref) {
    const auto *window = ctx->GetWindowByRef(ref);
    return window && window->WasActive;
}
struct ApplicationDialogState {
    vdev::DocumentToken document;
    uint64_t revision = 0;
    bool dirty = false, redo = false;
    size_t undo = 0;
    std::string undoLabel, redoLabel;
    std::vector<vdev::ObjectRef> objects;
    vdev::Selection schematic;
    bool operator==(const ApplicationDialogState &other) const {
        return document == other.document && revision == other.revision && dirty == other.dirty && redo == other.redo &&
               undo == other.undo && undoLabel == other.undoLabel && redoLabel == other.redoLabel &&
               objects == other.objects && schematic.script == other.schematic.script &&
               schematic.items == other.schematic.items && schematic.headers == other.schematic.headers;
    }
};
ApplicationDialogState ReadApplicationDialogState(ImGuiTestContext *ctx) {
    return Read<ApplicationDialogState>(ctx, [] {
        auto &state = App().State();
        return ApplicationDialogState{state.document->Token(), state.document->ChangeRevision(), state.document->Dirty(),
                                      state.History().CanRedo(), state.History().UndoCount(), state.History().UndoLabel(),
                                      state.History().RedoLabel(), state.objectSelection.References(),
                                      App().Canvas().Navigation().selection};
    });
}
void RenameDialogObject(ImGuiTestContext *ctx, vdev::ObjectRef target, const std::string &name) {
    OnMainThread(ctx, [&] {
        IM_CHECK_NO_RET(EditorCommands().Command("object.rename", {vdev::ObjectArgument("target", target),
                        vdev::TextArgument("name", name)}).status == EDITOR_OK);
    });
}
void SeedApplicationDialogRedo(ImGuiTestContext *ctx, vdev::ObjectRef object) {
    RenameDialogObject(ctx, object, ObjectName(ctx, object.object) + " temporary");
    OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Undo()); });
}
struct ExplorerDialogObjects {
    vdev::ObjectRef target, other;
    std::string name, otherName, filter;
    vdev::EditorConfiguration configuration;
    std::vector<vdev::EditorField> preferences;
};
void SetExplorerDialogOptions(const std::vector<vdev::EditorField> &fields) {
    vdev::EditorField record{"value", EDITOR_RECORD};
    record.fields = fields;
    const std::vector<vdev::EditorField> owned{record};
    const auto values = vdev::BorrowEditorFields(owned);
    IM_CHECK_NO_RET(EditorCommands().Command("preferences.set-explorer-options",
                    {values.data(), values.data() + values.size()}).status <= EDITOR_NO_CHANGE);
}
ExplorerDialogObjects OpenExplorerDialog(ImGuiTestContext *ctx, bool tree) {
    ExplorerDialogObjects objects;
    objects.configuration = SetupDialogTheme(ctx);
    if (ApplicationDialogShown(ctx, "//Schematic Explorer")) {
        ctx->ItemClick("//Schematic Explorer/Cancel/##dialogbutton");
        ctx->Yield(3);
    }
    OnMainThread(ctx, [&] {
        using namespace vdev;
        auto &doc = *App().State().document;
        doc.History().SetLimit(1000);
        objects.filter = "Explorer " + std::to_string(SDL_GetTicksNS()) + " ";
        objects.name = objects.filter + "first " + std::string(120, 'N');
        objects.otherName = objects.filter + "second";
        const auto a = CreateScript(doc, doc.Context()->GetCurrentLevel()->GetID(), objects.name);
        const auto b = CreateScript(doc, doc.Context()->GetCurrentLevel()->GetID(), objects.otherName);
        IM_CHECK_NO_RET(a.ok && b.ok);
        objects.target = doc.Reference(a.created);
        objects.other = doc.Reference(b.created);
        const auto preferences = EditorCommands().Query("preferences.display");
        for (const auto &field : EditorClient::Find(preferences, "value")->fields)
            if (field.key.find("Schematic Explorer\\") == 0) objects.preferences.push_back(field);
        SetExplorerDialogOptions({{"Schematic Explorer\\Show In List", EDITOR_INTEGER_VALUE, {}, {}, SearchScripts},
                                  {"Schematic Explorer\\Parse All Items", EDITOR_INTEGER_VALUE, {}, {}, 1},
                                  {"Schematic Explorer\\Tree View", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(tree)},
                                  {"Schematic Explorer\\Parameter Filter", EDITOR_INTEGER_VALUE, {}, {}, 0}});
        App().Canvas().ShowScript(a.created);
    });
    ctx->WindowFocus(SchematicRef().c_str());
    ctx->Yield(4);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_F);
    ctx->Yield(4);
    ctx->SetRef("//Schematic Explorer");
    TypeGraphDialog(ctx, "##text", objects.filter.c_str());
    ctx->Yield(3);
    IM_CHECK_NO_RET(ApplicationDialogShown(ctx, "//Schematic Explorer"));
    return objects;
}
void OpenExplorerRename(ImGuiTestContext *ctx, const ExplorerDialogObjects &objects, bool tree) {
    ctx->SetRef("//Schematic Explorer");
    if (tree) {
        auto *rows = SetupDialogContent(ctx, "//Schematic Explorer", "##tree");
        IM_CHECK_NO_RET(rows && rows->WasActive);
        if (!rows || !rows->WasActive) return;
        const int object = int(objects.target.object);
        ctx->ItemClick(ImHashStr("##node", 0, ImHashData(&object, sizeof(object), rows->ID)));
        ctx->WindowFocus("//Schematic Explorer");
    } else {
        const auto rows = Read<std::vector<vdev::SearchResult>>(ctx, [] { return App().Panels().SearchResults(); });
        const auto target = std::find_if(rows.begin(), rows.end(), [&](const auto &r) { return r.objectReference == objects.target; });
        IM_CHECK_NO_RET(target != rows.end());
        if (target == rows.end()) return;
        ctx->ItemClick(("**/$$" + std::to_string(target - rows.begin()) + "/##row").c_str());
    }
    ctx->KeyPress(ImGuiKey_F2);
    ctx->Yield(4);
    ctx->SetRef(tree ? "//Rename###explorertreerename" : "//Rename###explorerrename");
}
void CloseExplorerDialog(ImGuiTestContext *ctx, const ExplorerDialogObjects &objects) {
    ctx->ItemClick("//Schematic Explorer/Cancel/##dialogbutton");
    ctx->Yield(3);
    OnMainThread(ctx, [&] {
        SetExplorerDialogOptions(objects.preferences);
        App().State().History().SetLimit(50);
    });
    FinishSetupDialogs(ctx, objects.configuration);
}
std::vector<double> ReadDialogTime(ImGuiTestContext *ctx) {
    return Read<std::vector<double>>(ctx, [] {
        const auto response = EditorCommands().Query("runtime.time");
        const vdev::SetupFields fields(response.fields);
        return std::vector<double>{double(fields.Integer("frame-mode")), double(fields.Integer("behavior-mode")),
                                   fields.Real("frame-limit"), fields.Real("behavior-limit"), fields.Real("scale"),
                                   fields.Real("minimum"), fields.Real("maximum"), double(fields.Record("display").Integer("hide-fps"))};
    });
}
void ChooseDuplicateDialogAction(ImGuiTestContext *ctx, int node, const char *action) {
    ctx->SetRef("//###DuplicateNames");
    ctx->ItemClick(("**/$$" + std::to_string(node) + "/##row").c_str(), 1, ImGuiTestOpFlags_MoveToEdgeL | ImGuiTestOpFlags_NoScroll);
    ctx->Yield(2);
    ctx->ItemClick((std::string("//$FOCUSED/") + action).c_str());
    ctx->Yield(2);
    ctx->SetRef("//###DuplicateNames");
}
struct DuplicateDialogFile {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("appearance-duplicates-" + std::to_string(SDL_GetTicksNS()) + ".nmo");
    ~DuplicateDialogFile() { std::error_code error; std::filesystem::remove(path, error); }
};
void WaitForDuplicateDialog(ImGuiTestContext *ctx) {
    const auto deadline = SDL_GetTicks() + 5000;
    while (!ApplicationDialogShown(ctx, "//###DuplicateNames") && SDL_GetTicks() < deadline) ctx->Sleep(.01f);
    IM_CHECK_NO_RET(ApplicationDialogShown(ctx, "//###DuplicateNames"));
    ctx->SetRef("//###DuplicateNames");
}

struct OutputDialogObjects {
    vdev::ObjectRef script, object;
    vdev::EditorConfiguration configuration;
};
OutputDialogObjects MakeOutputDialogs(ImGuiTestContext *ctx) {
    OutputDialogObjects objects;
    objects.configuration = SetupDialogTheme(ctx);
    OnMainThread(ctx, [&] {
        using namespace vdev;
        auto &doc = *App().State().document;
        doc.History().SetLimit(1000);
        const auto script = CreateScript(doc, doc.Context()->GetCurrentLevel()->GetID(),
                                          "Output dialog Script " + std::string(100, 'S'));
        IM_CHECK_NO_RET(script.ok);
        const auto local = AddLocalParameter(doc, script.created, script.created,
                                              "Printed local " + std::string(100, 'L'), CKPGUID_INT, {60, 160});
        const auto made = CreateLayoutObject(doc, LayoutObject::Frame3D);
        IM_CHECK_NO_RET(local.ok && made.ok);
        objects.script = doc.Reference(script.created);
        objects.object = doc.Reference(made.created);
        doc.Resolve(objects.object)->SetName(("File dialog object " + std::string(120, 'O')).c_str());
        App().State().objectSelection.Restore(doc, {made.created});
        App().Canvas().ShowScript(script.created, {ElementKind::LocalParam, script.created, local.created, 0});
    });
    SeedApplicationDialogRedo(ctx, objects.object);
    ctx->Yield(4);
    return objects;
}
void CloseOutputDialogs(ImGuiTestContext *ctx, const OutputDialogObjects &objects) {
    FinishSetupDialogs(ctx, objects.configuration);
    OnMainThread(ctx, [] { App().State().History().SetLimit(50); });
}
struct OutputDialogFiles {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("appearance-output-" + std::to_string(SDL_GetTicksNS()));
    OutputDialogFiles() { std::filesystem::create_directory(root); }
    ~OutputDialogFiles() { std::error_code error; std::filesystem::remove_all(root, error); }
    std::filesystem::path Long(const char *name, int components = 6) const {
        auto path = root;
        for (int i = 0; i < components; ++i) path /= "file-dialog-" + std::to_string(i) + "-" + std::string(42, 'p');
        std::filesystem::create_directories(path);
        return path / name;
    }
};
std::string OutputDialogBytes(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
void WriteOutputDialogFile(const std::filesystem::path &path, const std::string &text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
    IM_CHECK_NO_RET(bool(out));
}
EditorOperation StartOutputDialog(ImGuiTestContext *ctx, const char *command,
                                  std::initializer_list<EditorValue> values = {}) {
    return Read<EditorOperation>(ctx, [&] {
        const auto result = EditorCommands().Command(command, values);
        IM_CHECK_NO_RET(result.status == EDITOR_QUEUED);
        return result.operation;
    });
}
EditorStatus WaitOutputOperation(ImGuiTestContext *ctx, EditorOperation operation) {
    auto status = EDITOR_QUEUED;
    const auto deadline = SDL_GetTicks() + 10000;
    while (status == EDITOR_QUEUED && SDL_GetTicks() < deadline) {
        ctx->Sleep(.01f);
        status = Read<EditorStatus>(ctx, [&] { return EditorCommands().Completion(operation).status; });
    }
    return status;
}
void WaitOutputWindow(ImGuiTestContext *ctx, const char *ref) {
    const auto deadline = SDL_GetTicks() + 5000;
    while (!ApplicationDialogShown(ctx, ref) && SDL_GetTicks() < deadline) ctx->Sleep(.01f);
    IM_CHECK_NO_RET(ApplicationDialogShown(ctx, ref));
    ctx->SetRef(ref);
}
vdev::EditorResponse ReadOutputDialogOptions(ImGuiTestContext *ctx) {
    return Read<vdev::EditorResponse>(ctx, [] {
        const auto input = EditorCommands().Query("output.input");
        return EditorCommands().Query("output.options", {vdev::IntegerArgument("input", vdev::EditorClient::Integer(input, "input"))});
    });
}
EditorOperation StartFileExportDialog(ImGuiTestContext *ctx, const char *command, vdev::ObjectRef object,
                                     const std::filesystem::path &path, bool behavior) {
    const EditorValue targets{EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST, {}, {}, 0, {&object, 1}};
    return StartOutputDialog(ctx, command, {targets, vdev::TextArgument("path", path.u8string()),
        vdev::IntegerArgument(behavior ? "ask-description" : "ask-options", 1)});
}
void CheckUnsavedDialogButtons(ImGuiTestContext *ctx, const char *save) {
    CheckSetupDialogBounds(ctx, "//Unsaved changes", "##save-changes", save, "Cancel");
    auto *window = ctx->GetWindowByRef("//Unsaved changes");
    std::vector<ImRect> rects;
    for (const char *label : {save, "Save As...", "Discard", "Cancel"}) {
        const auto rect = ctx->ItemInfo(window->GetID(label)).RectFull;
        IM_CHECK_NO_RET(rect.Min.x >= window->Pos.x && rect.Max.x <= window->Pos.x + window->Size.x &&
                        rect.Min.y >= SetupDialogContent(ctx, "//Unsaved changes", "##save-changes")->Rect().Max.y &&
                        rect.Max.y <= window->Pos.y + window->Size.y);
        for (const auto &previous : rects) IM_CHECK_NO_RET(!rect.Overlaps(previous));
        rects.push_back(rect);
    }
}

// The SDK owns registered managers until Context destruction. Sources are released first.
class SetupMidiManager final : public CKMidiManager {
    struct Source {
        std::string file;
        CKBOOL playing = FALSE, paused = FALSE;
        CKDWORD position = 0;
    };
    static Source &Of(void *source) { return *static_cast<Source *>(source); }

  public:
    explicit SetupMidiManager(CKContext *context) : CKMidiManager(context, "Setup MIDI test manager") {}
    void *Create(void *) override { return new Source; }
    void Release(void *source) override { delete static_cast<Source *>(source); }
    CKERROR SetSoundFileName(void *source, CKSTRING file) override {
        Of(source).file = file ? file : "";
        return CK_OK;
    }
    CKSTRING GetSoundFileName(void *source) override { return const_cast<char *>(Of(source).file.c_str()); }
    CKERROR Play(void *source) override {
        Of(source).playing = TRUE;
        Of(source).paused = FALSE;
        Of(source).position = 321;
        return CK_OK;
    }
    CKERROR Restart(void *source) override {
        Of(source).paused = FALSE;
        return CK_OK;
    }
    CKERROR Stop(void *source) override {
        Of(source).playing = Of(source).paused = FALSE;
        Of(source).position = 0;
        return CK_OK;
    }
    CKERROR Pause(void *source, CKBOOL pause = TRUE) override {
        Of(source).paused = pause;
        return CK_OK;
    }
    CKBOOL IsPlaying(void *source) override { return Of(source).playing; }
    CKBOOL IsPaused(void *source) override { return Of(source).paused; }
    CKERROR OpenFile(void *) override { return CK_OK; }
    CKERROR CloseFile(void *) override { return CK_OK; }
    CKERROR Preroll(void *) override { return CK_OK; }
    CKERROR Time(void *source, CKDWORD *ticks) override {
        *ticks = Of(source).position;
        return CK_OK;
    }
    CKDWORD MillisecsToTicks(void *, CKDWORD value) override { return value; }
    CKDWORD TicksToMillisecs(void *, CKDWORD value) override { return value; }
};
} // namespace
void RegisterAppearanceTests(ImGuiTestEngine *engine) {
    auto *edit = IM_REGISTER_TEST(engine, "appearance", "theme_font_and_image_switch_at_frame_boundary");
    edit->GuiFunc = MainThreadGui;
    edit->TestFunc = [](ImGuiTestContext *ctx) {
        ctx->Yield(8);
        OnMainThread(ctx, [] {
            auto &editor = App().WorkspaceForTests().Application();
            const EditorValue topic{EDITOR_TEXT("topic"), EDITOR_TEXT_VALUE, {}, EDITOR_TEXT("appearance.html")};
            const auto result = editor.Execute("help.open", {editor.Scope(), &topic, 1});
            IM_CHECK_NO_RET(result.status == EDITOR_OK || result.status == EDITOR_QUEUED);
        });
        ctx->Yield(4);
        auto *article = Article(ctx);
        IM_CHECK(article);
        const float lineWidth = ReadingLine(article).x;
        IM_CHECK_GT(lineWidth, 0);
        ctx->ScrollTo(article->ID, ImGuiAxis_Y, article->Scroll.y + ReadingLine(article).y + 3,
                      ImGuiTestOpFlags_NoFocusWindow);
        ctx->Yield(3);
        IM_CHECK(std::fabs(ReadingLine(article).y) < 6);
        vdev::EditorConfiguration original;
        ImFont *font = nullptr;
        ImTextureID image = 0;
        size_t history = 0;
        OnMainThread(ctx, [&] {
            auto &workspace = App().WorkspaceForTests();
            original = workspace.Application().Configuration();
            font = ImGui::GetIO().FontDefault;
            image = vdev::DevBitmap(777);
            history = App().State().History().UndoCount();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(config, error));
            // Drawing already submitted in this frame still uses the old resources.
            IM_CHECK_NO_RET(ImGui::GetIO().FontDefault == font && vdev::DevBitmap(777) == image);
        });
        ctx->Yield(5);
        CheckAppearance(ctx, true);
        IM_CHECK_GT(ReadingLine(article).x, lineWidth);
        IM_CHECK(std::fabs(ReadingLine(article).y) < 12);
        int fonts = 0;
        OnMainThread(ctx, [&] {
            auto &workspace = App().WorkspaceForTests();
            IM_CHECK_NO_RET(ImGui::GetIO().FontDefault != font && vdev::DevBitmap(777) != image);
            IM_CHECK_NO_RET(font->OwnerAtlas == ImGui::GetIO().Fonts);
            fonts = ImGui::GetIO().Fonts->Fonts.Size;
            image = vdev::DevBitmap(777);
            auto invalid = workspace.Application().Configuration();
            Select(invalid, "font", "ui.font.body", "example.invalid-font");
            std::string error;
            const auto active = ImGui::GetIO().FontDefault;
            IM_CHECK_NO_RET(!workspace.Configure(invalid, error) &&
                            error.find("Cannot load font") != std::string::npos);
            IM_CHECK_NO_RET(ImGui::GetIO().FontDefault == active && vdev::DevBitmap(777) == image);
            // Refuse publication after resources have been successfully prepared.
            const char *name = std::getenv("NEMODEV_APPEARANCE_PROFILE");
            IM_CHECK_NO_RET(name);
            const auto path = std::filesystem::u8path(name),
                       backup = std::filesystem::u8path(std::string(name) + ".retained");
            std::filesystem::rename(path, backup);
            struct Restore {
                std::filesystem::path path, backup;
                ~Restore() {
                    std::error_code e;
                    std::filesystem::remove(path, e);
                    std::filesystem::rename(backup, path, e);
                }
            } restore{path, backup};
            std::filesystem::create_directory(path);
            IM_CHECK_NO_RET(!workspace.Configure(original, error));
            IM_CHECK_NO_RET(ImGui::GetIO().FontDefault == active && vdev::DevBitmap(777) == image);
            IM_CHECK_NO_RET(App().State().History().UndoCount() == history);
        });
        ctx->Yield(12);
        CheckAppearance(ctx, true);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(ImGui::GetIO().Fonts->Fonts.Size == fonts && vdev::DevBitmap(777) == image);
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
        });
        ctx->Yield(4);
        CheckAppearance(ctx, false);
        IM_CHECK(std::fabs(ReadingLine(article).x - lineWidth) < 1);
        IM_CHECK(std::fabs(ReadingLine(article).y) < 12);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(ImGui::GetIO().Fonts->Fonts.Size == fonts && ImGui::GetIO().FontDefault == font);
            const auto scale = ImGui::GetStyle().FontScaleDpi;
            std::string error;
            IM_CHECK_NO_RET(!App().PrepareUiFrame(scale * 2, error));
            IM_CHECK_NO_RET(ImGui::GetStyle().FontScaleDpi == scale);
            IM_CHECK_NO_RET(ImGui::GetIO().FontDefault == font && ImGui::GetIO().Fonts->Fonts.Size == fonts);
            IM_CHECK_NO_RET(App().State().History().UndoCount() == history);
        });
    };
    auto *pages = IM_REGISTER_TEST(engine, "appearance", "page_colors_geometry_and_edit_state");
    pages->GuiFunc = MainThreadGui;
    pages->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        vdev::EditorConfiguration original;
        vdev::DocumentToken token;
        std::vector<vdev::ObjectRef> selection;
        size_t history = 0;
        bool dirty = false;
        EditorView setup = 0, log = 0;
        OnMainThread(ctx, [&] {
            using namespace vdev;
            auto &workspace = App().WorkspaceForTests();
            auto client = EditorCommands();
            const auto made = client.Command("object.create-light");
            IM_CHECK_NO_RET(made.status == EDITOR_OK && EditorClient::Find(made, "created"));
            const auto light = EditorClient::Find(made, "created")->object;
            IM_CHECK_NO_RET(client.Command("object.create-frame3d").status == EDITOR_OK);
            IM_CHECK_NO_RET(client.Command("history.undo").status == EDITOR_OK);
            IM_CHECK_NO_RET(client.Command("selection.replace", {ObjectsArgument("objects", {light})}).status <=
                            EDITOR_NO_CHANGE);
            IM_CHECK_NO_RET(client.Command("object.open-setup", {ObjectArgument("target", light)}).status <=
                            EDITOR_NO_CHANGE);
            setup = workspace.Role("setuplight");
            log = EditorClient::Integer(client.Command("log.open"), "view");
            IM_CHECK_NO_RET(setup && log);
            IM_CHECK_NO_RET(EditorCommands(workspace.Role("main.layout")).Command("layout.snapshots").status <=
                            EDITOR_NO_CHANGE);
            original = workspace.Application().Configuration();
            token = App().State().document->Token();
            history = App().State().History().UndoCount();
            selection = App().State().objectSelection.References();
            dirty = App().State().document->Dirty();
            IM_CHECK_NO_RET(App().State().History().CanRedo());
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(config, error));
        });
        ctx->Yield(4);
        ctx->WindowFocus(SchematicRef().c_str());
        ctx->Yield(3);
        IM_CHECK_GT(Colored(ctx->GetWindowByRef(SchematicRef().c_str()), IM_COL32(83, 100, 117, 255)), 0);
        const auto setupRef = "//" + App().WorkspaceForTests().WindowName(setup);
        ctx->WindowFocus(setupRef.c_str());
        ctx->Yield(3);
        IM_CHECK_GT(Colored(ctx->GetWindowByRef(setupRef.c_str()), IM_COL32(213, 234, 255, 255), true), 0);
        IM_CHECK_GT(Colored(ctx->GetWindowByRef(setupRef.c_str()), IM_COL32(201, 33, 123, 255), true), 0);
        ctx->WindowFocus("//Snapshot Buffer");
        ctx->Yield(3);
        IM_CHECK_GT(Colored(ctx->GetWindowByRef("//Snapshot Buffer"), IM_COL32(50, 67, 84, 255)), 0);
        const auto logRef = "//" + App().WorkspaceForTests().WindowName(log);
        ctx->WindowFocus(logRef.c_str());
        ctx->Yield(3);
        const float scale = ImGui::GetFontSize() / 13;
        IM_CHECK(std::fabs(ctx->ItemInfo((logRef + "/##caption").c_str()).RectFull.GetHeight() - (31 * scale + 1)) < 1);
        IM_CHECK(std::fabs(ctx->GetWindowByRef(logRef.c_str())->WindowRounding - 10 * scale) < 1);
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            IM_CHECK_NO_RET(state.document->Token() == token && state.History().UndoCount() == history &&
                            state.History().CanRedo() && state.document->Dirty() == dirty &&
                            state.objectSelection.References() == selection);
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
        });
        ctx->WindowFocus(SchematicRef().c_str());
        ctx->Yield(4);
        IM_CHECK_GT(Colored(ctx->GetWindowByRef(SchematicRef().c_str()), IM_COL32(176, 176, 176, 255)), 0);
        ctx->WindowFocus(setupRef.c_str());
        ctx->Yield(3);
        IM_CHECK_GT(Colored(ctx->GetWindowByRef(setupRef.c_str()), IM_COL32(255, 0, 0, 255), true), 0);
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            IM_CHECK_NO_RET(state.document->Token() == token && state.History().UndoCount() == history &&
                            state.History().CanRedo() && state.document->Dirty() == dirty &&
                            state.objectSelection.References() == selection);
        });
    };
    auto *layout = IM_REGISTER_TEST(engine, "appearance", "setup_reflows_and_keeps_edits");
    layout->SetVarsDataType<PageExtentProbe>();
    layout->GuiFunc = [](ImGuiTestContext *ctx) {
        MainThreadGui(ctx);
        if (!ctx->GetVars<PageExtentProbe>().visible) return;
        ImGui::SetNextWindowSize({260, 160}, ImGuiCond_Always);
        if (ImGui::Begin("Setup extent probe", nullptr,
                         ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoSavedSettings)) {
            vdev::setup::DevPage page;
            page.Label(0, 0, 20, 20,
                       "A long object name must remain reachable even when it exceeds the original page width");
            page.End(20, 20);
        }
        ImGui::End();
    };
    layout->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        EditorView sceneView = 0, frameView = 0, spriteView = 0;
        ObjectRef scene;
        EditorConfiguration original;
        size_t history = 0;
        OnMainThread(ctx, [&] {
            auto &workspace = App().WorkspaceForTests();
            auto client = EditorCommands();
            const auto tree = client.Query("scene.tree");
            const auto *levelScene = EditorClient::Find(tree, "level-scene");
            IM_CHECK_NO_RET(levelScene && levelScene->object);
            scene = levelScene->object;
            IM_CHECK_NO_RET(client.Command("object.open-setup", {ObjectArgument("target", scene)}).status <=
                            EDITOR_NO_CHANGE);
            sceneView = workspace.Role("setupscene");
            const auto made = client.Command("object.create-frame3d");
            const auto *frame = EditorClient::Find(made, "created");
            IM_CHECK_NO_RET(frame && frame->object);
            IM_CHECK_NO_RET(client.Command("object.open-setup", {ObjectArgument("target", frame->object)}).status <=
                            EDITOR_NO_CHANGE);
            frameView = workspace.Role("setup3d");
            // Test content unavailable from the built-in creation menu.
            auto &document = *App().State().document;
            auto *sprite = document.Context()->CreateObject(CKCID_SPRITE3D, "Setup layout sprite");
            IM_CHECK_NO_RET(sprite);
            document.Include(sprite->GetID());
            IM_CHECK_NO_RET(
                client.Command("object.open-setup", {ObjectArgument("target", document.Reference(sprite->GetID()))})
                    .status <= EDITOR_NO_CHANGE);
            spriteView = workspace.Role("setupsprite3d");
            IM_CHECK_NO_RET(sceneView && frameView && spriteView);
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(config, error));
            history = App().State().History().UndoCount();
        });
        const auto sceneName = App().WorkspaceForTests().WindowName(sceneView);
        const auto ref = "//" + sceneName;
        ctx->Yield(4);
        ImGuiID fogId = 0;
        ImGuiID fogStartId = 0;
        for (float width : {1350.f, 690.f, 450.f, 1350.f}) {
            OnMainThread(ctx, [&] {
                ImGui::SetWindowDock(ImGui::FindWindowByName(sceneName.c_str()), 0, ImGuiCond_Always);
                ImGui::SetWindowPos(sceneName.c_str(), {10, 60});
                ImGui::SetWindowSize(sceneName.c_str(), {width, 680});
            });
            ctx->WindowFocus(ref.c_str());
            ctx->Yield(4);
            ImGuiWindow *page = nullptr;
            for (auto *child : ctx->GetWindowByRef(ref.c_str())->DC.ChildWindows)
                if (std::string(child->Name).find("/page_") != std::string::npos) page = child;
            IM_CHECK(page);
            const auto *table = ImGui::GetCurrentContext()->Tables.GetByKey(page->GetID("##scene-sections"));
            IM_CHECK(table);
            IM_CHECK_EQ(table->ColumnsCount, width > 1000 ? 2 : 1);
            const auto background = ctx->ItemInfo(page->GetID("##background")).RectFull;
            const auto &column = table->Columns[3 % table->ColumnsCount];
            IM_CHECK(background.Min.x >= column.WorkMinX - 1 && background.Max.x <= column.WorkMaxX + 1);
            const auto fog = ctx->ItemInfo(page->GetID("##fogtype")).ID;
            IM_CHECK(!fogId || fog == fogId);
            fogId = fog;
            fogStartId = ImHashStr("##value", 0, ImHashStr("Fog Start:", 0, page->ID));
            if (width == 450) IM_CHECK_GT(page->ScrollMax.x, 0);
        }
        for (const auto view : {frameView, spriteView}) {
            const auto name = App().WorkspaceForTests().WindowName(view), windowRef = "//" + name;
            OnMainThread(ctx, [&] {
                ImGui::SetWindowDock(ImGui::FindWindowByName(name.c_str()), 0, ImGuiCond_Always);
                ImGui::SetWindowSize(name.c_str(), {490, 680});
            });
            ctx->WindowFocus(windowRef.c_str());
            ctx->Yield(4);
            ImGuiWindow *page = nullptr;
            for (auto *child : ctx->GetWindowByRef(windowRef.c_str())->DC.ChildWindows)
                if (std::string(child->Name).find("/page_") != std::string::npos) page = child;
            IM_CHECK(page);
            const int axis = 0;
            const auto component = ImHashData(&axis, sizeof(axis), ImHashStr("Position", 0, page->ID));
            IM_CHECK_GT(
                ctx->ItemInfo(ImHashStr(view == frameView ? "##world" : "##v", 0, component)).RectFull.GetWidth(), 1);
        }
        ctx->WindowFocus(ref.c_str());
        ctx->Yield(3);
        const auto mode = Read<uint64_t>(ctx, [&] {
            SetupDetails data;
            IM_CHECK_NO_RET(ReadSetupProperties(EditorCommands(), scene, {"scene.fog-mode"}, data));
            return data.Values().Integer("scene.fog-mode");
        });
        ChooseInCombo(ctx, ref.c_str(), "##fogtype", mode == 3 ? "Exponential" : "Linear");
        ctx->Yield(3);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        const auto start = [&] {
            SetupDetails data;
            ReadSetupProperties(EditorCommands(), scene, {"scene.fog-start"}, data);
            return data.Values().Real("scene.fog-start");
        };
        const auto before = Read<double>(ctx, start);
        ctx->SetRef(ref.c_str());
        const auto at = ctx->ItemInfo(fogStartId).RectFull.GetCenter();
        ctx->MouseMoveToPos(at);
        ctx->MouseDown(0);
        ctx->MouseMoveToPos({at.x + 40, at.y});
        ctx->Yield(3);
        IM_CHECK(std::fabs(Read<double>(ctx, start) - before) > .001);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->MouseUp(0);
        ctx->Yield(3);
        IM_CHECK(std::fabs(Read<double>(ctx, start) - before) < .001);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        ctx->ItemInputValue(fogStartId, float(before + 25));
        ctx->Yield(3);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 2);
        Undo(ctx);
        IM_CHECK(std::fabs(Read<double>(ctx, start) - before) < .001);
        ctx->GetVars<PageExtentProbe>().visible = true;
        ctx->Yield(4);
        IM_CHECK_GT(ctx->GetWindowByRef("//Setup extent probe")->ScrollMax.x, 100);
        ctx->GetVars<PageExtentProbe>().visible = false;
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *render = IM_REGISTER_TEST(engine, "appearance", "render_setup_reflows_and_keeps_edits");
    render->GuiFunc = MainThreadGui;
    render->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        struct Page {
            const char *create, *role, *table, *input, *property;
            float typed, divisor;
            ObjectRef object{};
            EditorView view = 0;
            ImGuiID inputId = 0;
        } pages[] = {
            {"object.create-camera", "setupcamera", "##camera-settings", "##fov", "camera.fov", 70,
             (180.f / 3.14159265358979323846f)},
            {"object.create-light", "setuplight", "##light-settings", "##range", "light.range", 325, 1},
            {"object.create-material", "setupmaterial", "##material-settings", "##power", "material.power", 60, 1}};
        EditorConfiguration original;
        DocumentToken token;
        std::vector<ObjectRef> selection;
        OnMainThread(ctx, [&] {
            auto &workspace = App().WorkspaceForTests();
            auto client = EditorCommands();
            for (auto &page : pages) {
                const auto made = client.Command(page.create);
                const auto *created = EditorClient::Find(made, "created");
                IM_CHECK_NO_RET(made.status == EDITOR_OK && created && created->object);
                page.object = created->object;
                IM_CHECK_NO_RET(client.Command("object.open-setup", {ObjectArgument("target", page.object)}).status <=
                                EDITOR_NO_CHANGE);
                page.view = workspace.Role(page.role);
                IM_CHECK_NO_RET(page.view);
            }
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(config, error));
            token = App().State().document->Token();
            selection = App().State().objectSelection.References();
        });
        ctx->Yield(4);
        const auto windowName = [](const Page &page) { return App().WorkspaceForTests().WindowName(page.view); };
        const auto pageWindow = [&](const Page &page) -> ImGuiWindow * {
            const auto ref = "//" + windowName(page);
            for (auto *child : ctx->GetWindowByRef(ref.c_str())->DC.ChildWindows)
                if (std::string(child->Name).find("/page_") != std::string::npos) return child;
            return nullptr;
        };
        const auto resize = [&](const Page &page, float width) {
            OnMainThread(ctx, [&] {
                const auto name = windowName(page);
                ImGui::SetWindowDock(ImGui::FindWindowByName(name.c_str()), 0, ImGuiCond_Always);
                ImGui::SetWindowPos(name.c_str(), {10, 60});
                ImGui::SetWindowSize(name.c_str(), {width, 680});
            });
            ctx->Yield(4);
        };
        const auto number = [&](const Page &page) {
            return Read<double>(ctx, [&] {
                SetupDetails data;
                IM_CHECK_NO_RET(ReadSetupProperties(EditorCommands(), page.object, {page.property}, data));
                return data.Values().Real(page.property);
            });
        };
        const auto history = App().State().History().UndoCount();
        for (auto &page : pages) {
            const auto ref = "//" + windowName(page);
            ctx->WindowFocus(ref.c_str());
            for (const float width : {1500.f, 690.f, 450.f, 1500.f}) {
                resize(page, width);
                auto *window = pageWindow(page);
                IM_CHECK(window);
                auto *table = ImGui::GetCurrentContext()->Tables.GetByKey(window->GetID(page.table));
                IM_CHECK(table);
                IM_CHECK_EQ(table->ColumnsCount, width > 1000 ? 3 : 1);
                const auto id = window->GetID(page.input);
                IM_CHECK(!page.inputId || page.inputId == id);
                page.inputId = id;
                const auto field = ctx->ItemInfo(id).RectFull;
                IM_CHECK_GT(field.GetWidth(), 1);
                bool inside = false;
                for (int i = 0; i < table->ColumnsCount; ++i)
                    inside |=
                        field.Min.x >= table->Columns[i].WorkMinX - 1 && field.Max.x <= table->Columns[i].WorkMaxX + 1;
                IM_CHECK(inside);
                if (width == 450) IM_CHECK_GT(window->ScrollMax.x, 0);
            }
            ctx->WindowFocus(ref.c_str());
            ctx->Yield(3);
            ctx->SetRef(ref.c_str());
            const auto before = number(page);
            ctx->ScrollToItemY(page.inputId);
            ctx->Yield(3);
            const auto at = ctx->ItemInfo(page.inputId).RectFull.GetCenter();
            ctx->MouseMoveToPos(at);
            ctx->MouseDown(0);
            ctx->MouseMoveToPos({at.x + 35, at.y});
            ctx->Yield(3);
            IM_CHECK(std::fabs(number(page) - before) > .0001);
            resize(page, 690);
            IM_CHECK_EQ(pageWindow(page)->GetID(page.input), page.inputId);
            IM_CHECK_EQ(App().State().History().UndoCount(), history);
            ctx->KeyPress(ImGuiKey_Escape);
            ctx->MouseUp(0);
            ctx->Yield(3);
            IM_CHECK(std::fabs(number(page) - before) < .0001);
            IM_CHECK_EQ(App().State().History().UndoCount(), history);
            resize(page, 1500);
            ctx->ItemInputValue(page.inputId, page.typed);
            ctx->Yield(3);
            IM_CHECK(std::fabs(number(page) * page.divisor - page.typed) < .01);
            IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
            Undo(ctx);
            IM_CHECK(std::fabs(number(page) - before) < .0001);
            IM_CHECK_EQ(App().State().History().UndoCount(), history);
        }
        // Directional lights keep range and attenuation unavailable; spot angles stay in degrees on screen.
        const auto lightRef = "//" + windowName(pages[1]);
        ctx->WindowFocus(lightRef.c_str());
        ctx->Yield(3);
        ChooseInCombo(ctx, lightRef.c_str(), "##type", "Directional");
        ctx->Yield(3);
        IM_CHECK(ctx->ItemInfo(pageWindow(pages[1])->GetID("##range")).ItemFlags & ImGuiItemFlags_Disabled);
        IM_CHECK(ctx->ItemInfo(pageWindow(pages[1])->GetID("##constant")).ItemFlags & ImGuiItemFlags_Disabled);
        Undo(ctx);
        ChooseInCombo(ctx, lightRef.c_str(), "##type", "Spot");
        ctx->Yield(3);
        const auto hotSpot = pageWindow(pages[1])->GetID("##hotspot");
        IM_CHECK(!(ctx->ItemInfo(hotSpot).ItemFlags & ImGuiItemFlags_Disabled));
        ctx->ItemInputValue(hotSpot, 45.f);
        ctx->Yield(3);
        const auto angle = Read<double>(ctx, [&] {
            SetupDetails data;
            IM_CHECK_NO_RET(ReadSetupProperties(EditorCommands(), pages[1].object, {"light.hot-spot"}, data));
            return data.Values().Real("light.hot-spot");
        });
        IM_CHECK(std::fabs(angle * (180.f / 3.14159265358979323846f) - 45) < .01);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 2);
        Undo(ctx);
        Undo(ctx);
        // Custom is only a page choice; depth and texture controls remain reachable after reflow.
        const auto materialRef = "//" + windowName(pages[2]);
        ctx->WindowFocus(materialRef.c_str());
        ctx->Yield(3);
        ChooseInCombo(ctx, materialRef.c_str(), "##mode", "Custom");
        ctx->Yield(3);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        auto *materialPage = pageWindow(pages[2]);
        IM_CHECK_GT(ctx->ItemInfo(materialPage->GetID("##zfunc")).RectFull.GetWidth(), 1);
        ChooseInCombo(ctx, materialRef.c_str(), "##addressmode", "Border");
        ctx->Yield(3);
        const auto border = ImHashStr("Border Color", 0, ImHashStr("Border Color", 0, materialPage->ID));
        IM_CHECK_GT(ctx->ItemInfo(border).RectFull.GetWidth(), 1);
        Undo(ctx);
        const auto diffuse = [&] {
            return Read<std::vector<double>>(ctx, [&] {
                SetupDetails data;
                IM_CHECK_NO_RET(ReadSetupProperties(EditorCommands(), pages[2].object, {"material.diffuse"}, data));
                return data.Values().Reals("material.diffuse");
            });
        };
        const auto color = diffuse();
        const auto swatch = ImHashStr("Diffuse", 0, ImHashStr("Diffuse", 0, materialPage->ID));
        ctx->ItemClick(swatch);
        ctx->Yield(3);
        resize(pages[2], 690);
        const auto popup = ImHashStr("picker", 0, ImHashStr("Diffuse", 0, materialPage->ID));
        IM_CHECK(ImGui::IsPopupOpen(popup, ImGuiPopupFlags_AnyPopupLevel));
        ctx->SetRef("//$FOCUSED");
        ctx->ItemClick("**/##picker/##hex/##Text");
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
        ctx->KeyChars("7F3344");
        ctx->Yield(3);
        IM_CHECK(diffuse() != color);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(4);
        IM_CHECK(diffuse() == color);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().State().document->Token() == token &&
                            App().State().objectSelection.References() == selection);
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *header = IM_REGISTER_TEST(engine, "appearance", "setup_header_reflow_navigation_and_rename");
    header->GuiFunc = MainThreadGui;
    header->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        EditorView view = 0;
        ObjectRef texture, other;
        EditorConfiguration original;
        std::string otherName, filename = "/tmp/";
        for (int i = 0; i < 80; ++i) filename += "long_header_component/";
        filename += "image.bmp";
        OnMainThread(ctx, [&] {
            auto &workspace = App().WorkspaceForTests();
            auto client = EditorCommands();
            for (auto *object : {&texture, &other}) {
                const auto made = client.Command("object.create-texture");
                const auto *created = EditorClient::Find(made, "created");
                IM_CHECK_NO_RET(created && created->object);
                *object = created->object;
            }
            // Image content and a filename longer than the header; no filesystem access is needed.
            auto *image = Object<CKTexture>(texture.object);
            IM_CHECK_NO_RET(image && image->Create(2, 2, 32) && image->SetSlotFileName(0, filename.data()));
            SetupPageData page;
            IM_CHECK_NO_RET(ReadSetupPage(client, other, -1, page));
            otherName = page.name;
            IM_CHECK_NO_RET(client.Command("object.open-setup", {ObjectArgument("target", texture)}).status <=
                            EDITOR_NO_CHANGE);
            view = workspace.Role("setuptexture");
            IM_CHECK_NO_RET(view);
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(config, error));
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        const auto token = App().State().document->Token();
        const auto selection = App().State().objectSelection.References();
        ImGuiID nameId = 0, replaceId = 0;
        for (const float width : {1500.f, 450.f, 260.f, 1500.f}) {
            ResizeSetup(ctx, view, width);
            auto *root = ctx->GetWindowByRef(SetupRef(view).c_str());
            auto *table = ctx->UiContext->Tables.GetByKey(root->GetID("##setup-header"));
            IM_CHECK(table);
            IM_CHECK(table->OuterRect.Min.x >= root->WorkRect.Min.x - 1 &&
                     table->OuterRect.Max.x <= root->WorkRect.Max.x + 1);
            IM_CHECK_LT(table->OuterRect.GetHeight(), 7 * ImGui::GetFrameHeight());
            IM_CHECK(!nameId || nameId == root->GetID("##name"));
            IM_CHECK(!replaceId || replaceId == root->GetID("...##texturefile"));
            nameId = root->GetID("##name");
            replaceId = root->GetID("...##texturefile");
            for (const auto id : {nameId, replaceId, ImHashStr("##bitmap", 0, root->GetID("picker")),
                                  ImHashStr("##bitmap", 0, root->GetID("scenefilter"))}) {
                const auto rect = ctx->ItemInfo(id).RectFull;
                IM_CHECK_GT(rect.GetWidth(), 1);
                IM_CHECK(rect.Min.x >= root->WorkRect.Min.x - 1 && rect.Max.x <= root->WorkRect.Max.x + 1);
                IM_CHECK(rect.Min.y >= table->OuterRect.Min.y && rect.Max.y <= table->OuterRect.Max.y + 1);
            }
            IM_CHECK(SetupPage(ctx, view)->Pos.y >= table->OuterRect.Max.y);
            IM_CHECK_EQ(App().State().History().UndoCount(), history);
        }
        ctx->SetRef(SetupRef(view).c_str());
        ctx->ItemClick("scenefilter/##bitmap");
        ctx->ItemClick("scenefilter/##bitmap");
        ctx->ItemClick(nameId);
        ctx->Yield(3);
        IM_CHECK(!ctx->UiContext->OpenPopupStack.empty());
        const auto popup = ctx->UiContext->OpenPopupStack.back().PopupId;
        ResizeSetup(ctx, view, 450, false);
        IM_CHECK(!ctx->UiContext->OpenPopupStack.empty() && ctx->UiContext->OpenPopupStack.back().PopupId == popup);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->WindowFocus(SetupRef(view).c_str());
        ctx->SetRef(SetupRef(view).c_str());
        ctx->ComboClick(("##name/" + otherName).c_str());
        ctx->Yield(3);
        OnMainThread(ctx, [&] {
            const auto info = EditorCommands().Inspect(view);
            const auto *target = EditorClient::Find(info, "target");
            IM_CHECK_NO_RET(target && target->object == other);
        });
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        const auto beforeName = Read<std::string>(ctx, [&] {
            SetupPageData page;
            ReadSetupPage(EditorCommands(), other, -1, page);
            return page.name;
        });
        ctx->WindowFocus(SetupRef(view).c_str());
        ctx->KeyPress(ImGuiKey_F2);
        ctx->Yield(3);
        ctx->ItemInput("//###setuprename/##name");
        ctx->KeyCharsReplace("Cancelled header name");
        ResizeSetup(ctx, view, 260, false);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
        IM_CHECK(ObjectName(ctx, other.object) == beforeName);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ctx->WindowFocus(SetupRef(view).c_str());
        ctx->KeyPress(ImGuiKey_F2);
        ctx->Yield(3);
        ctx->ItemInputValue("//###setuprename/##name", "Accepted header name");
        ctx->Yield(3);
        IM_CHECK(ObjectName(ctx, other.object) == "Accepted header name");
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK(ObjectName(ctx, other.object) == beforeName);
        ctx->WindowFocus(SetupRef(view).c_str());
        ctx->SetRef(SetupRef(view).c_str());
        ctx->ItemClick("picker/##bitmap");
        OnMainThread(ctx, [&] {
            auto &workspace = App().WorkspaceForTests();
            IM_CHECK_NO_RET(workspace.Close(view));
            IM_CHECK_NO_RET(EditorCommands().Command("object.open-setup", {ObjectArgument("target", texture)}).status <=
                            EDITOR_NO_CHANGE);
            view = workspace.Role("setuptexture");
            IM_CHECK_NO_RET(view && !workspace.Application().HasEdits());
            SetupPageData page;
            IM_CHECK_NO_RET(ReadSetupPage(EditorCommands(), texture, -1, page) && page.filename == filename);
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(original, error));
            IM_CHECK_NO_RET(App().State().document->Token() == token &&
                            App().State().objectSelection.References() == selection);
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *entity = IM_REGISTER_TEST(engine, "appearance", "entity3d_reflow_cancel_and_mesh_selection");
    entity->GuiFunc = MainThreadGui;
    entity->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        ObjectRef object, first, second;
        EditorView view = 0;
        EditorConfiguration original;
        OnMainThread(ctx, [&] {
            auto &document = *App().State().document;
            auto *context = document.Context();
            // A mesh-bearing object is not available from the built-in creation menu.
            auto *made = CK3dObject::Cast(context->CreateObject(CKCID_3DOBJECT, "Entity layout object"));
            auto *a = CKMesh::Cast(context->CreateObject(CKCID_MESH, "Entity layout first mesh"));
            auto *b = CKMesh::Cast(context->CreateObject(CKCID_MESH, "Entity layout second mesh"));
            IM_CHECK_NO_RET(made && a && b);
            for (auto *value : {static_cast<CKObject *>(made), static_cast<CKObject *>(a), static_cast<CKObject *>(b)})
                document.Include(value->GetID());
            made->AddMesh(a);
            made->AddMesh(b);
            made->SetCurrentMesh(a);
            made->SetPosition3f(10, 20, 30);
            object = document.Reference(made->GetID());
            first = document.Reference(a->GetID());
            second = document.Reference(b->GetID());
            auto &workspace = App().WorkspaceForTests();
            IM_CHECK_NO_RET(EditorCommands().Command("object.open-setup", {ObjectArgument("target", object)}).status <=
                            EDITOR_NO_CHANGE);
            view = workspace.Role("setup3d");
            IM_CHECK_NO_RET(view);
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(config, error));
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        ImGuiID input = 0;
        for (const float width : {1500.f, 690.f, 450.f, 1500.f}) {
            ResizeSetup(ctx, view, width);
            auto *page = SetupPage(ctx, view);
            IM_CHECK(page);
            for (const auto *id : {"##entity-transform", "##entity-details"}) {
                const auto *table = ctx->UiContext->Tables.GetByKey(page->GetID(id));
                IM_CHECK(table);
                IM_CHECK_EQ(table->ColumnsCount, width > 1000 ? 3 : 1);
            }
            const int axis = 0;
            const auto id = ImHashStr("##v", 0, ImHashData(&axis, sizeof(axis), page->GetID("Position")));
            IM_CHECK(!input || input == id);
            input = id;
            IM_CHECK_GT(ctx->ItemInfo(id).RectFull.GetWidth(), 1);
            if (width == 450) IM_CHECK_GT(page->ScrollMax.x, 0);
        }
        const auto position = [&] {
            return Read<std::vector<double>>(ctx, [&] {
                SetupDetails data;
                ReadSetupProperties(EditorCommands(), object, {"entity3d.world-position"}, data);
                return data.Values().Reals("entity3d.world-position");
            });
        };
        const auto before = position();
        IM_CHECK_EQ(before.size(), 3);
        ctx->SetRef(SetupRef(view).c_str());
        ctx->ScrollToItemY(input);
        const auto at = ctx->ItemInfo(input).RectFull.GetCenter();
        ctx->MouseMoveToPos(at);
        ctx->MouseDown(0);
        ctx->MouseMoveToPos({at.x + 40, at.y});
        ctx->Yield(3);
        IM_CHECK(position() != before);
        ResizeSetup(ctx, view, 690, false);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->MouseUp(0);
        ctx->Yield(3);
        IM_CHECK(position() == before);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ResizeSetup(ctx, view, 1500);
        ctx->ItemInputValue(input, float(before[0] + 12.5));
        ctx->Yield(3);
        const auto after = position();
        IM_CHECK(std::fabs(after[0] - before[0] - 12.5) < .001 && after[1] == before[1] && after[2] == before[2]);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK(position() == before);
        ctx->SetRef(SetupRef(view).c_str());
        ctx->ItemClick("**/Entity layout first mesh (Mesh)");
        ctx->KeyDown(ImGuiMod_Ctrl);
        ctx->ItemClick("**/Entity layout second mesh (Mesh)");
        ctx->KeyUp(ImGuiMod_Ctrl);
        ResizeSetup(ctx, view, 690);
        ctx->ScrollToBottom(SetupPage(ctx, view)->ID);
        ctx->Yield(3);
        ctx->SetRef(SetupRef(view).c_str());
        ctx->ItemClick("**/Entity layout second mesh (Mesh)", ImGuiMouseButton_Right);
        ctx->Yield(2);
        ctx->SetRef("//$FOCUSED");
        ctx->ItemClick("Remove Mesh(es)");
        ctx->Yield(3);
        const auto meshes = [&] {
            return Read<std::vector<ObjectRef>>(ctx, [&] {
                SetupDetails data;
                ReadSetupProperties(EditorCommands(), object, {"entity3d.meshes"}, data);
                return data.Values().Objects("entity3d.meshes");
            });
        };
        IM_CHECK(meshes().empty());
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        ctx->SetRef(SetupRef(view).c_str());
        Undo(ctx);
        const auto restored = meshes();
        IM_CHECK(restored.size() == 2 && std::find(restored.begin(), restored.end(), first) != restored.end() &&
                 std::find(restored.begin(), restored.end(), second) != restored.end());
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *entity2d = IM_REGISTER_TEST(engine, "appearance", "entity2d_reflow_cancel_and_object_choices");
    entity2d->GuiFunc = MainThreadGui;
    entity2d->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        ObjectRef object, parent, material;
        EditorView view = 0;
        EditorConfiguration original;
        std::string parentName, materialName, ownName;
        OnMainThread(ctx, [&] {
            auto client = EditorCommands();
            for (const auto &entry :
                 {std::make_pair("object.create-frame2d", &parent), std::make_pair("object.create-material", &material),
                  std::make_pair("object.create-frame2d", &object)}) {
                const auto made = client.Command(entry.first);
                const auto *created = EditorClient::Find(made, "created");
                IM_CHECK_NO_RET(created && created->object);
                *entry.second = created->object;
            }
            SetupPageData page;
            IM_CHECK_NO_RET(ReadSetupPage(client, parent, -1, page));
            parentName = page.name;
            IM_CHECK_NO_RET(ReadSetupPage(client, material, -1, page));
            materialName = page.name;
            IM_CHECK_NO_RET(ReadSetupPage(client, object, -1, page));
            ownName = page.name;
            IM_CHECK_NO_RET(client.Command("object.open-setup", {ObjectArgument("target", object)}).status <=
                            EDITOR_NO_CHANGE);
            auto &workspace = App().WorkspaceForTests();
            view = workspace.Role("setup2d");
            IM_CHECK_NO_RET(view);
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(config, error));
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        ImGuiID input = 0;
        for (const float width : {1500.f, 690.f, 450.f, 1500.f}) {
            ResizeSetup(ctx, view, width);
            auto *page = SetupPage(ctx, view);
            const auto *table = ctx->UiContext->Tables.GetByKey(page->GetID("##entity2d-settings"));
            IM_CHECK(table);
            IM_CHECK_EQ(table->ColumnsCount, width > 1000 ? 3 : 1);
            const int axis = 0;
            const auto id = ImHashData(&axis, sizeof(axis), page->GetID("X / Y"));
            IM_CHECK(!input || input == id);
            input = id;
            for (const auto &field : {std::make_pair("X / Y", 2), std::make_pair("Width / Height", 2),
                                      std::make_pair("Z Order", 1), std::make_pair("L / T / R / B", 4)}) {
                for (int component = 0; component < field.second; ++component) {
                    const auto id = field.second == 1
                                        ? page->GetID(field.first)
                                        : ImHashData(&component, sizeof(component), page->GetID(field.first));
                    const auto rect = ctx->ItemInfo(id).RectFull;
                    bool inside = false;
                    for (int i = 0; i < table->ColumnsCount; ++i)
                        inside |= rect.Min.x >= table->Columns[i].WorkMinX - 1 &&
                                  rect.Max.x <= table->Columns[i].WorkMaxX + 1;
                    IM_CHECK(inside);
                }
            }
            if (width == 450) IM_CHECK_GT(page->ScrollMax.x, 0);
        }
        const auto position = [&] {
            return Read<std::vector<double>>(ctx, [&] {
                SetupDetails data;
                ReadSetupProperties(EditorCommands(), object, {"entity2d.position"}, data);
                return data.Values().Reals("entity2d.position");
            });
        };
        const auto before = position();
        IM_CHECK_EQ(before.size(), 2);
        ctx->SetRef(SetupRef(view).c_str());
        ctx->ScrollToItemY(input);
        const auto at = ctx->ItemInfo(input).RectFull.GetCenter();
        ctx->MouseMoveToPos(at);
        ctx->MouseDown(0);
        ctx->MouseMoveToPos({at.x + 40, at.y});
        ctx->Yield(3);
        IM_CHECK(position() != before);
        ResizeSetup(ctx, view, 690, false);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->MouseUp(0);
        ctx->Yield(3);
        IM_CHECK(position() == before);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ResizeSetup(ctx, view, 1500);
        ctx->ItemInputValue(input, float(before[0] + .25));
        ctx->Yield(3);
        const auto after = position();
        IM_CHECK(std::fabs(after[0] - before[0] - .25) < .001 && after[1] == before[1]);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK(position() == before);
        const auto choice = [&](const char *property) {
            return Read<ObjectRef>(ctx, [&] {
                SetupDetails data;
                ReadSetupProperties(EditorCommands(), object, {property}, data);
                return data.Values().Object(property);
            });
        };
        const auto originalMaterial = choice("entity2d.material"), originalParent = choice("entity2d.parent");
        const auto ref = SetupRef(view);
        ChooseInCombo(ctx, ref.c_str(), "##material", materialName.c_str());
        ctx->Yield(3);
        IM_CHECK(choice("entity2d.material") == material);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK(choice("entity2d.material") == originalMaterial);
        ChooseInCombo(ctx, ref.c_str(), "##parent", ownName.c_str());
        ctx->Yield(3);
        IM_CHECK(choice("entity2d.parent") == originalParent);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ChooseInCombo(ctx, ref.c_str(), "##parent", parentName.c_str());
        ctx->Yield(3);
        IM_CHECK(choice("entity2d.parent") == parent);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK(choice("entity2d.parent") == originalParent);
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *texture = IM_REGISTER_TEST(engine, "appearance", "texture_reflow_slots_previews_cancel_and_undo");
    texture->GuiFunc = MainThreadGui;
    texture->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        ObjectRef target;
        EditorView view = 0;
        EditorConfiguration original;
        OnMainThread(ctx, [&] {
            auto client = EditorCommands();
            const auto made = client.Command("object.create-texture");
            IM_CHECK_NO_RET(EditorClient::Find(made, "created"));
            target = EditorClient::Find(made, "created")->object;
            auto *image = Object<CKTexture>(target.object);
            IM_CHECK_NO_RET(image && image->Create(8, 8, 32, 0) && image->Create(8, 8, 32, 1));
            for (int slot = 0; slot < 2; ++slot) {
                image->SetCurrentSlot(slot);
                for (int y = 0; y < 8; ++y)
                    for (int x = 0; x < 8; ++x)
                        image->SetPixel(x, y, slot ? 0x7f123456 : 0xffabcdef);
            }
            image->SetCurrentSlot(0);
            image->SetPickThreshold(0);
            IM_CHECK_NO_RET(client.Command("object.open-setup", {ObjectArgument("target", target)}).status <=
                            EDITOR_NO_CHANGE);
            auto &workspace = App().WorkspaceForTests();
            view = workspace.Role("setuptexture");
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(view && workspace.Configure(config, error));
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        ImGuiID threshold = 0, preview = 0;
        for (const float width : {1500.f, 690.f, 450.f, 1500.f}) {
            ResizeSetup(ctx, view, width);
            ctx->ScrollToBottom(SetupPage(ctx, view)->ID);
            ctx->Yield(3);
            auto *page = SetupPage(ctx, view);
            for (const auto &row :
                 {std::make_pair("##bitmap-info", 3), std::make_pair("##texture-settings", 2),
                  std::make_pair("##texture-previews", 2)}) {
                auto *table = SetupTable(ctx, view, row.first);
                IM_CHECK(table);
                IM_CHECK_EQ(table->ColumnsCount, width > 1000 ? row.second : 1);
            }
            const auto id = page->GetID("##threshold");
            IM_CHECK(!threshold || threshold == id);
            threshold = id;
            const auto imageId = ImHashStr("##preview", 0, page->GetID("image"));
            IM_CHECK(!preview || preview == imageId);
            preview = imageId;
            const auto *table = SetupTable(ctx, view, "##texture-previews");
            for (const char *name : {"image", "alpha"}) {
                const auto rect = ctx->ItemInfo(ImHashStr("##preview", 0, page->GetID(name))).RectFull;
                bool inside = false;
                for (int i = 0; i < table->ColumnsCount; ++i)
                    inside |= rect.Min.x >= table->Columns[i].WorkMinX - 1 &&
                              rect.Max.x <= table->Columns[i].WorkMaxX + 1;
                IM_CHECK(inside && rect.GetWidth() > 1);
            }
            if (width == 450)
                IM_CHECK_GT(page->ScrollMax.x, 0);
        }
        const auto value = [&](const char *property) {
            return Read<int>(ctx, [&] {
                SetupDetails data;
                ReadSetupProperties(EditorCommands(), target, {property}, data);
                return int(data.Values().Integer(property));
            });
        };
        ctx->SetRef(SetupRef(view).c_str());
        ctx->ScrollToItemY(threshold);
        ctx->Yield(3);
        const int before = value("texture.pick-threshold");
        const auto at = ctx->ItemInfo(threshold).RectFull.GetCenter();
        ctx->MouseMoveToPos(at);
        ctx->MouseDown(0);
        ctx->MouseMoveToPos({at.x + 35, at.y});
        ctx->Yield(3);
        IM_CHECK(value("texture.pick-threshold") != before);
        ResizeSetup(ctx, view, 690, false);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->MouseUp(0);
        ctx->Yield(3);
        IM_CHECK_EQ(value("texture.pick-threshold"), before);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ResizeSetup(ctx, view, 1500);
        ctx->SetRef(SetupRef(view).c_str());
        const int typed = before < 128 ? 200 : 40;
        ctx->ItemInputValue(threshold, typed);
        ctx->Yield(3);
        IM_CHECK_EQ(value("texture.pick-threshold"), typed);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK_EQ(value("texture.pick-threshold"), before);
        ctx->ScrollToTop(SetupPage(ctx, view)->ID);
        ctx->Yield(3);
        ctx->ItemClick("**/Next");
        ctx->Yield(3);
        IM_CHECK_EQ(value("texture.current-slot"), 1);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK_EQ(value("texture.current-slot"), 0);
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *mesh = IM_REGISTER_TEST(engine, "appearance", "mesh_reflow_channels_patch_cancel_and_undo");
    mesh->GuiFunc = MainThreadGui;
    mesh->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        ObjectRef target, patch;
        EditorView view = 0;
        EditorConfiguration original;
        OnMainThread(ctx, [&] {
            auto &doc = *App().State().document;
            auto *made = CKMesh::Cast(doc.Context()->CreateObject(CKCID_MESH, "Layout channels mesh"));
            auto *subdivided =
                CKPatchMesh::Cast(doc.Context()->CreateObject(CKCID_PATCHMESH, "Layout patch mesh"));
            auto *a = CKMaterial::Cast(doc.Context()->CreateObject(CKCID_MATERIAL, "Layout channel first"));
            auto *b = CKMaterial::Cast(doc.Context()->CreateObject(CKCID_MATERIAL, "Layout channel second"));
            IM_CHECK_NO_RET(made && subdivided && a && b);
            subdivided->SetIterationCount(0);
            for (CKObject *value : {static_cast<CKObject *>(made), static_cast<CKObject *>(subdivided),
                                    static_cast<CKObject *>(a), static_cast<CKObject *>(b)})
                doc.Include(value->GetID());
            IM_CHECK_NO_RET(made->AddChannel(a) >= 0 && made->AddChannel(b) >= 0);
            target = doc.Reference(made->GetID());
            patch = doc.Reference(subdivided->GetID());
            auto &workspace = App().WorkspaceForTests();
            IM_CHECK_NO_RET(
                EditorCommands().Command("object.open-setup", {ObjectArgument("target", target)}).status <=
                EDITOR_NO_CHANGE);
            view = workspace.Role("setupmesh");
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(view && workspace.Configure(config, error));
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        ImGuiID channelsId = 0;
        for (const float width : {1500.f, 690.f, 450.f, 1500.f}) {
            ResizeSetup(ctx, view, width);
            for (const char *name : {"##mesh-settings", "##mesh-lists"}) {
                auto *table = SetupTable(ctx, view, name);
                IM_CHECK(table);
                IM_CHECK_EQ(table->ColumnsCount, width > 1000 ? 3 : 1);
            }
            ShowSetupTable(ctx, view, "channels");
            auto *table = SetupTable(ctx, view, "channels");
            IM_CHECK(table && table->InnerWindow);
            IM_CHECK(!channelsId || channelsId == table->ID);
            channelsId = table->ID;
            IM_CHECK_GT(table->InnerWindow->ScrollMax.x, 0);
            const auto *outer = SetupTable(ctx, view, "##mesh-lists");
            bool inside = false;
            for (int i = 0; i < outer->ColumnsCount; ++i)
                inside |= table->OuterRect.Min.x >= outer->Columns[i].WorkMinX - 1 &&
                          table->OuterRect.Max.x <= outer->Columns[i].WorkMaxX + 1;
            IM_CHECK(inside);
        }
        const auto channelValue = [&](const char *property) {
            return Read<int>(ctx, [&] {
                SetupDetails data;
                ReadSetupProperties(EditorCommands(), target, {property}, data, {0});
                return int(data.Values().Integer(property));
            });
        };
        auto *table = SetupTable(ctx, view, "channels");
        ctx->ScrollToX(table->InnerWindow->ID, 0);
        ctx->Yield(3);
        const int row = 0;
        const auto seed = ImHashData(&row, sizeof(row), table->ID);
        const auto active = ImHashStr("##active", 0, seed);
        ctx->SetRef(SetupRef(view).c_str());
        const int wasActive = channelValue("mesh.channel-active");
        ctx->ItemClick(active);
        ctx->Yield(3);
        IM_CHECK_EQ(channelValue("mesh.channel-active"), !wasActive);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK_EQ(channelValue("mesh.channel-active"), wasActive);
        table = SetupTable(ctx, view, "channels");
        ctx->ScrollToX(table->InnerWindow->ID, table->InnerWindow->ScrollMax.x);
        ctx->Yield(3);
        const int blend = channelValue("mesh.channel-dest-blend"), desired = blend == 2 ? 1 : 2;
        ctx->ItemClick(ImHashStr("##dst", 0, seed));
        ctx->Yield(2);
        ctx->SetRef("//$FOCUSED");
        ctx->ItemClick(desired == 1 ? "**/Zero" : "**/One");
        ctx->Yield(3);
        IM_CHECK_EQ(channelValue("mesh.channel-dest-blend"), desired);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        ctx->SetRef(SetupRef(view).c_str());
        Undo(ctx);
        IM_CHECK_EQ(channelValue("mesh.channel-dest-blend"), blend);
        table = SetupTable(ctx, view, "channels");
        ctx->ScrollToX(table->InnerWindow->ID, 0);
        ctx->Yield(3);
        const auto selectChannel = [&](int index, const char *name, ImGuiMouseButton button = 0) {
            const auto rowId = ImHashData(&index, sizeof(index), channelsId);
            ctx->ItemClick(ImHashStr(name, 0, rowId), button);
        };
        selectChannel(0, "Layout channel first");
        ctx->KeyDown(ImGuiMod_Ctrl);
        selectChannel(1, "Layout channel second");
        ctx->KeyUp(ImGuiMod_Ctrl);
        ResizeSetup(ctx, view, 690);
        ShowSetupTable(ctx, view, "channels");
        selectChannel(1, "Layout channel second", ImGuiMouseButton_Right);
        ctx->SetRef("//$FOCUSED");
        ctx->ItemClick("**/Remove Channel(s)");
        ctx->Yield(3);
        const auto channelCount = [&] {
            return Read<int>(ctx, [&] {
                SetupDetails data;
                ReadSetupDetails(EditorCommands(), target, "mesh", data);
                return int(data.Values().Integer("channel-count"));
            });
        };
        IM_CHECK_EQ(channelCount(), 0);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        ctx->SetRef(SetupRef(view).c_str());
        Undo(ctx);
        IM_CHECK_EQ(channelCount(), 2);
        IM_CHECK_EQ(channelValue("mesh.channel-dest-blend"), blend);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(
                EditorCommands().Command("object.open-setup", {ObjectArgument("target", patch)}).status <=
                EDITOR_NO_CHANGE);
        });
        ResizeSetup(ctx, view, 1500);
        auto *page = SetupPage(ctx, view);
        const auto input = page->GetID("Iterations");
        ctx->ScrollToTop(page->ID);
        ctx->Yield(3);
        const auto iterations = [&] {
            return Read<int>(ctx, [&] {
                SetupDetails data;
                ReadSetupProperties(EditorCommands(), patch, {"patchmesh.iterations"}, data);
                return int(data.Values().Integer("patchmesh.iterations"));
            });
        };
        const int before = iterations();
        const auto at = ctx->ItemInfo(input).RectFull.GetCenter();
        ctx->MouseMoveToPos(at);
        ctx->MouseDown(0);
        ctx->MouseMoveToPos({at.x + 35, at.y});
        ctx->Yield(3);
        IM_CHECK(iterations() != before);
        ResizeSetup(ctx, view, 690, false);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->MouseUp(0);
        ctx->Yield(3);
        IM_CHECK_EQ(iterations(), before);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ResizeSetup(ctx, view, 1500);
        const int typed = before < 5 ? 7 : 2;
        ctx->ItemInputValue(input, typed);
        ctx->Yield(3);
        IM_CHECK_EQ(iterations(), typed);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK_EQ(iterations(), before);
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *grid = IM_REGISTER_TEST(engine, "appearance", "grid_reflow_multilayer_strokes_cancel_and_undo");
    grid->GuiFunc = MainThreadGui;
    grid->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        ObjectRef target;
        EditorView view = 0;
        EditorConfiguration original;
        int first = -1, second = -1;
        OnMainThread(ctx, [&] {
            auto client = EditorCommands();
            const auto made = client.Command("object.create-grid");
            IM_CHECK_NO_RET(EditorClient::Find(made, "created"));
            target = EditorClient::Find(made, "created")->object;
            for (const char *name : {"Layout grid first", "Layout grid second"}) {
                SetupDetails data;
                IM_CHECK_NO_RET(ReadSetupDetails(client, target, "grid", data));
                const auto registered =
                    client.Command("setup.grid-register-layer",
                                   {ObjectArgument("target", target), TextArgument("name", name),
                                    IntegerArgument("document-revision", data.documentRevision)});
                const auto *created = EditorClient::Find(registered, "created");
                IM_CHECK_NO_RET(registered.status == EDITOR_OK && created);
                const auto type = created->integer;
                IM_CHECK_NO_RET(ReadSetupDetails(client, target, "grid", data));
                IM_CHECK_NO_RET(client
                                    .Command("setup.grid-add-layer",
                                             {ObjectArgument("target", target), IntegerArgument("type", type),
                                              IntegerArgument("document-revision", data.documentRevision)})
                                    .status == EDITOR_OK);
                IM_CHECK_NO_RET(ReadSetupDetails(client, target, "grid", data));
                const auto &layers = data.Values().Rows("layers");
                for (size_t i = 0; i < layers.size(); ++i)
                    if (SetupFields(layers[i].fields).Integer("type") == type)
                        (first < 0 ? first : second) = int(i);
            }
            IM_CHECK_NO_RET(first >= 0 && second >= 0 && first != second);
            IM_CHECK_NO_RET(client.Command("object.open-setup", {ObjectArgument("target", target)}).status <=
                            EDITOR_NO_CHANGE);
            auto &workspace = App().WorkspaceForTests();
            view = workspace.Role("setupgrid");
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(view && workspace.Configure(config, error));
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        ImGuiID squares = 0;
        const auto canvas = [&]() -> ImGuiWindow * {
            auto *page = SetupPage(ctx, view);
            for (auto *child : page->DC.ChildWindows)
                if (child->ChildId == page->GetID("##grid-canvas"))
                    return child;
            return nullptr;
        };
        for (const float width : {1500.f, 690.f, 450.f, 1500.f}) {
            ResizeSetup(ctx, view, width);
            ctx->ScrollToTop(SetupPage(ctx, view)->ID);
            ctx->Yield(3);
            auto *page = SetupPage(ctx, view);
            auto *table = SetupTable(ctx, view, "##grid-sections");
            IM_CHECK(table);
            IM_CHECK_EQ(table->ColumnsCount, width > 1000 ? 3 : 1);
            const auto id = page->GetID("##squares");
            IM_CHECK(!squares || squares == id);
            squares = id;
            auto *child = canvas();
            IM_CHECK(child);
            IM_CHECK(child->Pos.x >= table->Columns[0].WorkMinX - 1 &&
                     child->Pos.x + child->Size.x <= table->Columns[0].WorkMaxX + 1);
            if (width == 450)
                IM_CHECK_GT(page->ScrollMax.x, 0);
        }
        ctx->SetRef(SetupRef(view).c_str());
        ShowSetupTable(ctx, view, "layers");
        const auto selectLayer = [&](int row, const char *name) {
            const auto seed = ImHashData(&row, sizeof(row), SetupTable(ctx, view, "layers")->ID);
            ctx->ItemClick(ImHashStr(name, 0, seed));
        };
        selectLayer(first, "Layout grid first");
        ctx->KeyDown(ImGuiMod_Ctrl);
        selectLayer(second, "Layout grid second");
        ctx->KeyUp(ImGuiMod_Ctrl);
        ctx->ScrollToTop(SetupPage(ctx, view)->ID);
        ctx->Yield(3);
        const auto cells = [&] {
            return Read<std::vector<std::vector<uint64_t>>>(ctx, [&] {
                SetupDetails data;
                ReadSetupDetails(EditorCommands(), target, "grid", data);
                std::vector<std::vector<uint64_t>> result;
                for (const auto &layer : data.Values().Rows("layers"))
                    result.push_back(SetupFields(layer.fields).Integers("squares"));
                return result;
            });
        };
        const auto before = cells();
        const auto beginStroke = [&] {
            const auto rect = ctx->ItemInfo(squares).RectFull;
            const ImVec2 at{rect.Min.x + rect.GetWidth() * .05f, rect.Min.y + rect.GetHeight() * .05f};
            ctx->MouseMoveToPos(at);
            ctx->MouseDown(0);
            ctx->MouseMoveToPos({at.x + rect.GetWidth() * .15f, at.y});
            ctx->Yield(3);
        };
        beginStroke();
        const auto preview = cells();
        IM_CHECK(preview != before && preview[size_t(first)] == preview[size_t(second)]);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ResizeSetup(ctx, view, 690, false);
        IM_CHECK(cells() == preview);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->MouseUp(0);
        ctx->Yield(3);
        IM_CHECK(cells() == before);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ResizeSetup(ctx, view, 1500);
        ctx->ScrollToTop(SetupPage(ctx, view)->ID);
        ctx->Yield(3);
        beginStroke();
        ctx->MouseUp(0);
        ctx->Yield(3);
        const auto after = cells();
        IM_CHECK(after != before && after[size_t(first)] == after[size_t(second)]);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK(cells() == before);
        Redo(ctx);
        IM_CHECK(cells() == after);
        Undo(ctx);
        IM_CHECK(cells() == before);
        const auto priority = [&] {
            return Read<int>(ctx, [&] {
                SetupDetails data;
                ReadSetupProperties(EditorCommands(), target, {"grid.priority"}, data);
                return int(data.Values().Integer("grid.priority"));
            });
        };
        const auto input = SetupPage(ctx, view)->GetID("Priority");
        const int oldPriority = priority();
        ctx->ItemInputValue(input, oldPriority + 3);
        ctx->Yield(3);
        IM_CHECK_EQ(priority(), oldPriority + 3);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK_EQ(priority(), oldPriority);
        const auto length = [&] {
            return Read<int>(ctx, [&] {
                SetupDetails data;
                ReadSetupDetails(EditorCommands(), target, "grid", data);
                return int(data.Values().Integer("length"));
            });
        };
        const int oldLength = length();
        ctx->ItemClick(SetupPage(ctx, view)->GetID("Add Row (Top)"));
        ctx->Yield(3);
        IM_CHECK_EQ(length(), oldLength + 1);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK_EQ(length(), oldLength);
        IM_CHECK(cells() == before);
        ctx->ScrollToTop(SetupPage(ctx, view)->ID);
        ctx->Yield(3);
        ctx->ItemClick("**/3D Properties");
        ctx->Yield(3);
        IM_CHECK(SetupTable(ctx, view, "##entity-transform") && SetupTable(ctx, view, "##entity-details"));
        ctx->ItemClick("**/Grid Properties");
        ctx->Yield(3);
        IM_CHECK(cells() == before);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        OnMainThread(ctx, [&] { Object<CKGrid>(target.object)->SetDimensions(256, 256, 256, 256); });
        ResizeSetup(ctx, view, 450);
        auto *child = canvas();
        IM_CHECK(child);
        IM_CHECK_GT(child->ScrollMax.x, 0);
        IM_CHECK_GT(child->ScrollMax.y, 0);
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *sprite = IM_REGISTER_TEST(engine, "appearance", "sprite_reflow_picking_slots_cancel_and_undo");
    sprite->GuiFunc = MainThreadGui;
    sprite->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        ObjectRef target;
        EditorView view = 0;
        EditorConfiguration original;
        OnMainThread(ctx, [&] {
            auto &doc = *App().State().document;
            auto *image = CKSprite::Cast(doc.Context()->CreateObject(CKCID_SPRITE, "Reflow sprite"));
            IM_CHECK_NO_RET(image && image->Create(16, 16, 32, 0) && image->Create(16, 16, 32, 1));
            doc.Include(image->GetID());
            for (int slot = 0; slot < 2; ++slot) {
                image->SetCurrentSlot(slot);
                for (int y = 0; y < 16; ++y)
                    for (int x = 0; x < 16; ++x)
                        image->SetPixel(x, y,
                                        0xff000000u | uint32_t(x * 16) << 16 | uint32_t(y * 16) << 8 |
                                            uint32_t(slot * 32));
            }
            image->SetCurrentSlot(0);
            image->SetSaveOptions(CKTEXTURE_RAWDATA);
            image->SetTransparentColor(0xff010203);
            target = doc.Reference(image->GetID());
            IM_CHECK_NO_RET(
                EditorCommands().Command("object.open-setup", {ObjectArgument("target", target)}).status <=
                EDITOR_NO_CHANGE);
            auto &workspace = App().WorkspaceForTests();
            view = workspace.Role("setupsprite2d");
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(view && workspace.Configure(config, error));
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        ImGuiID preview = 0;
        for (const float width : {1500.f, 690.f, 450.f, 1500.f}) {
            ResizeSetup(ctx, view, width);
            ShowSetupTable(ctx, view, "##sprite-settings");
            auto *page = SetupPage(ctx, view);
            auto *table = SetupTable(ctx, view, "##sprite-settings");
            IM_CHECK(table);
            IM_CHECK_EQ(table->ColumnsCount, width > 1000 ? 2 : 1);
            const auto id = ImHashStr("##preview", 0, page->GetID("image"));
            IM_CHECK(!preview || preview == id);
            preview = id;
            const auto rect = ctx->ItemInfo(preview).RectFull;
            const int column = table->ColumnsCount - 1;
            IM_CHECK(rect.Min.x >= table->Columns[column].WorkMinX - 1 &&
                     rect.Max.x <= table->Columns[column].WorkMaxX + 1);
            IM_CHECK(SetupTable(ctx, view, "##entity2d-settings"));
            if (width == 450)
                IM_CHECK_GT(page->ScrollMax.x, 0);
        }
        const auto integer = [&](const char *property) {
            return Read<int>(ctx, [&] {
                SetupDetails data;
                ReadSetupProperties(EditorCommands(), target, {property}, data);
                return int(data.Values().Integer(property));
            });
        };
        const auto color = [&] {
            return Read<std::vector<double>>(ctx, [&] {
                SetupDetails data;
                ReadSetupProperties(EditorCommands(), target, {"sprite.transparent-color"}, data);
                return data.Values().Reals("sprite.transparent-color");
            });
        };
        const auto ref = SetupRef(view);
        ChooseInCombo(ctx, ref.c_str(), "##saveoptions", "External");
        ctx->Yield(3);
        IM_CHECK_EQ(integer("sprite.save-options"), CKTEXTURE_EXTERNAL);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        ctx->SetRef(ref.c_str());
        Undo(ctx);
        IM_CHECK_EQ(integer("sprite.save-options"), CKTEXTURE_RAWDATA);
        const auto before = color();
        const auto beginPicking = [&] {
            ctx->SetRef(ref.c_str());
            ShowSetupTable(ctx, view, "##sprite-settings");
            ctx->ItemClick("**/Pick");
            ctx->Yield(3);
            ctx->ScrollToItemY(preview);
            ctx->Yield(3);
            const auto rect = ctx->ItemInfo(preview).RectFull;
            const auto at = rect.GetCenter();
            ctx->MouseMoveToPos(at);
            ctx->MouseDown(0);
            ctx->MouseMoveToPos({at.x + 25, at.y});
            ctx->Yield(3);
        };
        beginPicking();
        const auto picked = color();
        IM_CHECK(picked != before);
        const auto mouse = ctx->UiContext->IO.MousePos;
        IM_CHECK_EQ(ctx->UiContext->ActiveId, preview);
        ResizeSetup(ctx, view, 1300, false);
        IM_CHECK_EQ(ctx->UiContext->ActiveId, preview);
        IM_CHECK(ctx->UiContext->IO.MousePos.x == mouse.x && ctx->UiContext->IO.MousePos.y == mouse.y);
        IM_CHECK(ctx->UiContext->IO.MouseDelta.x == 0 && ctx->UiContext->IO.MouseDelta.y == 0);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        IM_CHECK(color() == picked);
        ctx->MouseMoveToPos({mouse.x - 25, mouse.y + 30});
        ctx->Yield(3);
        IM_CHECK(color() != picked);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->MouseUp(0);
        ctx->Yield(3);
        IM_CHECK(color() == before);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ResizeSetup(ctx, view, 1500);
        beginPicking();
        ctx->MouseUp(0);
        ctx->Yield(3);
        const auto after = color();
        IM_CHECK(after != before);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK(color() == before);
        Redo(ctx);
        IM_CHECK(color() == after);
        Undo(ctx);
        ctx->KeyPress(ImGuiKey_N);
        ctx->Yield(3);
        IM_CHECK_EQ(integer("sprite.current-slot"), 1);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK_EQ(integer("sprite.current-slot"), 0);
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *curve = IM_REGISTER_TEST(engine, "appearance", "curve_reflow_points_tools_cancel_and_undo");
    curve->GuiFunc = MainThreadGui;
    curve->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        ObjectRef target, points[3];
        EditorView view = 0;
        EditorConfiguration original;
        OnMainThread(ctx, [&] {
            auto client = EditorCommands();
            const auto result = client.Command("object.create-curve");
            IM_CHECK_NO_RET(EditorClient::Find(result, "created"));
            target = EditorClient::Find(result, "created")->object;
            auto &doc = *App().State().document;
            auto *made = Object<CKCurve>(target.object);
            IM_CHECK_NO_RET(made);
            for (int i = 0; i < 3; ++i) {
                auto *point = CKCurvePoint::Cast(doc.Context()->CreateObject(
                    CKCID_CURVEPOINT, ("Reflow point " + std::to_string(i)).c_str()));
                IM_CHECK_NO_RET(point);
                doc.Include(point->GetID());
                points[i] = doc.Reference(point->GetID());
                const VxVector position(float(i), float(i % 2), 0.f);
                point->SetPosition(&position);
                point->SetBias(0);
                point->SetTension(0);
                point->SetContinuity(0);
                point->SetLinear(FALSE);
                IM_CHECK_NO_RET(made->AddControlPoint(point) == CK_OK);
            }
            made->SetStepCount(10);
            made->SetFittingCoeff(0);
            made->Update();
            IM_CHECK_NO_RET(client.Command("object.open-setup", {ObjectArgument("target", target)}).status <=
                            EDITOR_NO_CHANGE);
            auto &workspace = App().WorkspaceForTests();
            view = workspace.Role("setupcurve");
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(view && workspace.Configure(config, error));
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        ImGuiID bias = 0, fitting = 0;
        for (const float width : {1500.f, 690.f, 450.f, 1500.f}) {
            ResizeSetup(ctx, view, width);
            ShowSetupTable(ctx, view, "##curve-settings");
            auto *page = SetupPage(ctx, view);
            auto *table = SetupTable(ctx, view, "##curve-settings");
            IM_CHECK(table);
            IM_CHECK_EQ(table->ColumnsCount, width > 1000 ? 3 : 1);
            IM_CHECK(!bias || bias == page->GetID("Bias"));
            bias = page->GetID("Bias");
            IM_CHECK(!fitting || fitting == page->GetID("Fitting Coef"));
            fitting = page->GetID("Fitting Coef");
            for (const auto name : {"Bias", "Tension", "Continuity", "Fitting Coef", "Step count",
                                    "Add Points", "##referential"}) {
                const auto rect = ctx->ItemInfo(page->GetID(name)).RectFull;
                bool inside = false;
                for (int i = 0; i < table->ColumnsCount; ++i)
                    inside |= rect.Min.x >= table->Columns[i].WorkMinX - 1 &&
                              rect.Max.x <= table->Columns[i].WorkMaxX + 1;
                IM_CHECK(inside);
            }
            if (width == 450)
                IM_CHECK_GT(page->ScrollMax.x, 0);
        }
        const auto value = [&](ObjectRef object, const char *property) {
            return Read<double>(ctx, [&] {
                SetupDetails data;
                ReadSetupProperties(EditorCommands(), object, {property}, data);
                return data.Values().Real(property);
            });
        };
        ctx->SetRef(SetupRef(view).c_str());
        ctx->ScrollToItemY(fitting);
        ctx->Yield(3);
        auto at = ctx->ItemInfo(fitting).RectFull.GetCenter();
        ctx->MouseMoveToPos(at);
        ctx->MouseDown(0);
        ctx->MouseMoveToPos({at.x + 25, at.y});
        ctx->Yield(3);
        IM_CHECK(std::fabs(value(target, "curve.fitting")) > .01);
        ResizeSetup(ctx, view, 690, false);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->MouseUp(0);
        ctx->Yield(3);
        IM_CHECK_EQ(value(target, "curve.fitting"), 0.0);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ResizeSetup(ctx, view, 1500);
        ctx->ItemInputValue(fitting, .25f);
        ctx->Yield(3);
        IM_CHECK(std::fabs(value(target, "curve.fitting") - .25) < .001);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK_EQ(value(target, "curve.fitting"), 0.0);
        ctx->ScrollToItemY(bias);
        ctx->Yield(3);
        at = ctx->ItemInfo(bias).RectFull.GetCenter();
        ctx->MouseMoveToPos(at);
        ctx->MouseDown(0);
        ctx->MouseMoveToPos({at.x + 25, at.y});
        ctx->Yield(3);
        IM_CHECK(std::fabs(value(points[0], "curvepoint.bias")) > .01);
        ResizeSetup(ctx, view, 690, false);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->MouseUp(0);
        ctx->Yield(3);
        IM_CHECK_EQ(value(points[0], "curvepoint.bias"), 0.0);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ResizeSetup(ctx, view, 1500);
        ShowSetupTable(ctx, view, "##curve-settings");
        ctx->ItemClick(SetupPage(ctx, view)->GetID(">"));
        ctx->Yield(3);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        for (const auto &field :
             {std::make_pair("Bias", "curvepoint.bias"), std::make_pair("Tension", "curvepoint.tension"),
              std::make_pair("Continuity", "curvepoint.continuity")}) {
            ctx->ItemInputValue(SetupPage(ctx, view)->GetID(field.first), .5f);
            ctx->Yield(3);
            IM_CHECK(std::fabs(value(points[1], field.second) - .5) < .001);
            IM_CHECK_EQ(value(points[0], field.second), 0.0);
            IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
            Undo(ctx);
            IM_CHECK_EQ(value(points[1], field.second), 0.0);
        }
        ctx->ItemClick(SetupPage(ctx, view)->GetID("Add Points"));
        ctx->Yield(3);
        const auto tool = [&] {
            return Read<int>(ctx, [&] {
                return int(EditorClient::Integer(EditorCommands().Query("layout.curve-tool"), "tool"));
            });
        };
        IM_CHECK_EQ(tool(), 128);
        ResizeSetup(ctx, view, 690);
        ShowSetupTable(ctx, view, "##curve-settings");
        ChooseInCombo(ctx, SetupRef(view).c_str(), "##referential", "View Plane");
        IM_CHECK_EQ(Read<int>(ctx,
                              [&] {
                                  return int(EditorClient::Integer(
                                      EditorCommands().Query("layout.curve-tool"), "plane"));
                              }),
                    1);
        ctx->SetRef(SetupRef(view).c_str());
        ctx->ItemClick(SetupPage(ctx, view)->GetID("Add Points"));
        ctx->Yield(3);
        IM_CHECK_EQ(tool(), 0);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *character =
        IM_REGISTER_TEST(engine, "appearance", "character_reflow_bodyparts_navigation_and_undo");
    character->GuiFunc = MainThreadGui;
    character->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        ObjectRef target, root, available, animation;
        EditorView view = 0;
        EditorConfiguration original;
        std::vector<std::string> labels;
        OnMainThread(ctx, [&] {
            auto &doc = *App().State().document;
            auto *context = doc.Context();
            auto *made = CKCharacter::Cast(context->CreateObject(CKCID_CHARACTER, "Reflow character"));
            IM_CHECK_NO_RET(made);
            doc.Include(made->GetID());
            target = doc.Reference(made->GetID());
            for (int i = 0; i < 3; ++i) {
                auto *part = CKBodyPart::Cast(
                    context->CreateObject(CKCID_BODYPART, ("Reflow body " + std::to_string(i)).c_str()));
                IM_CHECK_NO_RET(part);
                doc.Include(part->GetID());
                if (i < 2) {
                    IM_CHECK_NO_RET(made->AddBodyPart(part) == CK_OK);
                    if (i == 0) {
                        root = doc.Reference(part->GetID());
                        IM_CHECK_NO_RET(made->SetRootBodyPart(part) == CK_OK);
                    }
                } else
                    available = doc.Reference(part->GetID());
            }
            auto *walk = CKAnimation::Cast(context->CreateObject(CKCID_ANIMATION, "Reflow character walk"));
            auto *active = CKAnimation::Cast(context->CreateObject(
                CKCID_ANIMATION, ("Active animation " + std::string(150, 'A')).c_str()));
            IM_CHECK_NO_RET(walk && active);
            doc.Include(walk->GetID());
            doc.Include(active->GetID());
            animation = doc.Reference(walk->GetID());
            IM_CHECK_NO_RET(made->AddAnimation(walk) == CK_OK && made->AddAnimation(active) == CK_OK);
            IM_CHECK_NO_RET(made->SetActiveAnimation(active) == CK_OK);
            SetupDetails data;
            IM_CHECK_NO_RET(ReadSetupDetails(EditorCommands(), target, "character", data));
            root = data.Values().Object("root");
            for (const auto &row : data.Values().Rows("bodies")) {
                const SetupFields part(row.fields);
                labels.push_back(part.Text("name") + (part.Object("target") == root ? "  (Root)" : ""));
            }
            IM_CHECK_NO_RET(labels.size() == 2);
            IM_CHECK_NO_RET(
                EditorCommands().Command("object.open-setup", {ObjectArgument("target", target)}).status <=
                EDITOR_NO_CHANGE);
            auto &workspace = App().WorkspaceForTests();
            view = workspace.Role("setupcharacter");
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(view && workspace.Configure(config, error));
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        ImGuiID bodyparts = 0;
        for (const float width : {1500.f, 690.f, 450.f, 1500.f}) {
            ResizeSetup(ctx, view, width);
            ShowSetupTable(ctx, view, "bodyparts");
            auto *table = SetupTable(ctx, view, "##character-settings");
            IM_CHECK(table);
            IM_CHECK_EQ(table->ColumnsCount, width > 1000 ? 2 : 1);
            auto *list = SetupTable(ctx, view, "bodyparts");
            IM_CHECK(list && list->InnerWindow);
            IM_CHECK(!bodyparts || bodyparts == list->ID);
            bodyparts = list->ID;
            IM_CHECK_GT(list->InnerWindow->ScrollMax.x, 0);
            IM_CHECK(list->Flags & ImGuiTableFlags_Resizable);
            IM_CHECK(list->OuterRect.Min.x >= table->Columns[0].WorkMinX - 1 &&
                     list->OuterRect.Max.x <= table->Columns[0].WorkMaxX + 1);
            if (width == 450)
                IM_CHECK_GT(SetupPage(ctx, view)->ScrollMax.x, 0);
        }
        auto *table = SetupTable(ctx, view, "bodyparts");
        ctx->ScrollToX(table->InnerWindow->ID, 0);
        ctx->Yield(3);
        const float oldWidth = table->Columns[0].WidthGiven;
        const ImVec2 border{table->Columns[0].MaxX, table->InnerWindow->InnerRect.Min.y + 10};
        ctx->MouseMoveToPos(border);
        ctx->MouseDown(0);
        ctx->MouseMoveToPos({border.x + 35, border.y});
        ctx->MouseUp(0);
        ctx->Yield(3);
        table = SetupTable(ctx, view, "bodyparts");
        const float changedWidth = table->Columns[0].WidthGiven;
        IM_CHECK(std::fabs(changedWidth - oldWidth) > 10);
        ResizeSetup(ctx, view, 690);
        ShowSetupTable(ctx, view, "bodyparts");
        IM_CHECK(std::fabs(SetupTable(ctx, view, "bodyparts")->Columns[0].WidthGiven - changedWidth) < 3);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ResizeSetup(ctx, view, 1500);
        ShowSetupTable(ctx, view, "bodyparts");
        ctx->SetRef(SetupRef(view).c_str());
        const auto pickable = [&] {
            return Read<int>(ctx, [&] {
                SetupDetails data;
                ReadSetupProperties(EditorCommands(), target, {"entity3d.pickable"}, data);
                return int(data.Values().Integer("entity3d.pickable"));
            });
        };
        const int beforePick = pickable();
        ctx->ItemClick(SetupPage(ctx, view)->GetID("Pickable"));
        ctx->Yield(3);
        IM_CHECK_EQ(pickable(), !beforePick);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK_EQ(pickable(), beforePick);
        const auto select = [&](int row, ImGuiMouseButton button = 0) {
            const auto seed = ImHashData(&row, sizeof(row), bodyparts);
            ctx->ItemClick(ImHashStr(labels[size_t(row)].c_str(), 0, seed), button, ImGuiTestOpFlags_MoveToEdgeL);
        };
        ShowSetupTable(ctx, view, "bodyparts");
        select(0);
        ctx->KeyDown(ImGuiMod_Ctrl);
        select(1);
        ctx->KeyUp(ImGuiMod_Ctrl);
        ResizeSetup(ctx, view, 690);
        ShowSetupTable(ctx, view, "bodyparts");
        select(1, ImGuiMouseButton_Right);
        ctx->SetRef("//$FOCUSED");
        ctx->ItemClick("**/Remove Body Part");
        ctx->Yield(3);
        const auto bodies = [&] {
            return Read<std::vector<ObjectRef>>(ctx, [&] {
                SetupDetails data;
                ReadSetupDetails(EditorCommands(), target, "character", data);
                std::vector<ObjectRef> result;
                for (const auto &row : data.Values().Rows("bodies"))
                    result.push_back(SetupFields(row.fields).Object("target"));
                return result;
            });
        };
        IM_CHECK(bodies().empty());
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        ctx->SetRef(SetupRef(view).c_str());
        Undo(ctx);
        IM_CHECK_EQ(bodies().size(), 2);
        IM_CHECK(Read<ObjectRef>(ctx, [&] {
                     SetupDetails data;
                     ReadSetupDetails(EditorCommands(), target, "character", data);
                     return data.Values().Object("root");
                 }) == root);
        const auto openAdd = [&] {
            ctx->SetRef(SetupRef(view).c_str());
            ShowSetupTable(ctx, view, "bodyparts");
            auto *list = SetupTable(ctx, view, "bodyparts");
            ctx->MouseMoveToPos({list->OuterRect.Min.x + 15, list->OuterRect.Max.y - 35});
            ctx->MouseClick(ImGuiMouseButton_Right);
            ctx->Yield(3);
            ctx->SetRef("//$FOCUSED");
            ctx->ItemClick("**/Add Body Part");
            ctx->Yield(3);
        };
        openAdd();
        const auto popup = ctx->UiContext->OpenPopupStack.back().PopupId;
        ResizeSetup(ctx, view, 1500, false);
        IM_CHECK(!ctx->UiContext->OpenPopupStack.empty() &&
                 ctx->UiContext->OpenPopupStack.back().PopupId == popup);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
        IM_CHECK_EQ(bodies().size(), 2);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        openAdd();
        ctx->SetRef("//$FOCUSED");
        ctx->ItemClick("**/Reflow body 2");
        ctx->Yield(3);
        const auto added = bodies();
        IM_CHECK_EQ(added.size(), 3);
        IM_CHECK(std::find(added.begin(), added.end(), available) != added.end());
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        ctx->SetRef(SetupRef(view).c_str());
        Undo(ctx);
        IM_CHECK_EQ(bodies().size(), 2);
        ctx->ScrollToBottom(SetupPage(ctx, view)->ID);
        ctx->Yield(3);
        ctx->ItemDoubleClick("**/Reflow character walk (Animation)");
        ctx->Yield(3);
        IM_CHECK(ShowsObject(ctx, animation.object));
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *sound = IM_REGISTER_TEST(engine, "appearance", "sound_reflow_spatial_edits_cancel_and_undo");
    sound->GuiFunc = MainThreadGui;
    sound->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        ObjectRef target, empty, attached;
        EditorView view = 0;
        EditorConfiguration original;
        std::string attachedName;
        OnMainThread(ctx, [&] {
            auto client = EditorCommands();
            const auto frame = client.Command("object.create-frame3d");
            IM_CHECK_NO_RET(EditorClient::Find(frame, "created"));
            attached = EditorClient::Find(frame, "created")->object;
            SetupPageData page;
            IM_CHECK_NO_RET(ReadSetupPage(client, attached, -1, page));
            attachedName = page.name;
            auto &doc = *App().State().document;
            auto *wave =
                CKWaveSound::Cast(doc.Context()->CreateObject(CKCID_WAVESOUND, "Reflow point sound"));
            auto *noSource =
                CKWaveSound::Cast(doc.Context()->CreateObject(CKCID_WAVESOUND, "Reflow empty sound"));
            IM_CHECK_NO_RET(wave && noSource);
            doc.Include(wave->GetID());
            doc.Include(noSource->GetID());
            target = doc.Reference(wave->GetID());
            empty = doc.Reference(noSource->GetID());
            wave->SetType(CK_WAVESOUND_POINT);
            CKWaveFormat format{1, 1, 22050, 44100, 2, 16, 0};
            IM_CHECK_NO_RET(wave->Create(CK_WAVESOUND_POINT, &format, 44100) == CK_OK);
            std::vector<CKBYTE> silence(44100);
            IM_CHECK_NO_RET(wave->WriteData(silence.data(), int(silence.size())) == CK_OK);
            wave->SetGain(.2f);
            wave->SetPitch(1.f);
            wave->SetLoopMode(TRUE);
            VxVector position{1, 2, 3}, direction{0, 0, 1};
            wave->PositionSound(nullptr, &position, &direction);
            wave->SetCone(30, 60, .2f);
            wave->SetMinMaxDistance(1, 100);
            IM_CHECK_NO_RET(client.Command("object.open-setup", {ObjectArgument("target", target)}).status <=
                            EDITOR_NO_CHANGE);
            auto &workspace = App().WorkspaceForTests();
            view = workspace.Role("setupsound");
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(view && workspace.Configure(config, error));
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        ImGuiID gain = 0, position = 0;
        for (const float width : {1500.f, 690.f, 450.f, 1500.f}) {
            ResizeSetup(ctx, view, width);
            ShowSetupTable(ctx, view, "##sound-settings");
            auto *page = SetupPage(ctx, view);
            auto *table = SetupTable(ctx, view, "##sound-settings");
            IM_CHECK(table);
            IM_CHECK_EQ(table->ColumnsCount, width > 1000 ? 3 : 1);
            IM_CHECK(!gain || gain == page->GetID("Gain"));
            gain = page->GetID("Gain");
            const int axis = 0;
            const auto x = ImHashData(&axis, sizeof(axis), page->GetID("Relative Position"));
            IM_CHECK(!position || position == x);
            position = x;
            for (const auto id : {gain, position, page->GetID("Outside gain"), page->GetID("##attached")}) {
                const auto rect = ctx->ItemInfo(id).RectFull;
                bool inside = false;
                for (int i = 0; i < table->ColumnsCount; ++i)
                    inside |= rect.Min.x >= table->Columns[i].WorkMinX - 1 &&
                              rect.Max.x <= table->Columns[i].WorkMaxX + 1;
                IM_CHECK(inside);
            }
            IM_CHECK(ctx->ItemInfo(page->GetID("Pan")).ItemFlags & ImGuiItemFlags_Disabled);
            if (width == 450)
                IM_CHECK_GT(page->ScrollMax.x, 0);
        }
        const auto real = [&](const char *property) {
            return Read<double>(ctx, [&] {
                SetupDetails data;
                ReadSetupProperties(EditorCommands(), target, {property}, data);
                return data.Values().Real(property);
            });
        };
        const auto vector = [&](const char *property) {
            return Read<std::vector<double>>(ctx, [&] {
                SetupDetails data;
                ReadSetupProperties(EditorCommands(), target, {property}, data);
                return data.Values().Reals(property);
            });
        };
        const auto drag = [&](ImGuiID id) {
            ctx->SetRef(SetupRef(view).c_str());
            ctx->ScrollToItemY(id);
            ctx->Yield(3);
            const auto at = ctx->ItemInfo(id).RectFull.GetCenter();
            ctx->MouseMoveToPos(at);
            ctx->MouseDown(0);
            ctx->MouseMoveToPos({at.x + 25, at.y});
            ctx->Yield(3);
        };
        const double gainBefore = real("wavesound.gain");
        drag(gain);
        IM_CHECK(std::fabs(real("wavesound.gain") - gainBefore) > .01);
        ResizeSetup(ctx, view, 690, false);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->MouseUp(0);
        ctx->Yield(3);
        IM_CHECK(std::fabs(real("wavesound.gain") - gainBefore) < .001);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ResizeSetup(ctx, view, 1500);
        ctx->ItemInputValue(gain, .65f);
        ctx->Yield(3);
        IM_CHECK(std::fabs(real("wavesound.gain") - .65) < .001);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK(std::fabs(real("wavesound.gain") - gainBefore) < .001);
        const auto before = vector("wavesound.position");
        drag(position);
        IM_CHECK(vector("wavesound.position") != before);
        ResizeSetup(ctx, view, 690, false);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->MouseUp(0);
        ctx->Yield(3);
        IM_CHECK(vector("wavesound.position") == before);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ResizeSetup(ctx, view, 1500);
        ctx->ItemInputValue(position, float(before[0] + .5));
        ctx->Yield(3);
        const auto after = vector("wavesound.position");
        IM_CHECK(std::fabs(after[0] - before[0] - .5) < .001 && after[1] == before[1] &&
                 after[2] == before[2]);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK(vector("wavesound.position") == before);
        const auto cone = vector("wavesound.cone");
        const auto outside = SetupPage(ctx, view)->GetID("Outside gain");
        drag(outside);
        IM_CHECK(vector("wavesound.cone") != cone);
        ResizeSetup(ctx, view, 690, false);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->MouseUp(0);
        ctx->Yield(3);
        IM_CHECK(vector("wavesound.cone") == cone);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ResizeSetup(ctx, view, 1500);
        ctx->ItemInputValue(outside, .35f);
        ctx->Yield(3);
        const auto changedCone = vector("wavesound.cone");
        IM_CHECK(std::fabs(changedCone[2] - .35) < .001 && changedCone[0] == cone[0] &&
                 changedCone[1] == cone[1]);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK(vector("wavesound.cone") == cone);
        ChooseInCombo(ctx, SetupRef(view).c_str(), "##attached", attachedName.c_str());
        const auto attachedTo = [&] {
            return Read<ObjectRef>(ctx, [&] {
                SetupDetails data;
                ReadSetupProperties(EditorCommands(), target, {"wavesound.attached"}, data);
                return data.Values().Object("wavesound.attached");
            });
        };
        IM_CHECK(attachedTo() == attached);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        ctx->SetRef(SetupRef(view).c_str());
        Undo(ctx);
        IM_CHECK(!attachedTo());
        ShowSetupTable(ctx, view, "##sound-settings");
        const auto paused = [&] {
            return Read<bool>(ctx, [&] {
                SetupDetails data;
                ReadSetupDetails(EditorCommands(), target, "sound", data);
                return data.Values().Boolean("paused");
            });
        };
        ctx->ItemClick("**/Play");
        ctx->ItemClick("**/Pause");
        ctx->Yield(3);
        IM_CHECK(paused());
        ResizeSetup(ctx, view, 690);
        ShowSetupTable(ctx, view, "##sound-settings");
        ctx->ItemClick("**/Resume");
        ctx->Yield(3);
        IM_CHECK(!paused());
        ctx->ItemClick("**/Stop");
        ctx->Yield(3);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(
                EditorCommands().Command("object.open-setup", {ObjectArgument("target", empty)}).status <=
                EDITOR_NO_CHANGE);
        });
        ResizeSetup(ctx, view, 1500);
        ShowSetupTable(ctx, view, "##sound-settings");
        auto *page = SetupPage(ctx, view);
        for (const auto name : {"Play", "Pause", "Stop"})
            IM_CHECK(ctx->ItemInfo(page->GetID(name)).ItemFlags & ImGuiItemFlags_Disabled);
        IM_CHECK(!(ctx->ItemInfo(page->GetID("Pan")).ItemFlags & ImGuiItemFlags_Disabled));
        IM_CHECK(ctx->ItemInfo(position).ItemFlags & ImGuiItemFlags_Disabled);
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *place = IM_REGISTER_TEST(engine, "appearance", "place_links_reflow_cancel_navigation_and_undo");
    place->GuiFunc = MainThreadGui;
    place->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        ObjectRef target, first, second, portal;
        std::string firstName, secondName, portalName;
        EditorView view = 0;
        EditorConfiguration original;
        OnMainThread(ctx, [&] {
            auto client = EditorCommands();
            for (const auto &entry : {std::make_pair("Reflow source place", &target),
                                      std::make_pair("Reflow linked first", &first),
                                      std::make_pair("Reflow linked second", &second)}) {
                const auto made = client.Command("object.create-place", {TextArgument("name", entry.first)});
                IM_CHECK_NO_RET(EditorClient::Find(made, "created"));
                *entry.second = EditorClient::Find(made, "created")->object;
            }
            const auto made = client.Command("object.create-portal");
            IM_CHECK_NO_RET(EditorClient::Find(made, "created"));
            portal = EditorClient::Find(made, "created")->object;
            SetupPageData page;
            for (const auto &entry : {std::make_pair(first, &firstName), std::make_pair(second, &secondName),
                                      std::make_pair(portal, &portalName)}) {
                IM_CHECK_NO_RET(ReadSetupPage(client, entry.first, -1, page));
                *entry.second = page.name;
            }
            IM_CHECK_NO_RET(client.Command("object.open-setup", {ObjectArgument("target", target)}).status <=
                            EDITOR_NO_CHANGE);
            auto &workspace = App().WorkspaceForTests();
            view = workspace.Role("setup3d");
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(view && workspace.Configure(config, error));
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        ImGuiID tableId = 0;
        for (const float width : {1500.f, 690.f, 450.f, 1500.f}) {
            ResizeSetup(ctx, view, width);
            ShowSetupTable(ctx, view, "placelinks");
            auto *table = SetupTable(ctx, view, "placelinks");
            auto *page = SetupPage(ctx, view);
            IM_CHECK(table && table->InnerWindow);
            IM_CHECK(!tableId || tableId == table->ID);
            tableId = table->ID;
            IM_CHECK(table->OuterRect.Min.x >= page->WorkRect.Min.x - 1 &&
                     table->OuterRect.Max.x <= page->WorkRect.Max.x + 1);
            if (width < 1000)
                IM_CHECK_GT(table->InnerWindow->ScrollMax.x, 0);
            if (width == 450)
                IM_CHECK_GT(page->ScrollMax.x, 0);
        }
        const auto links = [&] {
            return Read<std::vector<std::pair<ObjectRef, ObjectRef>>>(ctx, [&] {
                SetupDetails data;
                ReadSetupDetails(EditorCommands(), target, "place", data);
                std::vector<std::pair<ObjectRef, ObjectRef>> result;
                for (const auto &row : data.Values().Rows("links")) {
                    const SetupFields value(row.fields);
                    result.emplace_back(value.Object("place"), value.Object("portal"));
                }
                return result;
            });
        };
        IM_CHECK(links().empty());
        const auto menu = [&](bool row) {
            ctx->SetRef(SetupRef(view).c_str());
            ShowSetupTable(ctx, view, "placelinks");
            auto *table = SetupTable(ctx, view, "placelinks");
            ctx->ScrollToX(table->InnerWindow->ID, 0);
            ctx->Yield(3);
            if (row) {
                const int index = 0;
                const auto seed = ImHashData(&index, sizeof(index), tableId);
                ctx->ItemClick(ImHashStr("##place", 0, seed), ImGuiMouseButton_Right,
                               ImGuiTestOpFlags_MoveToEdgeL);
            } else {
                ImRect visible = table->OuterRect;
                visible.ClipWith(table->InnerWindow->ClipRect);
                ctx->MouseMoveToPos({visible.Min.x + 15, visible.Max.y - 20});
                ctx->MouseClick(ImGuiMouseButton_Right);
            }
            ctx->Yield(3);
            ctx->SetRef("//$FOCUSED");
        };
        menu(false);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
        IM_CHECK(links().empty());
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        menu(false);
        ctx->ItemClick("**/Attach New Place");
        ctx->Yield(3);
        const auto initial = links();
        IM_CHECK_EQ(initial.size(), 1);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        ctx->SetRef(SetupRef(view).c_str());
        Undo(ctx);
        IM_CHECK(links().empty());
        Redo(ctx);
        IM_CHECK(links() == initial);
        const ObjectRef other = initial[0].first == first ? second : first;
        const std::string otherName = other == first ? firstName : secondName;
        const int row = 0;
        const auto seed = ImHashData(&row, sizeof(row), tableId);
        const auto placeCombo = ImHashStr("##place", 0, seed), portalCombo = ImHashStr("##portal", 0, seed);
        ShowSetupTable(ctx, view, "placelinks");
        ctx->ItemClick(placeCombo);
        ctx->Yield(3);
        const auto popup = ctx->UiContext->OpenPopupStack.back().PopupId;
        ResizeSetup(ctx, view, 690, false);
        IM_CHECK(!ctx->UiContext->OpenPopupStack.empty() &&
                 ctx->UiContext->OpenPopupStack.back().PopupId == popup);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
        IM_CHECK(links() == initial);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        ResizeSetup(ctx, view, 1500);
        ShowSetupTable(ctx, view, "placelinks");
        ctx->SetRef(SetupRef(view).c_str());
        ctx->ItemClick(placeCombo);
        ctx->Yield(3);
        ctx->SetRef("//$FOCUSED");
        ctx->ItemClick(("**/" + otherName).c_str());
        ctx->Yield(3);
        IM_CHECK(links()[0].first == other);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 2);
        ctx->SetRef(SetupRef(view).c_str());
        Undo(ctx);
        IM_CHECK(links() == initial);
        auto *table = SetupTable(ctx, view, "placelinks");
        ctx->ScrollToX(table->InnerWindow->ID, table->InnerWindow->ScrollMax.x);
        ctx->Yield(3);
        ctx->ItemClick(portalCombo);
        ctx->Yield(3);
        ctx->SetRef("//$FOCUSED");
        ctx->ItemClick(("**/" + portalName).c_str());
        ctx->Yield(3);
        const auto withPortal = links();
        IM_CHECK(withPortal[0].second == portal);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 2);
        ctx->SetRef(SetupRef(view).c_str());
        const auto active = [&] {
            return Read<bool>(ctx, [&] {
                SetupDetails data;
                ReadSetupDetails(EditorCommands(), target, "place", data);
                return SetupFields(data.Values().Rows("links")[0].fields).Boolean("active");
            });
        };
        const bool wasActive = active();
        ctx->ItemClick(ImHashStr("##active", 0, seed));
        ctx->Yield(3);
        IM_CHECK(active() != wasActive);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 3);
        Undo(ctx);
        IM_CHECK_EQ(active(), wasActive);
        menu(true);
        ctx->ItemClick("**/Remove Place");
        ctx->Yield(3);
        IM_CHECK(links().empty());
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 3);
        ctx->SetRef(SetupRef(view).c_str());
        Undo(ctx);
        IM_CHECK(links() == withPortal);
        menu(true);
        ctx->ItemClick("**/Portal Setup");
        ctx->Yield(3);
        IM_CHECK(ShowsObject(ctx, portal.object));
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(
                EditorCommands().Command("object.open-setup", {ObjectArgument("target", target)}).status <=
                EDITOR_NO_CHANGE);
        });
        ResizeSetup(ctx, view, 1500);
        menu(true);
        ctx->ItemClick("**/Place Setup");
        ctx->Yield(3);
        IM_CHECK(ShowsObject(ctx, initial[0].first.object));
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 2);
        ctx->SetRef(SetupRef(view).c_str());
        Undo(ctx);
        IM_CHECK(links() == initial);
        Undo(ctx);
        IM_CHECK(links().empty());
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *animation = IM_REGISTER_TEST(engine, "appearancespecialized", "animation_reflow_modes_merge_cancel_and_undo");
    animation->GuiFunc = MainThreadGui;
    animation->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto objects = MakeSetupAnimations(ctx);
        IM_CHECK(objects.first && objects.merged);
        EditorView view = 0;
        EditorConfiguration original;
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(EditorCommands()
                                .Command("object.open-setup", {ObjectArgument("target", objects.first)})
                                .status <= EDITOR_NO_CHANGE);
            auto &workspace = App().WorkspaceForTests();
            view = workspace.Role("setupanimation");
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(view && workspace.Configure(config, error));
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        ImGuiID length = 0, listId = 0;
        for (const float width : {1500.f, 690.f, 450.f, 1500.f}) {
            ResizeSetup(ctx, view, width);
            ShowSetupTable(ctx, view, "##animation-settings");
            auto *page = SetupPage(ctx, view);
            auto *table = SetupTable(ctx, view, "##animation-settings");
            IM_CHECK(table);
            IM_CHECK_EQ(table->ColumnsCount, width > 1000 ? 3 : 1);
            IM_CHECK(!length || length == page->GetID("Length (frames)"));
            length = page->GetID("Length (frames)");
            for (const char *label : {"Length (frames)", "Frames per second", "Can be interrupted",
                                      "Use velocity", "Loop N times"}) {
                const auto rect = ctx->ItemInfo(page->GetID(label)).RectFull;
                bool inside = false;
                for (int i = 0; i < table->ColumnsCount; ++i)
                    inside |= rect.Min.x >= table->Columns[i].WorkMinX - 1 &&
                              rect.Max.x <= table->Columns[i].WorkMaxX + 1;
                IM_CHECK(inside);
            }
            ShowSetupTable(ctx, view, "objectanimations");
            table = SetupTable(ctx, view, "objectanimations");
            IM_CHECK(table && table->InnerWindow);
            IM_CHECK(!listId || listId == table->ID);
            listId = table->ID;
            IM_CHECK_GT(table->InnerWindow->ScrollMax.x, 0);
            IM_CHECK(table->Flags & ImGuiTableFlags_Resizable);
            if (width == 450)
                IM_CHECK_GT(page->ScrollMax.x, 0);
        }
        const auto real = [&](ObjectRef target, const char *property) {
            return Read<double>(ctx, [&] {
                SetupDetails data;
                ReadSetupProperties(EditorCommands(), target, {property}, data);
                return data.Values().Real(property);
            });
        };
        const auto integer = [&](const char *property) {
            return Read<int>(ctx, [&] {
                SetupDetails data;
                ReadSetupProperties(EditorCommands(), objects.first, {property}, data);
                return int(data.Values().Integer(property));
            });
        };
        const auto drag = [&](ImGuiID input) {
            ctx->SetRef(SetupRef(view).c_str());
            ctx->ScrollToItemY(input);
            ctx->Yield(3);
            const auto at = ctx->ItemInfo(input).RectFull.GetCenter();
            ctx->MouseMoveToPos(at);
            ctx->MouseDown(0);
            ctx->MouseMoveToPos({at.x + 30, at.y});
            ctx->Yield(3);
        };
        const double before = real(objects.first, "animation.length");
        drag(length);
        IM_CHECK(real(objects.first, "animation.length") != before);
        ResizeSetup(ctx, view, 690, false);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->MouseUp(0);
        ctx->Yield(3);
        IM_CHECK_EQ(real(objects.first, "animation.length"), before);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ResizeSetup(ctx, view, 1500);
        ctx->ItemInputValue(length, float(before + 15));
        ctx->Yield(3);
        IM_CHECK_EQ(real(objects.first, "animation.length"), before + 15);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK_EQ(real(objects.first, "animation.length"), before);
        ctx->ItemClick(SetupPage(ctx, view)->GetID("Linked to frame rate"));
        ctx->Yield(3);
        IM_CHECK_EQ(integer("animation.linked"), 0);
        IM_CHECK(ctx->ItemInfo(SetupPage(ctx, view)->GetID("Frames per second")).ItemFlags &
                 ImGuiItemFlags_Disabled);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        const int velocity = integer("animation.transition-use-velocity"),
                  warp = integer("animation.secondary-warp");
        ctx->ItemClick(SetupPage(ctx, view)->GetID("Use velocity"));
        ctx->Yield(3);
        IM_CHECK_EQ(integer("animation.transition-use-velocity"), !velocity);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        ctx->ItemClick(SetupPage(ctx, view)->GetID("Loop N times"));
        ctx->Yield(3);
        IM_CHECK_EQ(integer("animation.secondary-play"), CKSECONDARYANIMATION_LOOPNTIMES);
        IM_CHECK_EQ(integer("animation.secondary-warp"), warp);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        ShowSetupTable(ctx, view, "objectanimations");
        auto *table = SetupTable(ctx, view, "objectanimations");
        ctx->ScrollToX(table->InnerWindow->ID, 0);
        ctx->Yield(3);
        const int row = 0;
        const auto seed = ImHashData(&row, sizeof(row), listId);
        ctx->ItemDoubleClick(ImHashStr(objects.partName.c_str(), 0, seed), ImGuiTestOpFlags_MoveToEdgeL);
        ctx->Yield(3);
        IM_CHECK(ShowsObject(ctx, objects.part.object));
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(EditorCommands()
                                .Command("object.open-setup", {ObjectArgument("target", objects.merged)})
                                .status <= EDITOR_NO_CHANGE);
        });
        ResizeSetup(ctx, view, 1500);
        ShowSetupTable(ctx, view, "##animation-settings");
        const auto merge = SetupPage(ctx, view)->GetID("Merge Factor");
        const double factor = real(objects.merged, "animation.merge-factor");
        drag(merge);
        IM_CHECK(std::fabs(real(objects.merged, "animation.merge-factor") - factor) > .01);
        ResizeSetup(ctx, view, 690, false);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->MouseUp(0);
        ctx->Yield(3);
        IM_CHECK_EQ(real(objects.merged, "animation.merge-factor"), factor);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ResizeSetup(ctx, view, 1500);
        ctx->ItemInputValue(merge, .7f);
        ctx->Yield(3);
        IM_CHECK(std::fabs(real(objects.merged, "animation.merge-factor") - .7) < .001);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK_EQ(real(objects.merged, "animation.merge-factor"), factor);
        ctx->ScrollToTop(SetupPage(ctx, view)->ID);
        ctx->Yield(3);
        ctx->ItemClick(ImHashStr(">", 0, SetupPage(ctx, view)->GetID("Character")));
        ctx->Yield(3);
        IM_CHECK(ShowsObject(ctx, objects.character.object));
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *objectAnimation =
        IM_REGISTER_TEST(engine, "appearancespecialized", "object_animation_reflow_shared_merge_cancel_and_undo");
    objectAnimation->GuiFunc = MainThreadGui;
    objectAnimation->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto objects = MakeSetupAnimations(ctx);
        IM_CHECK(objects.part && objects.shared && objects.mergedPart);
        EditorView view = 0;
        EditorConfiguration original;
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(EditorCommands()
                                .Command("object.open-setup", {ObjectArgument("target", objects.part)})
                                .status <= EDITOR_NO_CHANGE);
            auto &workspace = App().WorkspaceForTests();
            view = workspace.Role("setupobjectanimation");
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(view && workspace.Configure(config, error));
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        ImGuiID length = 0;
        for (const float width : {1500.f, 690.f, 450.f, 1500.f}) {
            ResizeSetup(ctx, view, width);
            ShowSetupTable(ctx, view, "##objectanimation-settings");
            auto *page = SetupPage(ctx, view);
            auto *table = SetupTable(ctx, view, "##objectanimation-settings");
            IM_CHECK(table);
            IM_CHECK_EQ(table->ColumnsCount, width > 1000 ? 3 : 1);
            IM_CHECK(!length || length == page->GetID("Length (frames)"));
            length = page->GetID("Length (frames)");
            for (const auto id :
                 {length, ImHashStr(">", 0, page->GetID("Entity")), page->GetID("Off-Axis Scale")}) {
                const auto rect = ctx->ItemInfo(id).RectFull;
                bool inside = false;
                for (int i = 0; i < table->ColumnsCount; ++i)
                    inside |= rect.Min.x >= table->Columns[i].WorkMinX - 1 &&
                              rect.Max.x <= table->Columns[i].WorkMaxX + 1;
                IM_CHECK(inside);
            }
            IM_CHECK(SetupTable(ctx, view, "##objectanimation-keys"));
            if (width == 450)
                IM_CHECK_GT(page->ScrollMax.x, 0);
        }
        const auto real = [&](ObjectRef target, const char *property) {
            return Read<double>(ctx, [&] {
                SetupDetails data;
                ReadSetupProperties(EditorCommands(), target, {property}, data);
                return data.Values().Real(property);
            });
        };
        const auto drag = [&](ImGuiID input) {
            ctx->SetRef(SetupRef(view).c_str());
            ctx->ScrollToItemY(input);
            ctx->Yield(3);
            const auto at = ctx->ItemInfo(input).RectFull.GetCenter();
            ctx->MouseMoveToPos(at);
            ctx->MouseDown(0);
            ctx->MouseMoveToPos({at.x + 30, at.y});
            ctx->Yield(3);
        };
        const double before = real(objects.part, "objectanimation.length");
        drag(length);
        IM_CHECK(real(objects.part, "objectanimation.length") != before);
        ResizeSetup(ctx, view, 690, false);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->MouseUp(0);
        ctx->Yield(3);
        IM_CHECK_EQ(real(objects.part, "objectanimation.length"), before);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ResizeSetup(ctx, view, 1500);
        ctx->ItemInputValue(length, float(before + 10));
        ctx->Yield(3);
        IM_CHECK_EQ(real(objects.part, "objectanimation.length"), before + 10);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK_EQ(real(objects.part, "objectanimation.length"), before);
        for (const auto &flag : {std::make_pair("Rotation", "objectanimation.ignore-rotation"),
                                 std::make_pair("Morph", "objectanimation.ignore-morph")}) {
            const auto get = [&] {
                return Read<int>(ctx, [&] {
                    SetupDetails data;
                    ReadSetupProperties(EditorCommands(), objects.part, {flag.second}, data);
                    return int(data.Values().Integer(flag.second));
                });
            };
            const int was = get();
            ctx->ItemClick(SetupPage(ctx, view)->GetID(flag.first));
            ctx->Yield(3);
            IM_CHECK_EQ(get(), !was);
            IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
            Undo(ctx);
            IM_CHECK_EQ(get(), was);
        }
        ctx->ScrollToBottom(SetupPage(ctx, view)->ID);
        ctx->Yield(3);
        ctx->ItemDoubleClick(("**/" + objects.userName).c_str());
        ctx->Yield(3);
        IM_CHECK(ShowsObject(ctx, objects.first.object));
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(EditorCommands()
                                .Command("object.open-setup", {ObjectArgument("target", objects.shared)})
                                .status <= EDITOR_NO_CHANGE);
        });
        ResizeSetup(ctx, view, 1500);
        ShowSetupTable(ctx, view, "##objectanimation-settings");
        ctx->ItemClick(ImHashStr(">", 0, SetupPage(ctx, view)->GetID("Shares data of")));
        ctx->Yield(3);
        IM_CHECK(ShowsObject(ctx, objects.part.object));
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(EditorCommands()
                                .Command("object.open-setup", {ObjectArgument("target", objects.mergedPart)})
                                .status <= EDITOR_NO_CHANGE);
        });
        ResizeSetup(ctx, view, 1500);
        ShowSetupTable(ctx, view, "##objectanimation-settings");
        const auto merge = SetupPage(ctx, view)->GetID("Merge Factor");
        const double factor = real(objects.mergedPart, "objectanimation.merge-factor");
        drag(merge);
        IM_CHECK(std::fabs(real(objects.mergedPart, "objectanimation.merge-factor") - factor) > .01);
        ResizeSetup(ctx, view, 690, false);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->MouseUp(0);
        ctx->Yield(3);
        IM_CHECK_EQ(real(objects.mergedPart, "objectanimation.merge-factor"), factor);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ResizeSetup(ctx, view, 1500);
        ctx->ItemInputValue(merge, .65f);
        ctx->Yield(3);
        IM_CHECK(std::fabs(real(objects.mergedPart, "objectanimation.merge-factor") - .65) < .001);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK_EQ(real(objects.mergedPart, "objectanimation.merge-factor"), factor);
        ctx->ScrollToTop(SetupPage(ctx, view)->ID);
        ctx->Yield(3);
        ctx->ItemClick(ImHashStr(">", 0, SetupPage(ctx, view)->GetID("Entity")));
        ctx->Yield(3);
        IM_CHECK(ShowsObject(ctx, objects.entity.object));
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *midi = IM_REGISTER_TEST(engine, "appearancespecialized", "midi_reflow_file_edit_playback_and_undo");
    midi->GuiFunc = MainThreadGui;
    midi->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        struct Files {
            std::filesystem::path folder, first, second;
            ~Files() {
                std::error_code e;
                std::filesystem::remove(first, e);
                std::filesystem::remove(second, e);
                std::filesystem::remove(folder, e);
            }
        } files;
        files.folder =
            std::filesystem::temp_directory_path() /
            ("nemodev-midi-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        IM_CHECK(std::filesystem::create_directory(files.folder));
        files.first = files.folder / (std::string(100, 'M') + ".mid");
        files.second = files.folder / "replacement.mid";
        const unsigned char bytes[] = {'M', 'T', 'h', 'd', 0,   0, 0, 6, 0, 0, 0,    1,    0,
                                       96,  'M', 'T', 'r', 'k', 0, 0, 0, 4, 0, 0xff, 0x2f, 0};
        for (const auto &path : {files.first, files.second}) {
            std::ofstream stream(path, std::ios::binary);
            stream.write(reinterpret_cast<const char *>(bytes), sizeof(bytes));
            IM_CHECK(bool(stream));
        }
        const auto firstPath = files.first.string(), secondPath = files.second.string();
        ObjectRef offline, live, empty;
        CKBaseManager *originalManager = nullptr;
        SetupMidiManager *manager = nullptr;
        EditorView view = 0;
        EditorConfiguration original;
        OnMainThread(ctx, [&] {
            auto &doc = *App().State().document;
            auto *context = doc.Context();
            originalManager = context->GetManagerByGuid(MIDI_MANAGER_GUID);
            if (originalManager)
                context->ActivateManager(originalManager, FALSE);
            auto *sound = CKMidiSound::Cast(context->CreateObject(CKCID_MIDISOUND, "Setup offline MIDI"));
            IM_CHECK_NO_RET(sound);
            sound->SetSoundFileName(firstPath.c_str());
            doc.Include(sound->GetID());
            offline = doc.Reference(sound->GetID());
            IM_CHECK_NO_RET(
                EditorCommands().Command("object.open-setup", {ObjectArgument("target", offline)}).status <=
                EDITOR_NO_CHANGE);
            auto &workspace = App().WorkspaceForTests();
            view = workspace.Role("setupmidi");
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(view && workspace.Configure(config, error));
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        ImGuiID save = 0;
        for (const float width : {1500.f, 690.f, 450.f, 1500.f}) {
            ResizeSetup(ctx, view, width);
            ShowSetupTable(ctx, view, "##midi-settings");
            auto *page = SetupPage(ctx, view);
            auto *table = SetupTable(ctx, view, "##midi-settings");
            IM_CHECK(table);
            IM_CHECK_EQ(table->ColumnsCount, width > 1000 ? 2 : 1);
            IM_CHECK(!save || save == page->GetID("Save Options"));
            save = page->GetID("Save Options");
            for (const auto id : {save, page->GetID("Change File...")}) {
                const auto rect = ctx->ItemInfo(id).RectFull;
                IM_CHECK(rect.Min.x >= table->Columns[0].WorkMinX - 1 &&
                         rect.Max.x <= table->Columns[0].WorkMaxX + 1);
            }
            for (const char *label : {"Play", "Stop"})
                IM_CHECK(ctx->ItemInfo(page->GetID(label)).ItemFlags & ImGuiItemFlags_Disabled);
            if (width == 450)
                IM_CHECK_GT(page->ScrollMax.x, 0);
        }
        const auto saveOption = [&] {
            return Read<int>(ctx, [&] {
                SetupDetails data;
                ReadSetupProperties(EditorCommands(), offline, {"sound.save-options"}, data);
                return int(data.Values().Integer("sound.save-options"));
            });
        };
        ctx->SetRef(SetupRef(view).c_str());
        const int beforeSave = saveOption();
        ctx->ItemClick(save);
        ctx->Yield(3);
        const auto popup = ctx->UiContext->OpenPopupStack.back().PopupId;
        ResizeSetup(ctx, view, 690, false);
        IM_CHECK(!ctx->UiContext->OpenPopupStack.empty() &&
                 ctx->UiContext->OpenPopupStack.back().PopupId == popup);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
        IM_CHECK_EQ(saveOption(), beforeSave);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ResizeSetup(ctx, view, 1500);
        ChooseInCombo(ctx, SetupRef(view).c_str(), "Save Options",
                      beforeSave == 2 ? "External" : "Use Global");
        IM_CHECK_EQ(saveOption(), beforeSave == 2 ? 0 : 2);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        ctx->SetRef(SetupRef(view).c_str());
        Undo(ctx);
        IM_CHECK_EQ(saveOption(), beforeSave);
        const auto filename = [&](ObjectRef target) {
            return Read<std::string>(ctx, [&] {
                SetupDetails data;
                ReadSetupProperties(EditorCommands(), target, {"sound.file"}, data);
                return data.Values().Text("sound.file");
            });
        };
        const auto beforeFile = filename(offline);
        OnMainThread(ctx, [&] {
            auto &app = App().WorkspaceForTests().Application();
            auto client = EditorCommands();
            const auto target = ObjectArgument("target", offline);
            EditorEdit edit = 0;
            IM_CHECK_NO_RET(app.BeginEdit("setup.sound.file", {client.Scope(), &target, 1}, edit) ==
                            EDITOR_OK);
            const auto value = TextArgument("value", secondPath);
            IM_CHECK_NO_RET(app.UpdateEdit(edit, value) == EDITOR_OK);
            IM_CHECK_NO_RET(app.FinishEdit(edit, false) == EDITOR_CANCELLED);
            auto &doc = *App().State().document;
            auto *context = doc.Context();
            manager = new SetupMidiManager(context);
            IM_CHECK_NO_RET(context->RegisterNewManager(manager) == CK_OK);
            auto *sound = CKMidiSound::Cast(context->CreateObject(CKCID_MIDISOUND, "Setup MIDI playback"));
            auto *noFile =
                CKMidiSound::Cast(context->CreateObject(CKCID_MIDISOUND, "Setup MIDI without file"));
            IM_CHECK_NO_RET(sound && noFile);
            IM_CHECK_NO_RET(sound->SetSoundFileName(firstPath.c_str()) == CK_OK);
            doc.Include(sound->GetID());
            doc.Include(noFile->GetID());
            live = doc.Reference(sound->GetID());
            empty = doc.Reference(noFile->GetID());
            IM_CHECK_NO_RET(client.Command("object.open-setup", {ObjectArgument("target", live)}).status <=
                            EDITOR_NO_CHANGE);
        });
        ctx->Yield(4);
        IM_CHECK_EQ(filename(offline), beforeFile);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        OnMainThread(ctx, [&] {
            auto &app = App().WorkspaceForTests().Application();
            const auto client = EditorCommands();
            const auto target = ObjectArgument("target", live);
            EditorEdit edit = 0;
            IM_CHECK_NO_RET(app.BeginEdit("setup.sound.file", {client.Scope(), &target, 1}, edit) ==
                            EDITOR_OK);
            IM_CHECK_NO_RET(app.UpdateEdit(edit, TextArgument("value", secondPath)) == EDITOR_OK);
            IM_CHECK_NO_RET(app.FinishEdit(edit, true) == EDITOR_QUEUED);
        });
        ctx->Yield(3);
        IM_CHECK_EQ(filename(live), secondPath);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        ctx->SetRef(SetupRef(view).c_str());
        Undo(ctx);
        IM_CHECK_EQ(filename(live), firstPath);
        ResizeSetup(ctx, view, 1500);
        ShowSetupTable(ctx, view, "##midi-settings");
        const auto state = [&](const char *key) {
            return Read<int>(ctx, [&] {
                SetupDetails data;
                ReadSetupDetails(EditorCommands(), live, "midi", data);
                return int(data.Values().Integer(key));
            });
        };
        ctx->ItemClick("**/Play");
        ctx->Yield(3);
        IM_CHECK_EQ(state("playing"), 1);
        IM_CHECK_EQ(state("position"), 321);
        ResizeSetup(ctx, view, 690);
        ShowSetupTable(ctx, view, "##midi-settings");
        ctx->ItemClick("**/Pause");
        ctx->Yield(3);
        IM_CHECK_EQ(state("paused"), 1);
        ctx->ItemClick("**/Play");
        ctx->Yield(3);
        IM_CHECK_EQ(state("paused"), 0);
        ctx->ItemClick("**/Stop");
        ctx->Yield(3);
        IM_CHECK_EQ(state("playing"), 0);
        IM_CHECK_EQ(state("position"), 0);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(
                EditorCommands().Command("object.open-setup", {ObjectArgument("target", empty)}).status <=
                EDITOR_NO_CHANGE);
        });
        ResizeSetup(ctx, view, 1500);
        ShowSetupTable(ctx, view, "##midi-settings");
        for (const char *label : {"Play", "Stop"})
            IM_CHECK(ctx->ItemInfo(SetupPage(ctx, view)->GetID(label)).ItemFlags & ImGuiItemFlags_Disabled);
        OnMainThread(ctx, [&] {
            auto *context = App().State().document->Context();
            context->ActivateManager(manager, FALSE);
            if (originalManager)
                context->ActivateManager(originalManager, TRUE);
            IM_CHECK_NO_RET(context->GetManagerByGuid(MIDI_MANAGER_GUID) == originalManager);
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *spriteText = IM_REGISTER_TEST(engine, "appearancespecialized", "sprite_text_reflow_text_font_color_cancel_and_undo");
    spriteText->GuiFunc = MainThreadGui;
    spriteText->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        ObjectRef target;
        EditorView view = 0;
        EditorConfiguration original;
        OnMainThread(ctx, [&] {
            auto &doc = *App().State().document;
            auto *text = CKSpriteText::Cast(doc.Context()->CreateObject(CKCID_SPRITETEXT, "Reflow sprite text"));
            IM_CHECK_NO_RET(text && text->Create(128, 64, 32, 0));
            text->SetText("Original text\nSecond line");
            text->SetFont("Arial", 14, 400, FALSE, FALSE);
            text->SetTextColor(0xff204060);
            text->SetBackgroundColor(0x80402010);
            text->SetAlign(static_cast<CKSPRITETEXT_ALIGNMENT>(CKSPRITETEXT_LEFT | CKSPRITETEXT_TOP));
            doc.Context()->GetCurrentLevel()->AddObject(text);
            doc.Include(text->GetID());
            target = doc.Reference(text->GetID());
            IM_CHECK_NO_RET(EditorCommands().Command("object.open-setup", {ObjectArgument("target", target)}).status <=
                            EDITOR_NO_CHANGE);
            auto &workspace = App().WorkspaceForTests();
            view = workspace.Role("setupspritetext");
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(view && workspace.Configure(config, error));
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        const auto values = [&] {
            return Read<SetupDetails>(ctx, [&] {
                SetupDetails data;
                ReadSetupProperties(EditorCommands(), target,
                                    {"spritetext.text", "spritetext.font", "spritetext.color",
                                     "spritetext.background-color", "spritetext.alignment"},
                                    data);
                return data;
            });
        };
        const auto text = [&] { return values().Values().Text("spritetext.text"); };
        const auto font = [&](const char *key) {
            return int(values().Values().Record("spritetext.font").Integer(key));
        };
        const auto alignment = [&] { return int(values().Values().Integer("spritetext.alignment")); };
        ImGuiID input = 0, size = 0;
        for (const float width : {1500.f, 690.f, 450.f, 1500.f}) {
            ResizeSetup(ctx, view, width);
            ShowSetupTable(ctx, view, "##spritetext-settings");
            auto *page = SetupPage(ctx, view);
            auto *table = SetupTable(ctx, view, "##spritetext-settings");
            IM_CHECK(table && SetupTable(ctx, view, "##entity2d-settings"));
            IM_CHECK_EQ(table->ColumnsCount, width > 1000 ? 3 : 1);
            IM_CHECK(!input || input == page->GetID("##text"));
            IM_CHECK(!size || size == page->GetID("Size"));
            input = page->GetID("##text");
            size = page->GetID("Size");
            for (const char *label : {"##text", "Face", "Size", "Underline", "Right", "Bottom"}) {
                const auto rect = ctx->ItemInfo(page->GetID(label)).RectFull;
                bool inside = false;
                for (int c = 0; c < table->ColumnsCount; ++c)
                    inside |=
                        rect.Min.x >= table->Columns[c].WorkMinX - 1 && rect.Max.x <= table->Columns[c].WorkMaxX + 1;
                IM_CHECK(inside);
            }
            if (width == 450)
                IM_CHECK_GT(page->ScrollMax.x, 0);
        }
        ctx->SetRef(SetupRef(view).c_str());
        const auto before = text();
        ctx->ItemClick(input);
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
        ctx->KeyChars("Cancelled sprite text");
        ctx->Yield(2);
        IM_CHECK(text() != before);
        ResizeSetup(ctx, view, 690, false);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
        IM_CHECK_EQ(text(), before);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ResizeSetup(ctx, view, 1500);
        ctx->ItemClick(input);
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
        ctx->KeyChars("Confirmed sprite text");
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Enter);
        ctx->Yield(3);
        IM_CHECK_EQ(text(), "Confirmed sprite text");
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK_EQ(text(), before);
        const int beforeSize = font("size");
        const auto at = ctx->ItemInfo(size).RectFull.GetCenter();
        ctx->MouseMoveToPos(at);
        ctx->MouseDown(0);
        ctx->MouseMoveToPos({at.x + 35, at.y});
        ctx->Yield(3);
        IM_CHECK(font("size") != beforeSize);
        ResizeSetup(ctx, view, 690, false);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->MouseUp(0);
        ctx->Yield(3);
        IM_CHECK_EQ(font("size"), beforeSize);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ResizeSetup(ctx, view, 1500);
        ctx->ItemInputValue(size, beforeSize + 5);
        ctx->Yield(3);
        IM_CHECK_EQ(font("size"), beforeSize + 5);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK_EQ(font("size"), beforeSize);
        for (const auto &field : {std::make_pair("Bold", "weight"), std::make_pair("Italic", "italic"),
                                  std::make_pair("Underline", "underline")}) {
            const int was = font(field.second);
            ctx->ItemClick(SetupPage(ctx, view)->GetID(field.first));
            ctx->Yield(3);
            IM_CHECK(font(field.second) != was);
            IM_CHECK_EQ(font("size"), beforeSize);
            IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
            Undo(ctx);
            IM_CHECK_EQ(font(field.second), was);
        }
        for (const auto &choice : {std::make_pair("Right", CKSPRITETEXT_RIGHT | CKSPRITETEXT_TOP),
                                   std::make_pair("Bottom", CKSPRITETEXT_LEFT | CKSPRITETEXT_BOTTOM)}) {
            ctx->ItemClick(SetupPage(ctx, view)->GetID(choice.first));
            ctx->Yield(3);
            IM_CHECK_EQ(alignment(), choice.second);
            IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
            Undo(ctx);
            IM_CHECK_EQ(alignment(), CKSPRITETEXT_LEFT | CKSPRITETEXT_TOP);
        }
        const auto color = [&](const char *property) { return values().Values().Reals(property); };
        const auto textColor = color("spritetext.color"), background = color("spritetext.background-color");
        const auto openColor = [&](const char *label) {
            ctx->SetRef(SetupRef(view).c_str());
            ctx->ItemClick(ImHashStr("##ColorButton", 0, SetupPage(ctx, view)->GetID(label)));
            ctx->Yield(2);
        };
        const auto enterColor = [&] {
            ctx->SetRef("//$FOCUSED");
            ctx->ItemClick("**/##picker/##hex/##Text");
            ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
            ctx->KeyChars("7F3344CC");
            ctx->Yield(2);
        };
        openColor("Text Color");
        const auto popup = ctx->UiContext->OpenPopupStack.back().PopupId;
        ResizeSetup(ctx, view, 690, false);
        IM_CHECK(!ctx->UiContext->OpenPopupStack.empty() && ctx->UiContext->OpenPopupStack.back().PopupId == popup);
        enterColor();
        IM_CHECK(color("spritetext.color") != textColor);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
        IM_CHECK(color("spritetext.color") == textColor && color("spritetext.background-color") == background);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ResizeSetup(ctx, view, 1500);
        openColor("Background Color");
        enterColor();
        IM_CHECK(color("spritetext.background-color") != background);
        ctx->KeyPress(ImGuiKey_Enter);
        auto *window = ctx->GetWindowByRef(SetupRef(view).c_str());
        ctx->MouseMoveToPos({window->Pos.x + window->Size.x * .7f, window->Pos.y + 10});
        ctx->MouseClick(0);
        ctx->Yield(3);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        IM_CHECK(color("spritetext.color") == textColor);
        ctx->SetRef(SetupRef(view).c_str());
        Undo(ctx);
        IM_CHECK(color("spritetext.background-color") == background);
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *layer = IM_REGISTER_TEST(engine, "appearancespecialized", "layer_reflow_readonly_preview_visibility_cancel_and_undo");
    layer->GuiFunc = MainThreadGui;
    layer->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        ObjectRef target, gridRef, large, empty;
        EditorView view = 0;
        EditorConfiguration original;
        OnMainThread(ctx, [&] {
            auto &doc = *App().State().document;
            auto *grid =
                CKGrid::Cast(doc.Context()->CreateObject(CKCID_GRID, ("Layer grid " + std::string(100, 'G')).c_str()));
            IM_CHECK_NO_RET(grid);
            grid->SetDimensions(8, 8, 8, 8);
            doc.Include(grid->GetID());
            const auto type = RegisterGridLayerType(doc, "Reflow layer type");
            IM_CHECK_NO_RET(type.ok);
            const auto made = AddGridLayer(doc, grid->GetID(), int(type.created));
            IM_CHECK_NO_RET(made.ok && made.created);
            auto *nativeLayer = CKLayer::Cast(doc.Context()->GetObject(made.created));
            IM_CHECK_NO_RET(nativeLayer);
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) {
                    int value = y * 16 + x;
                    nativeLayer->SetValue2(x, y, &value);
                }
            auto *orphan = doc.Context()->CreateObject(CKCID_LAYER, "Layer without grid");
            IM_CHECK_NO_RET(orphan);
            doc.Include(orphan->GetID());
            target = doc.Reference(nativeLayer->GetID());
            gridRef = doc.Reference(grid->GetID());
            empty = doc.Reference(orphan->GetID());
            auto *largeGrid = CKGrid::Cast(doc.Context()->CreateObject(CKCID_GRID, "Large layer grid"));
            IM_CHECK_NO_RET(largeGrid);
            largeGrid->SetDimensions(512, 512, 512, 512);
            doc.Include(largeGrid->GetID());
            const auto largeLayer = AddGridLayer(doc, largeGrid->GetID(), int(type.created));
            IM_CHECK_NO_RET(largeLayer.ok && largeLayer.created);
            large = doc.Reference(largeLayer.created);
            IM_CHECK_NO_RET(EditorCommands().Command("object.open-setup", {ObjectArgument("target", target)}).status <=
                            EDITOR_NO_CHANGE);
            auto &workspace = App().WorkspaceForTests();
            view = workspace.Role("setuplayer");
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(view && workspace.Configure(config, error));
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        const auto cells = [&] {
            return Read<std::vector<uint64_t>>(ctx, [&] {
                SetupDetails data;
                ReadSetupDetails(EditorCommands(), target, "layer", data);
                return data.Values().Integers("squares");
            });
        };
        const auto visible = [&] {
            return Read<bool>(ctx, [&] {
                SetupDetails data;
                ReadSetupProperties(EditorCommands(), target, {"layer.visible"}, data);
                return data.Values().Boolean("layer.visible");
            });
        };
        const auto canvas = [&]() -> ImGuiWindow * {
            auto *page = SetupPage(ctx, view);
            for (auto *child : page->DC.ChildWindows)
                if (child->ChildId == page->GetID("##layer-canvas"))
                    return child;
            return nullptr;
        };
        const auto before = cells();
        const bool wasVisible = visible();
        ImGuiID squares = 0, checkbox = 0;
        for (const float width : {1500.f, 690.f, 450.f, 1500.f}) {
            ResizeSetup(ctx, view, width);
            ShowSetupTable(ctx, view, "##layer-settings");
            auto *page = SetupPage(ctx, view);
            auto *table = SetupTable(ctx, view, "##layer-settings");
            auto *child = canvas();
            IM_CHECK(table && child);
            IM_CHECK_EQ(table->ColumnsCount, width > 1000 ? 2 : 1);
            IM_CHECK(!squares || squares == page->GetID("##squares"));
            squares = page->GetID("##squares");
            IM_CHECK(!checkbox || checkbox == page->GetID("Visible"));
            checkbox = page->GetID("Visible");
            const int column = table->ColumnsCount - 1;
            IM_CHECK(child->Pos.x >= table->Columns[column].WorkMinX - 1 &&
                     child->Pos.x + child->Size.x <= table->Columns[column].WorkMaxX + 1);
            if (width == 450)
                IM_CHECK_GT(page->ScrollMax.x, 0);
        }
        IM_CHECK(cells() == before);
        ctx->SetRef(SetupRef(view).c_str());
        const auto rect = ctx->ItemInfo(squares).RectFull;
        ctx->MouseMoveToPos({rect.Min.x + rect.GetWidth() * .3f, rect.Min.y + rect.GetHeight() * .7f});
        ctx->MouseClick(0);
        ctx->Yield(2);
        IM_CHECK(cells() == before);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        EditorEdit edit = 0;
        OnMainThread(ctx, [&] {
            auto &app = App().WorkspaceForTests().Application();
            const auto argument = ObjectArgument("target", target);
            IM_CHECK_NO_RET(app.BeginEdit("setup.layer.visible", {app.Scope(view), &argument, 1}, edit) == EDITOR_OK);
            IM_CHECK_NO_RET(app.UpdateEdit(edit, IntegerArgument("value", !wasVisible)) == EDITOR_OK);
        });
        IM_CHECK_EQ(visible(), !wasVisible);
        ResizeSetup(ctx, view, 690, false);
        IM_CHECK_EQ(visible(), !wasVisible);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(App().WorkspaceForTests().Application().FinishEdit(edit, false) == EDITOR_CANCELLED);
        });
        ctx->Yield(2);
        IM_CHECK_EQ(visible(), wasVisible);
        IM_CHECK(cells() == before);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ResizeSetup(ctx, view, 1500);
        ctx->ItemClick(checkbox);
        ctx->Yield(3);
        IM_CHECK_EQ(visible(), !wasVisible);
        IM_CHECK(cells() == before);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK_EQ(visible(), wasVisible);
        Redo(ctx);
        IM_CHECK_EQ(visible(), !wasVisible);
        Undo(ctx);
        IM_CHECK(cells() == before);
        ctx->ItemClick(SetupPage(ctx, view)->GetID(">"));
        ctx->Yield(3);
        IM_CHECK(ShowsObject(ctx, gridRef.object));
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(EditorCommands().Command("object.open-setup", {ObjectArgument("target", large)}).status <=
                            EDITOR_NO_CHANGE);
        });
        ResizeSetup(ctx, view, 450);
        ctx->ScrollToBottom(SetupPage(ctx, view)->ID);
        ctx->Yield(3);
        auto *child = canvas();
        IM_CHECK(child);
        IM_CHECK_GE(child->Pos.y, SetupPage(ctx, view)->InnerRect.Min.y - 1);
        IM_CHECK_LE(child->Pos.y + child->Size.y, SetupPage(ctx, view)->InnerRect.Max.y + 1);
        IM_CHECK_GT(child->ScrollMax.x, 0);
        IM_CHECK_GT(child->ScrollMax.y, 0);
        IM_CHECK_LT(child->DrawList->VtxBuffer.Size, 4 * 512 * 512);
        ctx->ScrollToX(child->ID, child->ScrollMax.x);
        ctx->ScrollToBottom(child->ID);
        ctx->Yield(2);
        IM_CHECK_GT(child->Scroll.x, 0);
        IM_CHECK_GT(child->Scroll.y, 0);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(EditorCommands().Command("object.open-setup", {ObjectArgument("target", empty)}).status <=
                            EDITOR_NO_CHANGE);
        });
        ResizeSetup(ctx, view, 690);
        ShowSetupTable(ctx, view, "##layer-settings");
        IM_CHECK(ctx->ItemInfo(SetupPage(ctx, view)->GetID(">")).ItemFlags & ImGuiItemFlags_Disabled);
        IM_CHECK(canvas());
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *kinematic = IM_REGISTER_TEST(engine, "appearancespecialized", "kinematic_reflow_effectors_popup_navigation_and_undo");
    kinematic->GuiFunc = MainThreadGui;
    kinematic->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        ObjectRef target, start, middle, end, outside, owner, parameter;
        EditorView view = 0;
        EditorConfiguration original;
        OnMainThread(ctx, [&] {
            auto &doc = *App().State().document;
            auto *context = doc.Context();
            const auto part = [&](const char *name) {
                auto *body = CKBodyPart::Cast(context->CreateObject(CKCID_BODYPART, name));
                if (body)
                    doc.Include(body->GetID());
                return body;
            };
            auto *a = part(("Chain start " + std::string(90, 'S')).c_str()), *b = part("Chain middle"),
                 *c = part("Chain end"), *d = part("Chain outside");
            auto *chain = CKKinematicChain::Cast(context->CreateObject(CKCID_KINEMATICCHAIN, "Reflow kinematic chain"));
            auto *behavior = CKBehavior::Cast(context->CreateObject(CKCID_BEHAVIOR, "Chain parameter owner"));
            IM_CHECK_NO_RET(a && b && c && d && chain && behavior);
            IM_CHECK_NO_RET(b->SetParent(a) && c->SetParent(b));
            VxVector position{0, 0, 0};
            a->SetPosition(&position);
            position.y = 2;
            b->SetPosition(&position);
            position.y = 5;
            c->SetPosition(&position);
            chain->SetStartEffector(a);
            chain->SetEndEffector(c);
            behavior->SetType(CKBEHAVIORTYPE_SCRIPT);
            auto *user = behavior->CreateLocalParameter("Chain parameter", CKPGUID_KINEMATICCHAIN);
            IM_CHECK_NO_RET(user);
            auto chainId = chain->GetID();
            user->SetValue(&chainId, sizeof(chainId));
            for (auto *object :
                 {static_cast<CKObject *>(chain), static_cast<CKObject *>(behavior), static_cast<CKObject *>(user)})
                doc.Include(object->GetID());
            target = doc.Reference(chain->GetID());
            start = doc.Reference(a->GetID());
            middle = doc.Reference(b->GetID());
            end = doc.Reference(c->GetID());
            outside = doc.Reference(d->GetID());
            owner = doc.Reference(behavior->GetID());
            parameter = doc.Reference(user->GetID());
            IM_CHECK_NO_RET(EditorCommands().Command("object.open-setup", {ObjectArgument("target", target)}).status <=
                            EDITOR_NO_CHANGE);
            auto &workspace = App().WorkspaceForTests();
            view = workspace.Role("setupchain");
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(view && workspace.Configure(config, error));
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        const auto details = [&] {
            return Read<SetupDetails>(ctx, [&] {
                SetupDetails data;
                ReadSetupDetails(EditorCommands(), target, "kinematic", data);
                return data;
            });
        };
        IM_CHECK_EQ(details().Values().Integer("body-count"), 3);
        IM_CHECK(std::fabs(details().Values().Real("length") - 5) < .001);
        ImGuiID first = 0, last = 0, chainTable = 0, usersTable = 0;
        for (const float width : {1500.f, 690.f, 450.f, 1500.f}) {
            ResizeSetup(ctx, view, width);
            ShowSetupTable(ctx, view, "##kinematic-settings");
            auto *page = SetupPage(ctx, view);
            auto *table = SetupTable(ctx, view, "##kinematic-settings");
            IM_CHECK(table);
            IM_CHECK_EQ(table->ColumnsCount, width > 1000 ? 2 : 1);
            IM_CHECK(!first || first == page->GetID("##start"));
            first = page->GetID("##start");
            IM_CHECK(!last || last == page->GetID("##end"));
            last = page->GetID("##end");
            for (const auto id : {first, last, page->GetID(">##startsetup"), page->GetID(">##endsetup")}) {
                const auto rect = ctx->ItemInfo(id).RectFull;
                IM_CHECK(rect.Min.x >= table->Columns[0].WorkMinX - 1 && rect.Max.x <= table->Columns[0].WorkMaxX + 1);
            }
            auto *list = SetupTable(ctx, view, "chain");
            IM_CHECK(list && list->InnerWindow);
            IM_CHECK(!chainTable || chainTable == list->ID);
            chainTable = list->ID;
            ShowSetupTable(ctx, view, "users");
            list = SetupTable(ctx, view, "users");
            IM_CHECK(list && list->InnerWindow);
            IM_CHECK(!usersTable || usersTable == list->ID);
            usersTable = list->ID;
            IM_CHECK_LE(list->OuterRect.Max.x, page->WorkRect.Max.x + 1);
            if (width < 1000)
                IM_CHECK_GT(list->InnerWindow->ScrollMax.x, 0);
            if (width == 450)
                IM_CHECK_GT(page->ScrollMax.x, 0);
        }
        ctx->SetRef(SetupRef(view).c_str());
        ShowSetupTable(ctx, view, "##kinematic-settings");
        ctx->ItemClick(last);
        ctx->Yield(2);
        const auto popup = ctx->UiContext->OpenPopupStack.back().PopupId;
        ResizeSetup(ctx, view, 690, false);
        IM_CHECK(!ctx->UiContext->OpenPopupStack.empty() && ctx->UiContext->OpenPopupStack.back().PopupId == popup);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(2);
        IM_CHECK(details().Values().Object("end") == end);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ResizeSetup(ctx, view, 1500);
        ShowSetupTable(ctx, view, "##kinematic-settings");
        ChooseInCombo(ctx, SetupRef(view).c_str(), "##end", "-- None --");
        IM_CHECK(!details().Values().Object("end"));
        IM_CHECK_EQ(details().Values().Integer("body-count"), 0);
        IM_CHECK(ctx->ItemInfo(SetupPage(ctx, view)->GetID(">##endsetup")).ItemFlags & ImGuiItemFlags_Disabled);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        ctx->SetRef(SetupRef(view).c_str());
        Undo(ctx);
        IM_CHECK(details().Values().Object("end") == end);
        IM_CHECK_EQ(details().Values().Integer("body-count"), 3);
        ChooseInCombo(ctx, SetupRef(view).c_str(), "##end", "Chain outside");
        IM_CHECK(details().Values().Object("end") == outside);
        IM_CHECK_EQ(details().Values().Integer("body-count"), 0);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        ctx->SetRef(SetupRef(view).c_str());
        Undo(ctx);
        ChooseInCombo(ctx, SetupRef(view).c_str(), "##start", "Chain middle");
        IM_CHECK(details().Values().Object("start") == middle);
        IM_CHECK_EQ(details().Values().Integer("body-count"), 2);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        ctx->SetRef(SetupRef(view).c_str());
        Undo(ctx);
        IM_CHECK(details().Values().Object("start") == start);
        ShowSetupTable(ctx, view, "chain");
        const int row = 1;
        const auto seed = ImHashData(&row, sizeof(row), chainTable);
        ctx->ItemDoubleClick(ImHashStr("Chain middle", 0, seed), ImGuiTestOpFlags_MoveToEdgeL);
        ctx->Yield(3);
        IM_CHECK(ShowsObject(ctx, middle.object));
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(EditorCommands().Command("object.open-setup", {ObjectArgument("target", target)}).status <=
                            EDITOR_NO_CHANGE);
        });
        ResizeSetup(ctx, view, 1500);
        ShowSetupTable(ctx, view, "users");
        const auto userSeed = ImHashData(&parameter.object, sizeof(int), usersTable);
        ctx->ItemDoubleClick(ImHashStr("Chain parameter owner", 0, userSeed), ImGuiTestOpFlags_MoveToEdgeL);
        ctx->Yield(3);
        IM_CHECK(ShowsObject(ctx, owner.object));
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(EditorCommands().Command("object.open-setup", {ObjectArgument("target", target)}).status <=
                            EDITOR_NO_CHANGE);
        });
        ResizeSetup(ctx, view, 1500);
        ShowSetupTable(ctx, view, "##kinematic-settings");
        ctx->ItemClick(SetupPage(ctx, view)->GetID(">##startsetup"));
        ctx->Yield(3);
        IM_CHECK(ShowsObject(ctx, start.object));
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *parameter =
        IM_REGISTER_TEST(engine, "appearanceparameters", "parameter_reflow_value_destinations_cancel_and_undo");
    parameter->GuiFunc = MainThreadGui;
    parameter->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto objects = MakeSetupParameters(ctx);
        IM_CHECK(objects.local && objects.output && objects.myself);
        EditorView view = 0;
        EditorConfiguration original;
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(
                EditorCommands().Command("object.open-setup", {ObjectArgument("target", objects.output)}).status <=
                EDITOR_NO_CHANGE);
            auto &workspace = App().WorkspaceForTests();
            view = workspace.Role("setupparameter");
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(view && workspace.Configure(config, error));
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        ImGuiID edit = 0, destinations = 0;
        for (const float width : {1500.f, 690.f, 450.f, 1500.f}) {
            ResizeSetup(ctx, view, width);
            ShowSetupTable(ctx, view, "##parameter-settings");
            auto *page = SetupPage(ctx, view);
            auto *table = SetupTable(ctx, view, "##parameter-settings");
            IM_CHECK(table);
            IM_CHECK_EQ(table->ColumnsCount, width > 1000 ? 2 : 1);
            IM_CHECK(!edit || edit == page->GetID("Edit Value..."));
            edit = page->GetID("Edit Value...");
            const auto rect = ctx->ItemInfo(edit).RectFull;
            IM_CHECK(rect.Min.x >= table->Columns[0].WorkMinX - 1 && rect.Max.x <= table->Columns[0].WorkMaxX + 1);
            auto *list = SetupTable(ctx, view, "destinations");
            IM_CHECK(list && list->InnerWindow);
            IM_CHECK(!destinations || destinations == list->ID);
            destinations = list->ID;
            if (width < 1000)
                IM_CHECK_GT(list->InnerWindow->ScrollMax.x, 0);
            if (width == 450)
                IM_CHECK_GT(page->ScrollMax.x, 0);
        }
        const auto output = SetupParameterDetails(ctx, objects.output, "parameter");
        IM_CHECK(output.Values().Boolean("output") && output.Values().Rows("destinations").size() == 1);
        ShowSetupTable(ctx, view, "destinations");
        ctx->SetRef(SetupRef(view).c_str());
        const int row = 0;
        const auto seed = ImHashData(&row, sizeof(row), destinations);
        ctx->ItemDoubleClick(ImHashStr(objects.destinationName.c_str(), 0, seed), ImGuiTestOpFlags_MoveToEdgeL);
        ctx->Yield(3);
        IM_CHECK(ShowsObject(ctx, objects.destination.object));
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(
                EditorCommands().Command("object.open-setup", {ObjectArgument("target", objects.local)}).status <=
                EDITOR_NO_CHANGE);
        });
        ResizeSetup(ctx, view, 1500);
        IM_CHECK_EQ(SetupInteger(ctx, objects.local), 7);
        EditSetupInteger(ctx, view, 41, 690, false);
        IM_CHECK_EQ(SetupInteger(ctx, objects.local), 7);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        EditSetupInteger(ctx, view, 52, 1500, true);
        IM_CHECK_EQ(SetupInteger(ctx, objects.local), 52);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        ctx->SetRef(SetupRef(view).c_str());
        Undo(ctx);
        IM_CHECK_EQ(SetupInteger(ctx, objects.local), 7);
        const auto show = [&](ObjectRef target) {
            OnMainThread(ctx, [&] {
                IM_CHECK_NO_RET(
                    EditorCommands().Command("object.open-setup", {ObjectArgument("target", target)}).status <=
                    EDITOR_NO_CHANGE);
            });
            ResizeSetup(ctx, view, 690);
            ShowSetupTable(ctx, view, "##parameter-settings");
        };
        show(objects.myself);
        const auto myself = SetupParameterDetails(ctx, objects.myself, "parameter");
        IM_CHECK_EQ(myself.Values().Text("heading"), "This Parameter");
        IM_CHECK(ctx->ItemInfo(SetupPage(ctx, view)->GetID("Edit Value...")).ItemFlags & ImGuiItemFlags_Disabled);
        show(objects.text);
        auto *form = SetupTable(ctx, view, "##parameter-value");
        auto *section = SetupTable(ctx, view, "##parameter-settings");
        IM_CHECK(form && section);
        IM_CHECK_LE(form->OuterRect.Max.x, section->Columns[0].WorkMaxX + 1);
        IM_CHECK_GT(form->OuterRect.GetHeight(), ImGui::GetTextLineHeight() * 2);
        show(objects.reference);
        ctx->SetRef(SetupRef(view).c_str());
        ctx->ItemClick(ImHashStr(">", 0, SetupPage(ctx, view)->GetID("Object")));
        ctx->Yield(3);
        IM_CHECK(ShowsObject(ctx, objects.referenced.object));
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        show(objects.local);
        ctx->ItemClick(SetupPage(ctx, view)->GetID("Show in Schematic"));
        ctx->Yield(3);
        IM_CHECK_EQ(App().Canvas().Navigation().selection.script, objects.script.object);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *input =
        IM_REGISTER_TEST(engine, "appearanceparameters", "parameter_input_reflow_sources_editability_and_undo");
    input->GuiFunc = MainThreadGui;
    input->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto objects = MakeSetupParameters(ctx);
        IM_CHECK(objects.ownedInput && objects.sharedInput && objects.linkedInput);
        EditorView view = 0;
        EditorConfiguration original;
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(
                EditorCommands().Command("object.open-setup", {ObjectArgument("target", objects.ownedInput)}).status <=
                EDITOR_NO_CHANGE);
            auto &workspace = App().WorkspaceForTests();
            view = workspace.Role("setupparameterin");
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(view && workspace.Configure(config, error));
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        ImGuiID edit = 0, direct = 0;
        for (const float width : {1500.f, 690.f, 450.f, 1500.f}) {
            ResizeSetup(ctx, view, width);
            ShowSetupTable(ctx, view, "##parameterinput-settings");
            auto *page = SetupPage(ctx, view);
            auto *table = SetupTable(ctx, view, "##parameterinput-settings");
            IM_CHECK(table);
            IM_CHECK_EQ(table->ColumnsCount, width > 1000 ? 3 : 1);
            IM_CHECK(!edit || edit == page->GetID("Edit Value..."));
            edit = page->GetID("Edit Value...");
            const auto link = ImHashStr(">", 0, page->GetID("Linked to"));
            IM_CHECK(!direct || direct == link);
            direct = link;
            for (const auto id : {edit, direct, ImHashStr(">", 0, page->GetID("Owner"))}) {
                const auto rect = ctx->ItemInfo(id).RectFull;
                bool inside = false;
                for (int c = 0; c < table->ColumnsCount; ++c)
                    inside |=
                        rect.Min.x >= table->Columns[c].WorkMinX - 1 && rect.Max.x <= table->Columns[c].WorkMaxX + 1;
                IM_CHECK(inside);
            }
            if (width == 450)
                IM_CHECK_GT(page->ScrollMax.x, 0);
        }
        EditSetupInteger(ctx, view, 77, 690, false);
        IM_CHECK_EQ(SetupInteger(ctx, objects.ownedValue), 13);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        EditSetupInteger(ctx, view, 85, 1500, true);
        IM_CHECK_EQ(SetupInteger(ctx, objects.ownedValue), 85);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        ctx->SetRef(SetupRef(view).c_str());
        Undo(ctx);
        IM_CHECK_EQ(SetupInteger(ctx, objects.ownedValue), 13);
        const auto show = [&](ObjectRef target) {
            OnMainThread(ctx, [&] {
                IM_CHECK_NO_RET(
                    EditorCommands().Command("object.open-setup", {ObjectArgument("target", target)}).status <=
                    EDITOR_NO_CHANGE);
            });
            ResizeSetup(ctx, view, 1500);
            ShowSetupTable(ctx, view, "##parameterinput-settings");
            ctx->SetRef(SetupRef(view).c_str());
        };
        show(objects.sharedInput);
        const auto shared = SetupParameterDetails(ctx, objects.sharedInput, "parameter-input");
        IM_CHECK(shared.Values().Object("shared") == objects.ownedInput &&
                 shared.Values().Object("value") == objects.ownedValue);
        IM_CHECK(ctx->ItemInfo(SetupPage(ctx, view)->GetID("Edit Value...")).ItemFlags & ImGuiItemFlags_Disabled);
        ctx->ItemClick(ImHashStr(">", 0, SetupPage(ctx, view)->GetID("Shares with")));
        ctx->Yield(3);
        IM_CHECK(ShowsObject(ctx, objects.ownedInput.object));
        show(objects.sharedInput);
        ctx->ItemClick(ImHashStr(">", 0, SetupPage(ctx, view)->GetID("Real source")));
        ctx->Yield(3);
        IM_CHECK(ShowsObject(ctx, objects.ownedValue.object));
        show(objects.linkedInput);
        const auto linked = SetupParameterDetails(ctx, objects.linkedInput, "parameter-input");
        IM_CHECK(linked.Values().Object("direct") == objects.output);
        IM_CHECK(ctx->ItemInfo(SetupPage(ctx, view)->GetID("Edit Value...")).ItemFlags & ImGuiItemFlags_Disabled);
        ctx->ItemClick(ImHashStr(">", 0, SetupPage(ctx, view)->GetID("Linked to")));
        ctx->Yield(3);
        IM_CHECK(ShowsObject(ctx, objects.output.object));
        show(objects.emptyInput);
        IM_CHECK(ctx->ItemInfo(SetupPage(ctx, view)->GetID("Edit Value...")).ItemFlags & ImGuiItemFlags_Disabled);
        IM_CHECK(ctx->ItemInfo(ImHashStr(">", 0, SetupPage(ctx, view)->GetID("Linked to"))).ItemFlags &
                 ImGuiItemFlags_Disabled);
        OnMainThread(ctx,
                     [&] { CKParameterIn::Cast(App().State().document->Resolve(objects.ownedInput))->Enable(FALSE); });
        show(objects.ownedInput);
        IM_CHECK(!SetupParameterDetails(ctx, objects.ownedInput, "parameter-input").Values().Boolean("enabled"));
        IM_CHECK(!(ctx->ItemInfo(SetupPage(ctx, view)->GetID("Edit Value...")).ItemFlags & ImGuiItemFlags_Disabled));
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *operation =
        IM_REGISTER_TEST(engine, "appearanceparameters", "parameter_operation_reflow_operands_navigation_and_undo");
    operation->GuiFunc = MainThreadGui;
    operation->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto objects = MakeSetupParameters(ctx);
        IM_CHECK(objects.operation && objects.firstOperand && objects.operandValue);
        EditorView view = 0;
        EditorConfiguration original;
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(
                EditorCommands().Command("object.open-setup", {ObjectArgument("target", objects.operation)}).status <=
                EDITOR_NO_CHANGE);
            auto &workspace = App().WorkspaceForTests();
            view = workspace.Role("setupoperation");
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(view && workspace.Configure(config, error));
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        ImGuiID listId = 0, owner = 0;
        for (const float width : {1500.f, 690.f, 450.f, 1500.f}) {
            ResizeSetup(ctx, view, width);
            ShowSetupTable(ctx, view, "##operation-settings");
            auto *page = SetupPage(ctx, view);
            auto *table = SetupTable(ctx, view, "##operation-settings");
            IM_CHECK(table);
            IM_CHECK_EQ(table->ColumnsCount, width > 1000 ? 2 : 1);
            auto *list = SetupTable(ctx, view, "operands");
            IM_CHECK(list && list->InnerWindow);
            IM_CHECK(!listId || listId == list->ID);
            listId = list->ID;
            const auto id = ImHashStr(">", 0, page->GetID("Owner"));
            IM_CHECK(!owner || id == owner);
            owner = id;
            const auto rect = ctx->ItemInfo(owner).RectFull;
            IM_CHECK(rect.Min.x >= table->Columns[0].WorkMinX - 1 && rect.Max.x <= table->Columns[0].WorkMaxX + 1);
            if (width < 1000)
                IM_CHECK_GT(list->InnerWindow->ScrollMax.x, 0);
            if (width == 450)
                IM_CHECK_GT(page->ScrollMax.x, 0);
        }
        const auto description = SetupParameterDetails(ctx, objects.operation, "operation");
        const auto &rows = description.Values().Rows("operands");
        IM_CHECK_EQ(rows.size(), 3);
        const auto choose = [&](int row) {
            ShowSetupTable(ctx, view, "operands");
            ctx->SetRef(SetupRef(view).c_str());
            const auto *list = SetupTable(ctx, view, "operands");
            ctx->ScrollToX(list->InnerWindow->ID, 0);
            ctx->Yield(2);
            const auto name = SetupFields(rows[size_t(row)].fields).Text("name");
            const auto seed = ImHashStr(rows[size_t(row)].key.c_str(), 0, listId);
            ctx->ItemDoubleClick(ImHashStr(name.c_str(), 0, seed), ImGuiTestOpFlags_MoveToEdgeL);
            ctx->Yield(3);
        };
        choose(0);
        IM_CHECK(ShowsObject(ctx, objects.firstOperand.object));
        const auto inputView =
            Read<EditorView>(ctx, [&] { return App().WorkspaceForTests().Role("setupparameterin"); });
        IM_CHECK(inputView);
        EditSetupInteger(ctx, inputView, 63, 690, false);
        IM_CHECK_EQ(SetupInteger(ctx, objects.operandValue), 17);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        EditSetupInteger(ctx, inputView, 74, 1500, true);
        IM_CHECK_EQ(SetupInteger(ctx, objects.operandValue), 74);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        ctx->SetRef(SetupRef(inputView).c_str());
        Undo(ctx);
        IM_CHECK_EQ(SetupInteger(ctx, objects.operandValue), 17);
        const auto showOperation = [&] {
            OnMainThread(ctx, [&] {
                IM_CHECK_NO_RET(EditorCommands()
                                    .Command("object.open-setup", {ObjectArgument("target", objects.operation)})
                                    .status <= EDITOR_NO_CHANGE);
            });
            ResizeSetup(ctx, view, 1500);
        };
        showOperation();
        choose(1);
        IM_CHECK(ShowsObject(ctx, objects.secondOperand.object));
        auto *inputPage = SetupPage(ctx, inputView);
        IM_CHECK(ctx->ItemInfo(inputPage->GetID("Edit Value...")).ItemFlags & ImGuiItemFlags_Disabled);
        showOperation();
        choose(2);
        IM_CHECK(ShowsObject(ctx, objects.result.object));
        showOperation();
        ShowSetupTable(ctx, view, "##operation-settings");
        ctx->ItemClick(SetupPage(ctx, view)->GetID("Show in Schematic"));
        ctx->Yield(3);
        IM_CHECK_EQ(App().Canvas().Navigation().selection.script, objects.script.object);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        showOperation();
        ShowSetupTable(ctx, view, "##operation-settings");
        ctx->ItemClick(owner);
        ctx->Yield(3);
        IM_CHECK(ShowsObject(ctx, objects.script.object));
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(EditorCommands()
                                .Command("object.open-setup", {ObjectArgument("target", objects.emptyOperation)})
                                .status <= EDITOR_NO_CHANGE);
        });
        ResizeSetup(ctx, view, 690);
        ShowSetupTable(ctx, view, "##operation-settings");
        IM_CHECK(SetupTable(ctx, view, "operands"));
        IM_CHECK(ctx->ItemInfo(SetupPage(ctx, view)->GetID("Show in Schematic")).ItemFlags & ImGuiItemFlags_Disabled);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *behavior =
        IM_REGISTER_TEST(engine, "appearancebehaviors", "behavior_reflow_priority_cancel_undo_and_navigation");
    behavior->GuiFunc = MainThreadGui;
    behavior->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto objects = MakeSetupBehaviors(ctx);
        IM_CHECK(objects.graph && objects.block && objects.target);
        EditorView view = 0;
        EditorConfiguration original;
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(
                EditorCommands().Command("object.open-setup", {ObjectArgument("target", objects.graph)}).status <=
                EDITOR_NO_CHANGE);
            auto &workspace = App().WorkspaceForTests();
            view = workspace.Role("setupbehavior");
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(view && workspace.Configure(config, error));
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        ImGuiID priority = 0, children = 0;
        for (const float width : {1500.f, 690.f, 450.f, 1500.f}) {
            ResizeSetup(ctx, view, width);
            ShowSetupTable(ctx, view, "##behavior-settings");
            auto *page = SetupPage(ctx, view);
            auto *table = SetupTable(ctx, view, "##behavior-settings");
            IM_CHECK(table);
            IM_CHECK_EQ(table->ColumnsCount, width > 1000 ? 3 : 1);
            IM_CHECK(!priority || priority == page->GetID("##priority"));
            priority = page->GetID("##priority");
            for (const auto id :
                 {priority, ImHashStr(">", 0, page->GetID("Owner")), ImHashStr(">", 0, page->GetID("Script"))}) {
                const auto rect = ctx->ItemInfo(id).RectFull;
                IM_CHECK(rect.Min.x >= table->Columns[0].WorkMinX - 1 && rect.Max.x <= table->Columns[0].WorkMaxX + 1);
            }
            for (const char *label : {"Active", "Building Block", "Variable Output Parameters"})
                IM_CHECK(ctx->ItemInfo(page->GetID(label)).ItemFlags & ImGuiItemFlags_Disabled);
            ShowSetupTable(ctx, view, "behaviors");
            auto *list = SetupTable(ctx, view, "behaviors");
            IM_CHECK(list && list->InnerWindow);
            IM_CHECK(!children || children == list->ID);
            children = list->ID;
            IM_CHECK_GT(list->InnerWindow->ScrollMax.x, 0);
            if (width == 450)
                IM_CHECK_GT(page->ScrollMax.x, 0);
        }
        const auto details = [&](ObjectRef target) {
            return Read<SetupDetails>(ctx, [&] {
                SetupDetails data;
                ReadSetupDetails(EditorCommands(), target, "behavior", data);
                return data;
            });
        };
        const auto nativePriority = [&] {
            return Read<int>(
                ctx, [&] { return CKBehavior::Cast(App().State().document->Resolve(objects.graph))->GetPriority(); });
        };
        ctx->SetRef(SetupRef(view).c_str());
        ctx->ItemInput(priority);
        ctx->KeyCharsReplace("41");
        ResizeSetup(ctx, view, 690, false);
        IM_CHECK_EQ(ctx->UiContext->ActiveId, priority);
        IM_CHECK_EQ(nativePriority(), 7);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
        IM_CHECK_EQ(nativePriority(), 7);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ctx->ItemInputValue(priority, -17);
        ctx->Yield(3);
        IM_CHECK_EQ(nativePriority(), -17);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK_EQ(nativePriority(), 7);
        Redo(ctx);
        IM_CHECK_EQ(nativePriority(), -17);
        Undo(ctx);
        IM_CHECK_EQ(nativePriority(), 7);
        ResizeSetup(ctx, view, 1500);
        ShowSetupTable(ctx, view, "behaviors");
        auto *list = SetupTable(ctx, view, "behaviors");
        ctx->ScrollToX(list->InnerWindow->ID, 0);
        ctx->Yield(2);
        const auto graph = details(objects.graph);
        IM_CHECK(graph.Values().Boolean("graph") && graph.Values().Rows("behaviors").size() == 2);
        const auto &rows = graph.Values().Rows("behaviors");
        const auto child = std::find_if(rows.begin(), rows.end(), [&](const auto &row) {
            return SetupFields(row.fields).Object("target") == objects.child;
        });
        IM_CHECK(child != rows.end());
        const int index = int(child - rows.begin());
        const auto seed = ImHashData(&index, sizeof(index), children);
        ctx->ItemDoubleClick(ImHashStr(SetupFields(child->fields).Text("name").c_str(), 0, seed),
                             ImGuiTestOpFlags_MoveToEdgeL);
        ctx->Yield(3);
        IM_CHECK(ShowsObject(ctx, objects.child.object));
        const auto nested = details(objects.child);
        IM_CHECK(nested.Values().Object("parent") == objects.graph &&
                 nested.Values().Object("script") == objects.script);
        ResizeSetup(ctx, view, 690);
        ctx->ItemClick(ImHashStr(">", 0, SetupPage(ctx, view)->GetID("Parent")));
        ctx->Yield(3);
        IM_CHECK(ShowsObject(ctx, objects.graph.object));
        const auto show = [&](ObjectRef target) {
            OnMainThread(ctx, [&] {
                IM_CHECK_NO_RET(
                    EditorCommands().Command("object.open-setup", {ObjectArgument("target", target)}).status <=
                    EDITOR_NO_CHANGE);
            });
            ResizeSetup(ctx, view, 1500);
            ShowSetupTable(ctx, view, "##behavior-settings");
            ctx->SetRef(SetupRef(view).c_str());
        };
        show(objects.block);
        const auto block = details(objects.block);
        IM_CHECK(!block.Values().Boolean("graph") && block.Values().Boolean("prototype") &&
                 block.Values().Boolean("using-target") && !block.Values().Text("guid").empty());
        ctx->ItemClick(ImHashStr(">", 0, SetupPage(ctx, view)->GetID("Target")));
        ctx->Yield(3);
        IM_CHECK(ShowsObject(ctx, objects.target.object));
        show(objects.script);
        IM_CHECK_EQ(details(objects.script).Values().Text("heading"), "Script");
        ctx->ItemClick(ImHashStr(">", 0, SetupPage(ctx, view)->GetID("Owner")));
        ctx->Yield(3);
        IM_CHECK(ShowsObject(ctx, objects.owner.object));
        show(objects.graph);
        ctx->ItemClick(SetupPage(ctx, view)->GetID("Show in Schematic"));
        ctx->Yield(3);
        IM_CHECK_EQ(App().Canvas().Navigation().selection.script, objects.script.object);
        show(objects.detached);
        IM_CHECK(ctx->ItemInfo(SetupPage(ctx, view)->GetID("##priority")).ItemFlags & ImGuiItemFlags_Disabled);
        IM_CHECK(ctx->ItemInfo(SetupPage(ctx, view)->GetID("Show in Schematic")).ItemFlags & ImGuiItemFlags_Disabled);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *behaviorIO =
        IM_REGISTER_TEST(engine, "appearancebehaviors", "behavior_io_reflow_inner_outer_links_and_rename_undo");
    behaviorIO->GuiFunc = MainThreadGui;
    behaviorIO->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto objects = MakeSetupBehaviors(ctx);
        IM_CHECK(objects.input && objects.output);
        EditorView view = 0;
        EditorConfiguration original;
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(
                EditorCommands().Command("object.open-setup", {ObjectArgument("target", objects.input)}).status <=
                EDITOR_NO_CHANGE);
            auto &workspace = App().WorkspaceForTests();
            view = workspace.Role("setupbehaviorio");
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(view && workspace.Configure(config, error));
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        ImGuiID owner = 0, links = 0;
        for (const float width : {1500.f, 690.f, 450.f, 1500.f}) {
            ResizeSetup(ctx, view, width);
            ShowSetupTable(ctx, view, "##behaviorio-settings");
            auto *page = SetupPage(ctx, view);
            auto *table = SetupTable(ctx, view, "##behaviorio-settings");
            IM_CHECK(table);
            IM_CHECK_EQ(table->ColumnsCount, width > 1000 ? 2 : 1);
            const auto id = ImHashStr(">", 0, page->GetID("Owner"));
            IM_CHECK(!owner || id == owner);
            owner = id;
            const auto rect = ctx->ItemInfo(owner).RectFull;
            IM_CHECK(rect.Min.x >= table->Columns[0].WorkMinX - 1 && rect.Max.x <= table->Columns[0].WorkMaxX + 1);
            auto *list = SetupTable(ctx, view, "links");
            IM_CHECK(list && list->InnerWindow);
            IM_CHECK(!links || links == list->ID);
            links = list->ID;
            IM_CHECK_GT(list->InnerWindow->ScrollMax.x, 0);
            if (width == 450)
                IM_CHECK_GT(page->ScrollMax.x, 0);
        }
        const auto details = [&](ObjectRef target) {
            return Read<SetupDetails>(ctx, [&] {
                SetupDetails data;
                ReadSetupDetails(EditorCommands(), target, "behavior-io", data);
                return data;
            });
        };
        const auto input = details(objects.input);
        const auto &rows = input.Values().Rows("links");
        IM_CHECK(input.Values().Boolean("input") && rows.size() == 2);
        std::set<ObjectRef> connected;
        for (const auto &row : rows)
            connected.insert(SetupFields(row.fields).Object("target"));
        IM_CHECK(connected == std::set<ObjectRef>({objects.outerInputLink, objects.innerInputLink}));
        ctx->SetRef(SetupRef(view).c_str());
        const auto name = ObjectName(ctx, objects.input.object);
        ctx->KeyPress(ImGuiKey_F2);
        ctx->Yield(3);
        ctx->ItemInput("//###setuprename/##name");
        ctx->KeyCharsReplace("Cancelled Setup IO name");
        ResizeSetup(ctx, view, 690, false);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
        IM_CHECK_EQ(ObjectName(ctx, objects.input.object), name);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ctx->WindowFocus(SetupRef(view).c_str());
        ctx->KeyPress(ImGuiKey_F2);
        ctx->Yield(3);
        ctx->ItemInputValue("//###setuprename/##name", "Confirmed Setup IO name");
        ctx->Yield(3);
        IM_CHECK_EQ(ObjectName(ctx, objects.input.object), "Confirmed Setup IO name");
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK_EQ(ObjectName(ctx, objects.input.object), name);
        const auto show = [&](ObjectRef target) {
            OnMainThread(ctx, [&] {
                IM_CHECK_NO_RET(
                    EditorCommands().Command("object.open-setup", {ObjectArgument("target", target)}).status <=
                    EDITOR_NO_CHANGE);
            });
            ResizeSetup(ctx, view, 1500);
            ctx->SetRef(SetupRef(view).c_str());
        };
        for (const auto &value : rows) {
            show(objects.input);
            ShowSetupTable(ctx, view, "links");
            auto *list = SetupTable(ctx, view, "links");
            ctx->ScrollToX(list->InnerWindow->ID, 0);
            ctx->Yield(2);
            const SetupFields row(value.fields);
            const int id = int(row.Object("target").object);
            const auto seed = ImHashData(&id, sizeof(id), links);
            ctx->ItemDoubleClick(ImHashStr(row.Text("from").c_str(), 0, seed), ImGuiTestOpFlags_MoveToEdgeL);
            ctx->Yield(3);
            IM_CHECK(ShowsObject(ctx, row.Object("target").object));
        }
        show(objects.output);
        const auto output = details(objects.output);
        IM_CHECK(!output.Values().Boolean("input") && output.Values().Rows("links").size() == 2);
        connected.clear();
        for (const auto &row : output.Values().Rows("links"))
            connected.insert(SetupFields(row.fields).Object("target"));
        IM_CHECK(connected == std::set<ObjectRef>({objects.innerOutputLink, objects.outerOutputLink}));
        ctx->ItemClick(SetupPage(ctx, view)->GetID("Show in Schematic"));
        ctx->Yield(3);
        IM_CHECK_EQ(App().Canvas().Navigation().selection.script, objects.script.object);
        show(objects.input);
        ctx->ItemClick(owner);
        ctx->Yield(3);
        IM_CHECK(ShowsObject(ctx, objects.graph.object));
        show(objects.emptyIO);
        IM_CHECK(details(objects.emptyIO).Values().Rows("links").empty());
        IM_CHECK(ctx->ItemInfo(SetupPage(ctx, view)->GetID("Show in Schematic")).ItemFlags & ImGuiItemFlags_Disabled);
        IM_CHECK(ctx->ItemInfo(ImHashStr(">", 0, SetupPage(ctx, view)->GetID("Owner"))).ItemFlags &
                 ImGuiItemFlags_Disabled);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *behaviorLink =
        IM_REGISTER_TEST(engine, "appearancebehaviors", "behavior_link_reflow_delay_cancel_undo_and_endpoints");
    behaviorLink->GuiFunc = MainThreadGui;
    behaviorLink->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto objects = MakeSetupBehaviors(ctx);
        IM_CHECK(objects.innerInputLink && objects.input && objects.childInput);
        EditorView view = 0;
        EditorConfiguration original;
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(EditorCommands()
                                .Command("object.open-setup", {ObjectArgument("target", objects.innerInputLink)})
                                .status <= EDITOR_NO_CHANGE);
            auto &workspace = App().WorkspaceForTests();
            view = workspace.Role("setupbehaviorlink");
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(view && workspace.Configure(config, error));
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        ImGuiID delay = 0, from = 0, to = 0;
        for (const float width : {1500.f, 690.f, 450.f, 1500.f}) {
            ResizeSetup(ctx, view, width);
            ShowSetupTable(ctx, view, "##behaviorlink-settings");
            auto *page = SetupPage(ctx, view);
            auto *table = SetupTable(ctx, view, "##behaviorlink-settings");
            IM_CHECK(table);
            IM_CHECK_EQ(table->ColumnsCount, width > 1000 ? 2 : 1);
            IM_CHECK(!delay || delay == page->GetID("##delay"));
            delay = page->GetID("##delay");
            const auto a = ImHashStr(">", 0, page->GetID("From")), b = ImHashStr(">", 0, page->GetID("To"));
            IM_CHECK((!from || from == a) && (!to || to == b));
            from = a;
            to = b;
            for (const auto id : {delay, from, to}) {
                const auto rect = ctx->ItemInfo(id).RectFull;
                const auto &column = table->Columns[id == delay ? table->ColumnsCount - 1 : 0];
                IM_CHECK(rect.Min.x >= column.WorkMinX - 1 && rect.Max.x <= column.WorkMaxX + 1);
            }
            auto *form = SetupTable(ctx, view, "##behaviorlink-ends");
            IM_CHECK(form && form->OuterRect.Max.x <= table->Columns[0].WorkMaxX + 1);
            IM_CHECK_GT(form->OuterRect.GetHeight(), ImGui::GetTextLineHeight() * 4);
            if (width == 450)
                IM_CHECK_GT(page->ScrollMax.x, 0);
        }
        const auto details = [&](ObjectRef target) {
            return Read<SetupDetails>(ctx, [&] {
                SetupDetails data;
                ReadSetupDetails(EditorCommands(), target, "behavior-link", data);
                return data;
            });
        };
        const auto nativeDelay = [&] {
            return Read<int>(ctx, [&] {
                return CKBehaviorLink::Cast(App().State().document->Resolve(objects.innerInputLink))
                    ->GetInitialActivationDelay();
            });
        };
        const auto link = details(objects.innerInputLink);
        IM_CHECK(link.Values().Object("from") == objects.input && link.Values().Object("to") == objects.childInput);
        IM_CHECK_EQ(link.Values().Integer("current-delay"), 2);
        ctx->SetRef(SetupRef(view).c_str());
        ctx->ItemInput(delay);
        ctx->KeyCharsReplace("12");
        ResizeSetup(ctx, view, 690, false);
        IM_CHECK_EQ(ctx->UiContext->ActiveId, delay);
        IM_CHECK_EQ(nativeDelay(), 4);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
        IM_CHECK_EQ(nativeDelay(), 4);
        IM_CHECK_EQ(details(objects.innerInputLink).Values().Integer("current-delay"), 2);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ctx->ItemInputValue(delay, 9);
        ctx->Yield(3);
        IM_CHECK_EQ(nativeDelay(), 9);
        // The existing graph command resets both delays on commit and Undo/Redo.
        IM_CHECK_EQ(details(objects.innerInputLink).Values().Integer("current-delay"), 9);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK_EQ(nativeDelay(), 4);
        IM_CHECK_EQ(details(objects.innerInputLink).Values().Integer("current-delay"), 4);
        Redo(ctx);
        IM_CHECK_EQ(nativeDelay(), 9);
        IM_CHECK_EQ(details(objects.innerInputLink).Values().Integer("current-delay"), 9);
        Undo(ctx);
        IM_CHECK_EQ(nativeDelay(), 4);
        for (const auto typed : {-1, 32765}) {
            ctx->ItemInputValue(delay, typed);
            ctx->Yield(3);
            const int expected = typed < 0 ? 0 : 32764;
            IM_CHECK_EQ(nativeDelay(), expected);
            IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
            Undo(ctx);
            IM_CHECK_EQ(nativeDelay(), 4);
        }
        const auto show = [&](ObjectRef target) {
            OnMainThread(ctx, [&] {
                IM_CHECK_NO_RET(
                    EditorCommands().Command("object.open-setup", {ObjectArgument("target", target)}).status <=
                    EDITOR_NO_CHANGE);
            });
            ResizeSetup(ctx, view, 1500);
            ctx->SetRef(SetupRef(view).c_str());
        };
        ctx->ItemClick(from);
        ctx->Yield(3);
        IM_CHECK(ShowsObject(ctx, objects.input.object));
        show(objects.innerInputLink);
        ctx->ItemClick(to);
        ctx->Yield(3);
        IM_CHECK(ShowsObject(ctx, objects.childInput.object));
        show(objects.innerInputLink);
        ctx->ItemClick(SetupPage(ctx, view)->GetID("Show in Schematic"));
        ctx->Yield(3);
        IM_CHECK_EQ(App().Canvas().Navigation().selection.script, objects.script.object);
        show(objects.partialLink);
        const auto partial = details(objects.partialLink);
        IM_CHECK(!partial.Values().Object("from") && partial.Values().Object("to") == objects.childInput);
        IM_CHECK(ctx->ItemInfo(from).ItemFlags & ImGuiItemFlags_Disabled);
        IM_CHECK(!(ctx->ItemInfo(to).ItemFlags & ImGuiItemFlags_Disabled));
        show(objects.emptyLink);
        IM_CHECK(ctx->ItemInfo(from).ItemFlags & ImGuiItemFlags_Disabled);
        IM_CHECK(ctx->ItemInfo(to).ItemFlags & ImGuiItemFlags_Disabled);
        IM_CHECK(ctx->ItemInfo(delay).ItemFlags & ImGuiItemFlags_Disabled);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *synchro = IM_REGISTER_TEST(engine, "appearancesyncstate", "synchro_reflow_cancel_undo_reset_and_navigation");
    synchro->GuiFunc = MainThreadGui;
    synchro->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto objects = MakeSetupControls(ctx);
        IM_CHECK(objects.synchro && objects.arrived.size() == 16);
        EditorView view = 0;
        EditorConfiguration original;
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(
                EditorCommands().Command("object.open-setup", {ObjectArgument("target", objects.synchro)}).status <=
                EDITOR_NO_CHANGE);
            auto &workspace = App().WorkspaceForTests();
            view = workspace.Role("setupsynchro");
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(view && workspace.Configure(config, error));
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        ImGuiID users = 0, arrived = 0, usedBy = 0;
        for (const float width : {1500.f, 690.f, 450.f, 1500.f}) {
            ResizeSetup(ctx, view, width);
            ShowSetupTable(ctx, view, "##synchro-settings");
            auto *page = SetupPage(ctx, view);
            auto *table = SetupTable(ctx, view, "##synchro-settings");
            IM_CHECK(table);
            IM_CHECK_EQ(table->ColumnsCount, width > 1000 ? 3 : 1);
            IM_CHECK(!users || users == page->GetID("##users"));
            users = page->GetID("##users");
            for (const auto id : {users, page->GetID("Reset")}) {
                const auto rect = ctx->ItemInfo(id).RectFull;
                IM_CHECK(rect.Min.x >= table->Columns[0].WorkMinX - 1 && rect.Max.x <= table->Columns[0].WorkMaxX + 1);
            }
            auto *list = SetupChild(ctx, view, "arrived");
            IM_CHECK(list);
            IM_CHECK(!arrived || arrived == list->ID);
            arrived = list->ID;
            IM_CHECK_GT(list->ScrollMax.x, 0);
            IM_CHECK_GT(list->ScrollMax.y, 0);
            auto *used = SetupTable(ctx, view, "users");
            IM_CHECK(used && used->InnerWindow);
            IM_CHECK(!usedBy || usedBy == used->ID);
            usedBy = used->ID;
            IM_CHECK_GT(used->InnerWindow->ScrollMax.x, 0);
            if (width == 450)
                IM_CHECK_GT(page->ScrollMax.x, 0);
        }
        const auto count = [&] {
            return Read<int>(ctx, [&] {
                return CKSynchroObject::Cast(App().State().document->Resolve(objects.synchro))
                    ->GetRendezVousNumberOfWaiters();
            });
        };
        const auto arrivals = [&] {
            return Read<int>(ctx, [&] {
                return CKSynchroObject::Cast(App().State().document->Resolve(objects.synchro))
                    ->GetRendezVousNumberOfArrivedObjects();
            });
        };
        ctx->SetRef(SetupRef(view).c_str());
        ctx->ScrollToItemY(users);
        const auto at = ctx->ItemInfo(users).RectFull.GetCenter();
        ctx->MouseMoveToPos(at);
        ctx->MouseDown(0);
        ctx->MouseMoveToPos({at.x + 40, at.y});
        ctx->Yield(3);
        IM_CHECK(count() != 100);
        ResizeSetup(ctx, view, 690, false);
        IM_CHECK_EQ(ctx->UiContext->ActiveId, users);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->MouseUp(0);
        ctx->Yield(3);
        IM_CHECK_EQ(count(), 100);
        IM_CHECK_EQ(arrivals(), 16);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ctx->ItemInputValue(users, 103);
        ctx->Yield(3);
        IM_CHECK_EQ(count(), 103);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK_EQ(count(), 100);
        Redo(ctx);
        IM_CHECK_EQ(count(), 103);
        Undo(ctx);
        IM_CHECK_EQ(arrivals(), 16);
        ResizeSetup(ctx, view, 1500);
        auto *list = SetupChild(ctx, view, "arrived");
        ctx->ScrollToX(list->ID, 0);
        ctx->ScrollToTop(list->ID);
        ctx->Yield(2);
        const int index = 0;
        const auto seed = ImHashData(&index, sizeof(index), list->ID);
        const auto name = ObjectName(ctx, objects.arrived.front().object);
        ctx->ItemDoubleClick(ImHashStr(name.c_str(), 0, seed), ImGuiTestOpFlags_MoveToEdgeL);
        ctx->Yield(3);
        IM_CHECK(ShowsObject(ctx, objects.arrived.front().object));
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(
                EditorCommands().Command("object.open-setup", {ObjectArgument("target", objects.synchro)}).status <=
                EDITOR_NO_CHANGE);
        });
        ResizeSetup(ctx, view, 1500);
        const auto revision = App().State().document->ChangeRevision();
        const bool dirty = App().State().document->Dirty(), redo = App().State().History().CanRedo();
        ctx->ItemClick(SetupPage(ctx, view)->GetID("Reset"));
        ctx->Yield(3);
        IM_CHECK_EQ(arrivals(), 0);
        IM_CHECK_EQ(count(), 100);
        IM_CHECK_EQ(App().State().document->ChangeRevision(), revision);
        IM_CHECK_EQ(App().State().document->Dirty(), dirty);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        IM_CHECK_EQ(App().State().History().CanRedo(), redo);
        CheckSetupUser(ctx, view, objects.synchro, objects.synchroParameter, objects.user);
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *state = IM_REGISTER_TEST(engine, "appearancesyncstate", "state_reflow_toggle_undo_rename_cancel_and_users");
    state->GuiFunc = MainThreadGui;
    state->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto objects = MakeSetupControls(ctx);
        IM_CHECK(objects.state);
        EditorView view = 0;
        EditorConfiguration original;
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(
                EditorCommands().Command("object.open-setup", {ObjectArgument("target", objects.state)}).status <=
                EDITOR_NO_CHANGE);
            auto &workspace = App().WorkspaceForTests();
            view = workspace.Role("setupstate");
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(view && workspace.Configure(config, error));
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        ImGuiID active = 0, usedBy = 0;
        for (const float width : {1500.f, 690.f, 450.f, 1500.f}) {
            ResizeSetup(ctx, view, width);
            ShowSetupTable(ctx, view, "##state-settings");
            auto *page = SetupPage(ctx, view);
            auto *table = SetupTable(ctx, view, "##state-settings");
            IM_CHECK(table);
            IM_CHECK_EQ(table->ColumnsCount, width > 1000 ? 2 : 1);
            IM_CHECK(!active || active == page->GetID("Active (entered)"));
            active = page->GetID("Active (entered)");
            const auto rect = ctx->ItemInfo(active).RectFull;
            IM_CHECK(rect.Min.x >= table->Columns[0].WorkMinX - 1 && rect.Max.x <= table->Columns[0].WorkMaxX + 1);
            auto *list = SetupTable(ctx, view, "users");
            IM_CHECK(list && list->InnerWindow);
            IM_CHECK(!usedBy || usedBy == list->ID);
            usedBy = list->ID;
            IM_CHECK_GT(list->InnerWindow->ScrollMax.x, 0);
            if (width == 450)
                IM_CHECK_GT(page->ScrollMax.x, 0);
        }
        const auto entered = [&] {
            return Read<bool>(ctx, [&] {
                return CKStateObject::Cast(App().State().document->Resolve(objects.state))->IsStateActive() != FALSE;
            });
        };
        CheckSetupRename(ctx, view, objects.state);
        IM_CHECK(!entered());
        ctx->SetRef(SetupRef(view).c_str());
        ctx->ItemClick(active);
        ctx->Yield(3);
        IM_CHECK(entered());
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK(!entered());
        Redo(ctx);
        IM_CHECK(entered());
        Undo(ctx);
        IM_CHECK(!entered());
        CheckSetupUser(ctx, view, objects.state, objects.stateParameter, objects.user);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *critical =
        IM_REGISTER_TEST(engine, "appearancesyncstate", "critical_section_reflow_reset_rename_cancel_and_users");
    critical->GuiFunc = MainThreadGui;
    critical->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto objects = MakeSetupControls(ctx);
        IM_CHECK(objects.section && objects.arrived.size() == 16);
        EditorView view = 0;
        EditorConfiguration original;
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(
                EditorCommands().Command("object.open-setup", {ObjectArgument("target", objects.section)}).status <=
                EDITOR_NO_CHANGE);
            auto &workspace = App().WorkspaceForTests();
            view = workspace.Role("setupcriticalsection");
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(view && workspace.Configure(config, error));
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        ImGuiID reset = 0, usedBy = 0;
        for (const float width : {1500.f, 690.f, 450.f, 1500.f}) {
            ResizeSetup(ctx, view, width);
            ShowSetupTable(ctx, view, "##criticalsection-settings");
            auto *page = SetupPage(ctx, view);
            auto *table = SetupTable(ctx, view, "##criticalsection-settings");
            IM_CHECK(table);
            IM_CHECK_EQ(table->ColumnsCount, width > 1000 ? 2 : 1);
            IM_CHECK(!reset || reset == page->GetID("Reset"));
            reset = page->GetID("Reset");
            const auto rect = ctx->ItemInfo(reset).RectFull;
            IM_CHECK(rect.Min.x >= table->Columns[0].WorkMinX - 1 && rect.Max.x <= table->Columns[0].WorkMaxX + 1);
            auto *list = SetupTable(ctx, view, "users");
            IM_CHECK(list && list->InnerWindow);
            IM_CHECK(!usedBy || usedBy == list->ID);
            usedBy = list->ID;
            IM_CHECK_GT(list->InnerWindow->ScrollMax.x, 0);
            if (width == 450)
                IM_CHECK_GT(page->ScrollMax.x, 0);
        }
        CheckSetupRename(ctx, view, objects.section);
        OnMainThread(ctx, [&] {
            auto &doc = *App().State().document;
            auto *section = CKCriticalSectionObject::Cast(doc.Resolve(objects.section));
            IM_CHECK_NO_RET(!section->EnterCriticalSection(CKBeObject::Cast(doc.Resolve(objects.arrived[1]))));
        });
        const auto revision = App().State().document->ChangeRevision();
        const bool dirty = App().State().document->Dirty(), redo = App().State().History().CanRedo();
        ctx->SetRef(SetupRef(view).c_str());
        ctx->ItemClick(reset);
        ctx->Yield(3);
        OnMainThread(ctx, [&] {
            auto &doc = *App().State().document;
            auto *section = CKCriticalSectionObject::Cast(doc.Resolve(objects.section));
            auto *object = CKBeObject::Cast(doc.Resolve(objects.arrived[1]));
            IM_CHECK_NO_RET(section->EnterCriticalSection(object) && section->LeaveCriticalSection(object));
        });
        IM_CHECK_EQ(App().State().document->ChangeRevision(), revision);
        IM_CHECK_EQ(App().State().document->Dirty(), dirty);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        IM_CHECK_EQ(App().State().History().CanRedo(), redo);
        CheckSetupUser(ctx, view, objects.section, objects.sectionParameter, objects.user);
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *interfaceObjects =
        IM_REGISTER_TEST(engine, "appearancesyncstate", "interface_objects_reflow_chunks_rename_cancel_and_undo");
    interfaceObjects->GuiFunc = MainThreadGui;
    interfaceObjects->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto objects = MakeSetupControls(ctx);
        IM_CHECK(objects.interfaceObjects && objects.emptyInterface && objects.chunkSizes.size() == 34);
        EditorView view = 0;
        EditorConfiguration original;
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(EditorCommands()
                                .Command("object.open-setup", {ObjectArgument("target", objects.interfaceObjects)})
                                .status <= EDITOR_NO_CHANGE);
            auto &workspace = App().WorkspaceForTests();
            view = workspace.Role("setupinterfaceobjects");
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(view && workspace.Configure(config, error));
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        const auto details = [&](ObjectRef target) {
            return Read<SetupDetails>(ctx, [&] {
                SetupDetails data;
                ReadSetupDetails(EditorCommands(), target, "interface-objects", data);
                return data;
            });
        };
        const auto originalData = details(objects.interfaceObjects);
        IM_CHECK_EQ(originalData.Values().Integer("count"), 34);
        IM_CHECK(originalData.Values().Integers("sizes") == objects.chunkSizes);
        IM_CHECK(objects.chunkSizes[0] == UINT64_MAX && objects.chunkSizes[1] == 0);
        ImGuiID chunks = 0;
        for (const float width : {1500.f, 690.f, 450.f, 1500.f}) {
            ResizeSetup(ctx, view, width);
            ShowSetupTable(ctx, view, "##interfaceobjects-settings");
            auto *page = SetupPage(ctx, view);
            auto *table = SetupTable(ctx, view, "##interfaceobjects-settings");
            auto *form = SetupTable(ctx, view, "##interfaceobjects-info");
            IM_CHECK(table && form);
            IM_CHECK_EQ(table->ColumnsCount, width > 1000 ? 2 : 1);
            IM_CHECK(form->OuterRect.Min.x >= table->Columns[0].WorkMinX - 1 &&
                     form->OuterRect.Max.x <= table->Columns[0].WorkMaxX + 1);
            auto *list = SetupChild(ctx, view, "chunks");
            IM_CHECK(list);
            IM_CHECK(!chunks || chunks == list->ID);
            chunks = list->ID;
            IM_CHECK_GT(list->ScrollMax.y, 0);
            IM_CHECK(list->Flags & ImGuiWindowFlags_HorizontalScrollbar);
            if (width == 450)
                IM_CHECK_GT(page->ScrollMax.x, 0);
        }
        auto *list = SetupChild(ctx, view, "chunks");
        ctx->ScrollToBottom(list->ID);
        ctx->Yield(2);
        IM_CHECK_GT(list->Scroll.y, 0);
        CheckSetupRename(ctx, view, objects.interfaceObjects);
        const auto after = details(objects.interfaceObjects);
        IM_CHECK_EQ(after.Values().Text("guid"), originalData.Values().Text("guid"));
        IM_CHECK(after.Values().Integers("sizes") == objects.chunkSizes);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(EditorCommands()
                                .Command("object.open-setup", {ObjectArgument("target", objects.emptyInterface)})
                                .status <= EDITOR_NO_CHANGE);
        });
        ResizeSetup(ctx, view, 690);
        const auto empty = details(objects.emptyInterface);
        IM_CHECK_EQ(empty.Values().Integer("count"), 0);
        IM_CHECK(empty.Values().Integers("sizes").empty() && SetupChild(ctx, view, "chunks"));
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *generic =
        IM_REGISTER_TEST(engine, "appearancevaluedialogs", "generic_object_reflow_description_rename_cancel_and_undo");
    generic->GuiFunc = MainThreadGui;
    generic->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        ObjectRef target;
        EditorView view = 0;
        EditorConfiguration original;
        OnMainThread(ctx, [&] {
            auto &doc = *App().State().document;
            auto *object =
                doc.Context()->CreateObject(CKCID_BEOBJECT, ("Generic object " + std::string(260, 'G')).c_str());
            IM_CHECK_NO_RET(object);
            doc.Include(object->GetID());
            target = doc.Reference(object->GetID());
            IM_CHECK_NO_RET(EditorCommands().Command("object.open-setup", {ObjectArgument("target", target)}).status <=
                            EDITOR_NO_CHANGE);
            auto &workspace = App().WorkspaceForTests();
            view = workspace.Role("setupother");
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(view && workspace.Configure(config, error));
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        const auto revision = App().State().document->ChangeRevision();
        float wideHeight = 0;
        ImGuiID formId = 0;
        for (const float width : {1500.f, 690.f, 450.f, 1500.f}) {
            ResizeSetup(ctx, view, width);
            ShowSetupTable(ctx, view, "##object-info");
            const auto *columns = SetupTable(ctx, view, "##object-info");
            const auto *form = SetupTable(ctx, view, "##object-details");
            IM_CHECK(columns && form && columns->ColumnsCount == 1);
            IM_CHECK(!formId || formId == form->ID);
            formId = form->ID;
            IM_CHECK(form->OuterRect.Min.x >= columns->Columns[0].WorkMinX - 1 &&
                     form->OuterRect.Max.x <= columns->Columns[0].WorkMaxX + 1);
            if (!wideHeight)
                wideHeight = form->OuterRect.GetHeight();
            if (width == 690)
                IM_CHECK_GT(form->OuterRect.GetHeight(), wideHeight);
            if (width == 450)
                IM_CHECK_GT(SetupPage(ctx, view)->ScrollMax.x, 0);
        }
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        IM_CHECK_EQ(App().State().document->ChangeRevision(), revision);
        CheckSetupRename(ctx, view, target);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *scalarDialog =
        IM_REGISTER_TEST(engine, "appearancevaluedialogs", "parameter_value_dialog_resize_keeps_input_cancel_and_undo");
    scalarDialog->GuiFunc = MainThreadGui;
    scalarDialog->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto objects = MakeSetupParameters(ctx);
        EditorConfiguration original;
        OnMainThread(ctx, [&] {
            App()
                .State()
                .document->Resolve(objects.local)
                ->SetName(("Dialog integer " + std::string(130, 'I')).c_str());
            auto &workspace = App().WorkspaceForTests();
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(config, error));
        });
        const auto history = App().State().History().UndoCount();
        const char *ref = "//Edit Parameter###AttributeManagerValue";
        const auto open = [&] {
            OnMainThread(ctx, [&] {
                IM_CHECK_NO_RET(
                    EditorCommands().Command("parameter.edit", {ObjectArgument("target", objects.local)}).status <=
                    EDITOR_NO_CHANGE);
            });
            ctx->Yield(4);
            ctx->SetRef(ref);
        };
        open();
        ctx->ItemClick("**/##number");
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
        ctx->KeyChars("41");
        ctx->Yield(2);
        const auto input = ctx->ItemInfo("**/##number").ID;
        const auto popup = ctx->UiContext->OpenPopupStack.back().PopupId;
        for (const float width : {1200.f, 430.f, 760.f, 430.f}) {
            ResizeParameterDialog(ctx, ref, {width, 270});
            CheckParameterDialogBounds(ctx, ref);
            IM_CHECK_EQ(ctx->UiContext->ActiveId, input);
            IM_CHECK_EQ(ctx->UiContext->OpenPopupStack.back().PopupId, popup);
            IM_CHECK_EQ(ctx->ItemInfo("**/##number").ID, input);
            IM_CHECK_GT(ParameterDialogValues(ctx, ref)->ScrollMax.x, 0);
            IM_CHECK_EQ(SetupInteger(ctx, objects.local), 7);
        }
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
        IM_CHECK(!ctx->UiContext->OpenPopupStack.empty());
        ctx->ItemClick("**/Cancel/##dialogbutton");
        ctx->Yield(3);
        IM_CHECK_EQ(SetupInteger(ctx, objects.local), 7);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        open();
        ctx->ItemClick("**/##number");
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
        ctx->KeyChars("52");
        ResizeParameterDialog(ctx, ref, {430, 270});
        CheckParameterDialogBounds(ctx, ref);
        ctx->ItemClick("**/OK/##dialogbutton");
        ctx->Yield(3);
        IM_CHECK_EQ(SetupInteger(ctx, objects.local), 52);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        OnMainThread(ctx, [&] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        IM_CHECK_EQ(SetupInteger(ctx, objects.local), 7);
        OnMainThread(ctx, [&] { IM_CHECK_NO_RET(App().State().History().Redo()); });
        IM_CHECK_EQ(SetupInteger(ctx, objects.local), 52);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(App().State().History().Undo());
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *matrixDialog =
        IM_REGISTER_TEST(engine, "appearancevaluedialogs", "parameter_matrix_dialog_scroll_resize_cancel_and_undo");
    matrixDialog->GuiFunc = MainThreadGui;
    matrixDialog->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        ObjectRef target;
        EditorConfiguration original;
        OnMainThread(ctx, [&] {
            auto &doc = *App().State().document;
            auto *parameter = doc.Context()->CreateCKParameterLocal(("Dialog matrix " + std::string(130, 'M')).c_str(),
                                                                    CKPGUID_MATRIX);
            IM_CHECK_NO_RET(parameter);
            VxMatrix matrix;
            matrix.SetIdentity();
            parameter->SetValue(&matrix, sizeof(matrix));
            doc.Include(parameter->GetID());
            target = doc.Reference(parameter->GetID());
            auto &workspace = App().WorkspaceForTests();
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(config, error));
        });
        const auto history = App().State().History().UndoCount();
        const char *ref = "//Edit Parameter###AttributeManagerValue";
        const auto first = [&] {
            return Read<float>(ctx, [&] {
                VxMatrix matrix;
                CKParameter::Cast(App().State().document->Resolve(target))->GetValue(&matrix);
                return matrix[0][0];
            });
        };
        const auto open = [&] {
            OnMainThread(ctx, [&] {
                IM_CHECK_NO_RET(EditorCommands().Command("parameter.edit", {ObjectArgument("target", target)}).status <=
                                EDITOR_NO_CHANGE);
            });
            ctx->Yield(4);
            ctx->SetRef(ref);
        };
        open();
        ctx->ItemClick("**/$$0/##m");
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
        ctx->KeyChars("3.5");
        ResizeParameterDialog(ctx, ref, {430, 150});
        CheckParameterDialogBounds(ctx, ref);
        auto *values = ParameterDialogValues(ctx, ref);
        IM_CHECK_GT(values->ScrollMax.x, 0);
        IM_CHECK_GT(values->ScrollMax.y, 0);
        IM_CHECK_EQ(first(), 1.f);
        ctx->ItemClick("**/Cancel/##dialogbutton");
        ctx->Yield(3);
        IM_CHECK_EQ(first(), 1.f);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        open();
        ctx->ItemClick("**/$$0/##m");
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
        ctx->KeyChars("4.5");
        ResizeParameterDialog(ctx, ref, {430, 250});
        ctx->ItemClick("**/OK/##dialogbutton");
        ctx->Yield(3);
        IM_CHECK_EQ(first(), 4.5f);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(App().State().History().Undo());
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
        IM_CHECK_EQ(first(), 1.f);
    };
    auto *attributeDialog =
        IM_REGISTER_TEST(engine, "appearancevaluedialogs", "attribute_value_dialog_type_layout_resize_cancel_and_undo");
    attributeDialog->GuiFunc = MainThreadGui;
    attributeDialog->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        ObjectRef owner, parameter;
        int type = -1;
        EditorConfiguration original;
        OnMainThread(ctx, [&] {
            auto &doc = *App().State().document;
            auto *object = CKBeObject::Cast(doc.Context()->CreateObject(CKCID_3DOBJECT, "Dialog attribute owner"));
            IM_CHECK_NO_RET(object);
            doc.Include(object->GetID());
            type = doc.Context()->GetAttributeManager()->RegisterNewAttributeType(
                "Dialog appearance attribute", CKPGUID_INT, CKCID_BEOBJECT, CK_ATTRIBUT_USER);
            IM_CHECK_NO_RET(type >= 0 && object->SetAttribute(type));
            auto *value = object->GetAttributeParameter(type);
            const int initial = 3;
            value->SetValue(&initial);
            doc.Include(value->GetID());
            owner = doc.Reference(object->GetID());
            parameter = doc.Reference(value->GetID());
            auto &workspace = App().WorkspaceForTests();
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(config, error));
        });
        const auto history = App().State().History().UndoCount();
        const char *ref = "//Edit Parameter###AttributeValue";
        const auto open = [&] {
            OnMainThread(ctx, [&] {
                const auto client = EditorCommands();
                const auto catalog = EditorClient::Integer(client.Query("attribute.catalog"), "catalog");
                IM_CHECK_NO_RET(
                    client
                        .Command("attribute.edit-value",
                                 {ObjectArgument("target", owner), IntegerArgument("type", type),
                                  IntegerArgument("catalog", catalog), ObjectArgument("parameter", parameter)})
                        .status <= EDITOR_NO_CHANGE);
            });
            ctx->Yield(4);
            ctx->SetRef(ref);
        };
        open();
        ResizeParameterDialog(ctx, ref, {430, 280});
        CheckParameterDialogBounds(ctx, ref);
        auto *values = ParameterDialogValues(ctx, ref);
        auto *form = ctx->UiContext->Tables.GetByKey(ctx->GetWindowByRef(ref)->GetID("##parameter-type"));
        IM_CHECK(form && form->OuterRect.Min.x >= values->InnerRect.Min.x - 1 &&
                 form->OuterRect.Max.x <= values->InnerRect.Max.x + 1);
        ctx->ItemClick("**/##number");
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
        ctx->KeyChars("17");
        ctx->ItemClick("**/Cancel/##dialogbutton");
        ctx->Yield(3);
        IM_CHECK_EQ(SetupInteger(ctx, parameter), 3);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        open();
        ctx->ItemClick("**/##number");
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
        ctx->KeyChars("21");
        ResizeParameterDialog(ctx, ref, {600, 350});
        ctx->ItemClick("**/OK/##dialogbutton");
        ctx->Yield(3);
        IM_CHECK_EQ(SetupInteger(ctx, parameter), 21);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(App().State().History().Undo());
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
        IM_CHECK_EQ(SetupInteger(ctx, parameter), 3);
    };
    auto *schematicLocal = IM_REGISTER_TEST(
        engine, "appearanceschematicdialogs", "schematic_parameter_resize_keeps_name_value_cancel_and_undo");
    schematicLocal->GuiFunc = MainThreadGui;
    schematicLocal->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto objects = MakeSchematicDialogs(ctx);
        const auto history = App().State().History().UndoCount();
        const auto revision = App().State().document->ChangeRevision();
        const bool redo = App().State().History().CanRedo();
        const char *ref = "//###EditParameter";
        const auto open = [&] {
            OnMainThread(ctx, [&] { App().Canvas().EditElement(objects.script.object, objects.element); });
            ctx->Yield(4);
            ctx->SetRef(ref);
        };
        const auto input = [&](const char *id, const char *text) {
            ctx->ItemClick(id);
            ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
            ctx->KeyChars(text);
            ctx->Yield(2);
        };
        open();
        input("##name", "Cancelled Schematic name");
        const auto nameId = ctx->ItemInfo("##name").ID;
        IM_CHECK(nameId != 0);
        const auto popup = ctx->UiContext->OpenPopupStack.back().PopupId;
        for (const float width : {1400.f, 520.f, 880.f, 430.f}) {
            ResizeParameterDialog(ctx, ref, {width, 310});
            CheckParameterDialogBounds(ctx, ref);
            IM_CHECK_EQ(ctx->UiContext->ActiveId, nameId);
            IM_CHECK_EQ(ctx->ItemInfo("##name").ID, nameId);
            IM_CHECK_EQ(ctx->UiContext->OpenPopupStack.back().PopupId, popup);
            auto *window = ctx->GetWindowByRef(ref);
            const auto *form = ctx->UiContext->Tables.GetByKey(window->GetID("##parameter-info"));
            IM_CHECK(form && form->ColumnsCount == 2);
            IM_CHECK(form->OuterRect.GetWidth() <= ParameterDialogValues(ctx, ref)->InnerRect.GetWidth() + 1);
            IM_CHECK_EQ(SetupInteger(ctx, objects.local), 7);
            IM_CHECK(ObjectName(ctx, objects.local.object) == objects.name);
        }
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
        IM_CHECK(ctx->GetWindowByRef(ref)->WasActive);
        ctx->ItemClick("Cancel/##dialogbutton");
        ctx->Yield(3);
        IM_CHECK(ObjectName(ctx, objects.local.object) == objects.name);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        IM_CHECK_EQ(App().State().document->ChangeRevision(), revision);
        IM_CHECK(App().State().History().CanRedo() == redo);
        open();
        input("##name", "Accepted Schematic name");
        input("**/##number", "42");
        const auto numberId = ctx->ItemInfo("**/##number").ID;
        ResizeParameterDialog(ctx, ref, {430, 260});
        CheckParameterDialogBounds(ctx, ref);
        IM_CHECK_EQ(ctx->UiContext->ActiveId, numberId);
        ctx->ItemClick("OK/##dialogbutton");
        ctx->Yield(4);
        IM_CHECK_EQ(SetupInteger(ctx, objects.local), 42);
        IM_CHECK(ObjectName(ctx, objects.local.object) == "Accepted Schematic name");
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        OnMainThread(ctx, [&] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        IM_CHECK_EQ(SetupInteger(ctx, objects.local), 7);
        IM_CHECK(ObjectName(ctx, objects.local.object) == objects.name);
        OnMainThread(ctx, [&] { IM_CHECK_NO_RET(App().State().History().Redo()); });
        IM_CHECK_EQ(SetupInteger(ctx, objects.local), 42);
        OnMainThread(ctx, [&] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        CloseSchematicDialogs(ctx, objects);
    };
    auto *schematicType = IM_REGISTER_TEST(
        engine, "appearanceschematicdialogs", "schematic_parameter_type_and_expanded_value_resize_cancel_and_undo");
    schematicType->GuiFunc = MainThreadGui;
    schematicType->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto objects = MakeSchematicDialogs(ctx);
        const auto history = App().State().History().UndoCount();
        const char *ref = "//###EditParameter";
        const auto open = [&] {
            OnMainThread(ctx, [&] { App().Canvas().EditElement(objects.script.object, objects.element); });
            ctx->Yield(4);
            ctx->SetRef(ref);
            ctx->ItemClick("##type/##combo");
            ctx->Yield(2);
            ctx->KeyChars("String");
            ctx->Yield(3);
            ctx->KeyPress(ImGuiKey_Enter);
            ctx->Yield(3);
            ctx->ItemClick("**/##expand");
            ctx->Yield(3);
            IM_CHECK_NO_RET(ctx->ItemInfo("**/##string").RectFull.GetHeight() > 100);
        };
        const auto type = [&] {
            return Read<CKGUID>(ctx, [&] {
                return CKParameter::Cast(App().State().document->Resolve(objects.local))->GetGUID();
            });
        };
        open();
        ctx->ItemClick("**/##string");
        ctx->KeyChars("Cancelled expanded string");
        const auto input = ctx->ItemInfo("**/##string").ID;
        IM_CHECK(input != 0);
        for (const float width : {1300.f, 430.f, 850.f, 430.f}) {
            ResizeParameterDialog(ctx, ref, {width, 200});
            CheckParameterDialogBounds(ctx, ref);
            IM_CHECK_EQ(ctx->UiContext->ActiveId, input);
            IM_CHECK_GT(ParameterDialogValues(ctx, ref)->ScrollMax.x, 0);
            IM_CHECK_GT(ParameterDialogValues(ctx, ref)->ScrollMax.y, 0);
            IM_CHECK(type() == CKPGUID_INT);
        }
        ctx->ItemClick("Cancel/##dialogbutton");
        ctx->Yield(3);
        IM_CHECK(type() == CKPGUID_INT && SetupInteger(ctx, objects.local) == 7);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        open();
        ctx->ItemClick("**/##string");
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
        ctx->KeyChars("Accepted expanded string");
        ResizeParameterDialog(ctx, ref, {520, 280});
        ctx->ItemClick("OK/##dialogbutton");
        ctx->Yield(4);
        IM_CHECK(type() == CKPGUID_STRING);
        IM_CHECK(Read<std::string>(ctx, [&] {
            return ParameterText(CKParameter::Cast(App().State().document->Resolve(objects.local)));
        }) == "Accepted expanded string");
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        OnMainThread(ctx, [&] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        IM_CHECK(type() == CKPGUID_INT && SetupInteger(ctx, objects.local) == 7);
        CloseSchematicDialogs(ctx, objects);
    };
    auto *schematicInputs = IM_REGISTER_TEST(
        engine, "appearanceschematicdialogs", "schematic_inputs_resize_scroll_apply_cancel_and_single_undo");
    schematicInputs->GuiFunc = MainThreadGui;
    schematicInputs->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto objects = MakeSchematicDialogs(ctx, true);
        const auto history = App().State().History().UndoCount();
        const char *ref = "//###EditParameters";
        const auto open = [&] {
            OnMainThread(ctx, [&] {
                App().Canvas().Navigation().selection.Set(objects.script.object,
                                                          {ElementKind::Behavior, 0, objects.block.object});
                App().Canvas().OpenParametersDialog();
                const auto projection = EditorCommands().Query(
                    "schematic.parameter-editor",
                    {ObjectArgument("target", objects.script), ObjectArgument("object", objects.block),
                     IntegerArgument("mode", 1)});
                const auto *rows = EditorClient::Find(projection, "rows");
                IM_CHECK_NO_RET(rows && rows->fields.size() == 6);
            });
            ctx->Yield(4);
            ctx->SetRef(ref);
        };
        const auto number = [&](ObjectRef target) {
            return Read<float>(ctx, [&] {
                float value = 0;
                CKParameter::Cast(App().State().document->Resolve(target))->GetValue(&value);
                return value;
            });
        };
        const auto input = [&](const char *id, const char *text) {
            ctx->ItemClick(id);
            ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
            ctx->KeyChars(text);
            ctx->Yield(2);
        };
        open();
        input("**/$$0/##number", "3.5");
        const auto active = ctx->UiContext->ActiveId;
        IM_CHECK(active != 0);
        ResizeParameterDialog(ctx, ref, {520, 240});
        CheckParameterDialogBounds(ctx, ref);
        IM_CHECK_EQ(ctx->UiContext->ActiveId, active);
        IM_CHECK_GT(ParameterDialogValues(ctx, ref)->ScrollMax.x, 0);
        IM_CHECK_GT(ParameterDialogValues(ctx, ref)->ScrollMax.y, 0);
        IM_CHECK_EQ(number(objects.first), 2.f);
        const auto title = ctx->GetWindowByRef(ref)->Pos;
        ctx->MouseMoveToPos({title.x + 10, title.y + 10});
        ctx->MouseClick(0);
        ctx->Yield(2);
        ctx->ScrollTo(ParameterDialogValues(ctx, ref)->ID, ImGuiAxis_X,
                      ParameterDialogValues(ctx, ref)->ScrollMax.x);
        ctx->ScrollToBottom(ParameterDialogValues(ctx, ref)->ID);
        ctx->Yield(3);
        input("**/$$5/##number", "6.5");
        ctx->ItemClick("Apply/##dialogbutton");
        ctx->Yield(5);
        IM_CHECK(ctx->GetWindowByRef(ref)->WasActive);
        IM_CHECK_EQ(number(objects.first), 3.5f);
        IM_CHECK_EQ(number(objects.last), 6.5f);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        ctx->ScrollToTop(ParameterDialogValues(ctx, ref)->ID);
        ctx->Yield(3);
        input("**/$$0/##number", "9.5");
        ResizeParameterDialog(ctx, ref, {1100, 350});
        CheckParameterDialogBounds(ctx, ref);
        ctx->ItemClick("Cancel/##dialogbutton");
        ctx->Yield(3);
        IM_CHECK_EQ(number(objects.first), 3.5f);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        OnMainThread(ctx, [&] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        IM_CHECK_EQ(number(objects.first), 2.f);
        IM_CHECK_EQ(number(objects.last), 2.f);
        OnMainThread(ctx, [&] { IM_CHECK_NO_RET(App().State().History().Redo()); });
        IM_CHECK_EQ(number(objects.first), 3.5f);
        IM_CHECK_EQ(number(objects.last), 6.5f);
        OnMainThread(ctx, [&] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        open();
        ctx->ItemClick("OK/##dialogbutton");
        ctx->Yield(4);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        IM_CHECK(App().State().History().CanRedo());
        CloseSchematicDialogs(ctx, objects);
    };
    auto *schematicObject = IM_REGISTER_TEST(
        engine, "workspacelayout", "schematic_object_filter_reflow_cancel_and_undo");
    schematicObject->GuiFunc = MainThreadGui;
    schematicObject->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto objects = MakeSchematicDialogs(ctx, false, CKPGUID_3DENTITY);
        ObjectRef target;
        OnMainThread(ctx, [&] {
            auto &doc = *App().State().document;
            const auto made = CreateLayoutObject(doc, LayoutObject::Frame3D);
            IM_CHECK_NO_RET(made.ok); target = doc.Reference(made.created);
            IM_CHECK_NO_RET(RenameLevelObject(doc, target, "Schematic object choice").ok);
        });
        const auto history = App().State().History().UndoCount();
        const char *ref = "//###EditParameter";
        const auto open = [&] {
            OnMainThread(ctx, [&] { App().Canvas().EditElement(objects.script.object, objects.element); });
            ctx->Yield(4); ctx->SetRef(ref);
            ShowScrollableItem(ctx, ref, "**/##expand"); ctx->ItemClick("**/##expand"); ctx->Yield(3);
        };
        const auto choose = [&] {
            ShowScrollableItem(ctx, ref, "**/##object/##combo"); ctx->ItemClick("**/##object/##combo");
            ctx->SetRef("//$FOCUSED"); ctx->ItemClick("**/Schematic object choice"); ctx->SetRef(ref); ctx->Yield(3);
        };
        open(); choose();
        ShowScrollableItem(ctx, ref, "**/##filter"); ctx->ItemClick("**/##filter"); ctx->KeyChars("choice");
        const auto active = ctx->UiContext->ActiveId;
        ResizeParameterDialog(ctx, ref, {430, 300}); CheckParameterDialogBounds(ctx, ref);
        IM_CHECK_EQ(ctx->UiContext->ActiveId, active);
        IM_CHECK_EQ(SetupInteger(ctx, objects.local), 0);
        ctx->ItemClick("Cancel/##dialogbutton"); ctx->Yield(4);
        IM_CHECK_EQ(SetupInteger(ctx, objects.local), 0);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        open(); choose();
        ResizeParameterDialog(ctx, ref, {1000, 650});
        ctx->ItemClick("OK/##dialogbutton"); ctx->Yield(4);
        IM_CHECK_EQ(SetupInteger(ctx, objects.local), target.object);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        OnMainThread(ctx, [&] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        IM_CHECK_EQ(SetupInteger(ctx, objects.local), 0);
        OnMainThread(ctx, [&] { IM_CHECK_NO_RET(App().State().History().Redo()); });
        IM_CHECK_EQ(SetupInteger(ctx, objects.local), target.object);
        OnMainThread(ctx, [&] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        CloseSchematicDialogs(ctx, objects);
    };
    auto *schematicSettings = IM_REGISTER_TEST(
        engine, "appearanceschematicdialogs", "building_block_settings_resize_cancel_callback_and_single_undo");
    schematicSettings->GuiFunc = MainThreadGui;
    schematicSettings->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto objects = MakeSchematicDialogs(ctx);
        const auto history = App().State().History().UndoCount();
        const auto revision = App().State().document->ChangeRevision();
        const char *ref = "//###Settings";
        const auto count = [&] {
            return Read<int>(ctx, [&] {
                return CKBehavior::Cast(App().State().document->Resolve(objects.block))->GetInputParameterCount();
            });
        };
        const auto open = [&] {
            OnMainThread(ctx, [&] {
                App().Canvas().EditElement(objects.script.object, {ElementKind::Behavior, 0, objects.block.object});
            });
            ctx->Yield(4);
            ctx->SetRef(ref);
        };
        IM_CHECK_EQ(count(), 3);
        open();
        ctx->ItemClick("**/##bool");
        ResizeParameterDialog(ctx, ref, {430, 200});
        CheckParameterDialogBounds(ctx, ref);
        IM_CHECK_GT(ParameterDialogValues(ctx, ref)->ScrollMax.x, 0);
        IM_CHECK_EQ(SetupInteger(ctx, objects.setting), 0);
        IM_CHECK_EQ(count(), 3);
        ctx->ItemClick("Cancel/##dialogbutton");
        ctx->Yield(3);
        IM_CHECK_EQ(count(), 3);
        IM_CHECK_EQ(SetupInteger(ctx, objects.setting), 0);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        IM_CHECK_EQ(App().State().document->ChangeRevision(), revision);
        open();
        ctx->ItemClick("**/##bool");
        ResizeParameterDialog(ctx, ref, {700, 250});
        ctx->ItemClick("OK/##dialogbutton");
        ctx->Yield(4);
        IM_CHECK_EQ(count(), 6);
        IM_CHECK_EQ(SetupInteger(ctx, objects.setting), 1);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        OnMainThread(ctx, [&] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        IM_CHECK_EQ(count(), 3);
        IM_CHECK_EQ(SetupInteger(ctx, objects.setting), 0);
        OnMainThread(ctx, [&] { IM_CHECK_NO_RET(App().State().History().Redo()); });
        IM_CHECK_EQ(count(), 6);
        IM_CHECK_EQ(SetupInteger(ctx, objects.setting), 1);
        OnMainThread(ctx, [&] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        CloseSchematicDialogs(ctx, objects);
    };
    auto *arrayChoice = IM_REGISTER_TEST(
        engine, "appearancesetupdialogs", "object_array_chooser_resize_filter_cancel_and_single_undo");
    arrayChoice->GuiFunc = MainThreadGui;
    arrayChoice->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto original = SetupDialogTheme(ctx);
        ObjectRef parameter, first, second;
        std::string className;
        OnMainThread(ctx, [&] {
            auto &doc = *App().State().document;
            auto *context = doc.Context();
            auto *value = context->CreateCKParameterLocal("Array chooser dialog", CKPGUID_OBJECTARRAY);
            auto *a = context->CreateObject(CKCID_3DENTITY, "Array choice A");
            auto *b = context->CreateObject(CKCID_3DENTITY, "Array choice B");
            IM_CHECK_NO_RET(value && a && b);
            for (auto *object : {static_cast<CKObject *>(value), a, b}) doc.Include(object->GetID());
            XObjectArray objects;
            objects.PushBack(a->GetID());
            IM_CHECK_NO_RET(SetHeldValue(value, &objects));
            parameter = doc.Reference(value->GetID());
            first = doc.Reference(a->GetID());
            second = doc.Reference(b->GetID());
            const auto classes = EditorCommands().Query("parameter.object-classes");
            const auto *rows = EditorClient::Find(classes, "rows");
            IM_CHECK_NO_RET(rows);
            for (const auto &row : rows->fields)
                if (SetupFields(row.fields).Integer("id") == CKCID_3DENTITY)
                    className = SetupFields(row.fields).Text("name");
            IM_CHECK_NO_RET(!className.empty());
        });
        const auto history = App().State().History().UndoCount();
        const auto value = [&] {
            return Read<std::vector<ObjectRef>>(ctx, [&] {
                ParameterValueDescription description;
                IM_CHECK_NO_RET(DescribeParameterValue(*App().State().document,
                    CKParameter::Cast(App().State().document->Resolve(parameter)), description));
                return description.value.objects;
            });
        };
        const char *outer = "//Edit Parameter###AttributeManagerValue", *chooser = "//Edit Parameter###ChooseObject";
        const auto open = [&] {
            OnMainThread(ctx, [&] {
                IM_CHECK_NO_RET(EditorCommands().Command("parameter.edit", {ObjectArgument("target", parameter)}).status <=
                                EDITOR_NO_CHANGE);
            });
            ctx->Yield(4);
        };
        const auto choose = [&] {
            ctx->SetRef(outer);
            ctx->ItemClick("**/##rest", ImGuiMouseButton_Right);
            ctx->Yield(3);
            ctx->SetRef("//$FOCUSED");
            ctx->ItemClick("**/Add Object");
            ctx->Yield(3);
            ctx->SetRef(chooser);
            ctx->ItemClick("##class/##combo");
            ctx->Yield(2);
            ctx->SetRef("//$FOCUSED");
            ctx->ItemClick(("**/" + className).c_str());
            ctx->Yield(3);
            ctx->SetRef(chooser);
            ctx->ItemClick("##derived");
            ctx->ItemClick("##filter");
            ctx->KeyChars("Array choice B");
            ctx->Yield(2);
        };
        open();
        choose();
        const auto input = ctx->UiContext->ActiveId;
        IM_CHECK(input != 0);
        const auto popup = ctx->UiContext->OpenPopupStack.back().PopupId;
        for (const float width : {1100.f, 430.f, 760.f, 430.f}) {
            ResizeParameterDialog(ctx, chooser, {width, 200});
            CheckSetupDialogBounds(ctx, chooser, "##object-choice", "OK", "Cancel", true);
            IM_CHECK_EQ(ctx->UiContext->ActiveId, input);
            IM_CHECK_EQ(ctx->UiContext->OpenPopupStack.back().PopupId, popup);
            IM_CHECK_GT(SetupDialogContent(ctx, chooser, "##object-choice")->ScrollMax.y, 0);
            IM_CHECK(value() == std::vector<ObjectRef>{first});
        }
        ctx->ItemClick("Cancel/##dialogbutton");
        ctx->Yield(3);
        IM_CHECK(ctx->GetWindowByRef(outer)->WasActive && value() == std::vector<ObjectRef>{first});
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        const auto add = [&] {
            choose();
            ctx->ScrollToTop(SetupDialogContent(ctx, chooser, "##object-choice")->ID);
            ctx->Yield(3);
            ctx->ItemClick("##object/##combo");
            ctx->Yield(2);
            ctx->KeyChars("Array choice B");
            ctx->Yield(3);
            ctx->KeyPress(ImGuiKey_Enter);
            ctx->Yield(3);
            ctx->SetRef(chooser);
            ctx->ItemClick("OK/##dialogbutton");
            ctx->Yield(3);
            ctx->SetRef(outer);
        };
        add();
        IM_CHECK(value() == std::vector<ObjectRef>{first});
        ctx->ItemClick("Cancel/##dialogbutton");
        ctx->Yield(3);
        IM_CHECK(value() == std::vector<ObjectRef>{first});
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        open();
        add();
        ctx->ItemClick("OK/##dialogbutton");
        ctx->Yield(4);
        const std::vector<ObjectRef> expected{first, second};
        IM_CHECK(value() == expected);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        OnMainThread(ctx, [&] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        IM_CHECK(value() == std::vector<ObjectRef>{first});
        OnMainThread(ctx, [&] { IM_CHECK_NO_RET(App().State().History().Redo()); });
        IM_CHECK(value() == expected);
        OnMainThread(ctx, [&] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        FinishSetupDialogs(ctx, original);
    };
    auto *slotDialogs = IM_REGISTER_TEST(
        engine, "appearancesetupdialogs", "slot_resize_and_texture_sprite_remove_cancel_and_single_undo");
    slotDialogs->GuiFunc = MainThreadGui;
    slotDialogs->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto original = SetupDialogTheme(ctx);
        for (const bool sprite : {false, true}) {
            const auto target = Read<ObjectRef>(ctx, [&] {
                auto &doc = *App().State().document;
                auto *object = doc.Context()->CreateObject(sprite ? CKCID_SPRITE : CKCID_TEXTURE,
                                                            sprite ? "Slot dialog sprite" : "Slot dialog texture");
                IM_CHECK_NO_RET(object);
                doc.Include(object->GetID());
                const auto prepare = [&](auto *image) {
                    IM_CHECK_NO_RET(image->Create(8, 8, 32, 0) && image->Create(8, 8, 32, 1));
                    for (int slot = 0; slot < 2; ++slot) {
                        image->SetCurrentSlot(slot);
                        for (int y = 0; y < 8; ++y)
                            for (int x = 0; x < 8; ++x) image->SetPixel(x, y, slot ? 0x7f123456 : 0xffabcdef);
                    }
                    image->SetCurrentSlot(0);
                };
                if (sprite) prepare(CKSprite::Cast(object));
                else prepare(CKTexture::Cast(object));
                return doc.Reference(object->GetID());
            });
            const auto view = OpenSetupDialogPage(ctx, target, sprite ? "setupsprite2d" : "setuptexture");
            const auto pixelsBefore = Read<std::vector<uint32_t>>(ctx, [&] {
                auto *object = App().State().document->Resolve(target);
                const auto read = [](auto *image) {
                    return std::vector<uint32_t>{image->GetPixel(0, 0, 0), image->GetPixel(0, 0, 1)};
                };
                return sprite ? read(CKSprite::Cast(object)) : read(CKTexture::Cast(object));
            });
            const auto details = [&] {
                return Read<SetupDetails>(ctx, [&] {
                    SetupDetails data;
                    IM_CHECK_NO_RET(ReadSetupDetails(EditorCommands(), target, "bitmap", data));
                    return data;
                });
            };
            const auto history = App().State().History().UndoCount();
            if (!sprite) {
                const auto open = [&] {
                    ctx->SetRef(SetupRef(view).c_str());
                    ShowSetupTable(ctx, view, "##texture-previews");
                    ctx->ItemClick(ImHashStr("##preview", 0, SetupPage(ctx, view)->GetID("image")), ImGuiMouseButton_Right);
                    ctx->Yield(3);
                    ctx->SetRef("//$FOCUSED");
                    ctx->ItemClick("**/Resize Slot...");
                    ctx->Yield(3);
                    ctx->SetRef("//Resize Slot");
                };
                open();
                const int index = 0;
                const auto input = ImHashData(&index, sizeof(index), ctx->GetWindowByRef("//Resize Slot")->GetID("Size"));
                ctx->ItemClick(input);
                ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
                ctx->KeyChars("16");
                ctx->Yield(2);
                IM_CHECK(ctx->UiContext->ActiveId != 0);
                for (const float width : {950.f, 430.f, 760.f, 430.f}) {
                    ResizeParameterDialog(ctx, "//Resize Slot", {width, 240});
                    CheckSetupDialogBounds(ctx, "//Resize Slot", "##slot-size");
                    IM_CHECK_EQ(ctx->UiContext->ActiveId, input);
                    IM_CHECK_EQ(details().Values().Integer("width"), 8);
                }
                ctx->ItemClick("Cancel");
                ctx->Yield(3);
                IM_CHECK_EQ(details().Values().Integer("width"), 8);
                IM_CHECK_EQ(App().State().History().UndoCount(), history);
                open();
                ctx->ItemInputValue(input, 16);
                ResizeParameterDialog(ctx, "//Resize Slot", {430, 240});
                ctx->ItemClick("OK");
                ctx->Yield(4);
                IM_CHECK_EQ(details().Values().Integer("width"), 16);
                IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
                OnMainThread(ctx, [&] { IM_CHECK_NO_RET(App().State().History().Undo()); });
                IM_CHECK_EQ(details().Values().Integer("width"), 8);
            }
            const auto remove = [&] {
                ctx->SetRef(SetupRef(view).c_str());
                ShowSetupTable(ctx, view, "##bitmap-info");
                ctx->ItemClick(SetupPage(ctx, view)->GetID("Remove Current Slot"));
                ctx->Yield(3);
                ctx->SetRef("//Warning!###removeslot");
            };
            remove();
            ResizeParameterDialog(ctx, "//Warning!###removeslot", {430, 220});
            CheckSetupDialogBounds(ctx, "//Warning!###removeslot", "##slot-question", "Yes", "No");
            ctx->ItemClick("No");
            ctx->Yield(3);
            IM_CHECK_EQ(details().Values().Integer("slots"), 2);
            IM_CHECK_EQ(App().State().History().UndoCount(), history);
            remove();
            ResizeParameterDialog(ctx, "//Warning!###removeslot", {760, 240});
            ctx->ItemClick("Yes");
            ctx->Yield(4);
            IM_CHECK_EQ(details().Values().Integer("slots"), 1);
            IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
            OnMainThread(ctx, [&] { IM_CHECK_NO_RET(App().State().History().Undo()); });
            IM_CHECK_EQ(details().Values().Integer("slots"), 2);
            OnMainThread(ctx, [&] {
                auto *object = App().State().document->Resolve(target);
                const auto restored = [&](auto *image) {
                    return image->GetPixel(0, 0, 0) == pixelsBefore[0] && image->GetPixel(0, 0, 1) == pixelsBefore[1];
                };
                IM_CHECK_NO_RET(sprite ? restored(CKSprite::Cast(object)) : restored(CKTexture::Cast(object)));
                IM_CHECK_NO_RET(App().State().History().Redo());
                IM_CHECK_NO_RET(App().State().History().Undo());
            });
        }
        FinishSetupDialogs(ctx, original);
    };
    auto *gridSquares = IM_REGISTER_TEST(
        engine, "appearancesetupdialogs", "grid_square_value_resize_multilayer_cancel_and_single_undo");
    gridSquares->GuiFunc = MainThreadGui;
    gridSquares->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto original = SetupDialogTheme(ctx);
        const auto grid = MakeDialogGrid(ctx, false, 2);
        const auto data = [&] {
            return Read<SetupDetails>(ctx, [&] {
                SetupDetails result;
                ReadSetupDetails(EditorCommands(), grid.object, "grid", result);
                return result;
            });
        };
        SelectDialogGridLayers(ctx, grid);
        const auto history = App().State().History().UndoCount();
        const auto cells = [&] {
            std::vector<std::vector<uint64_t>> result;
            const auto state = data();
            for (int index : grid.indices)
                result.push_back(SetupFields(state.Values().Rows("layers")[size_t(index)].fields).Integers("squares"));
            return result;
        };
        const auto initial = cells();
        OpenSquareDialog(ctx, grid);
        ctx->ItemClick("Value");
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
        ctx->KeyChars("77");
        const auto active = ctx->UiContext->ActiveId;
        IM_CHECK(active != 0);
        for (const float width : {1100.f, 430.f, 760.f, 430.f}) {
            ResizeParameterDialog(ctx, "//Square Value", {width, 240});
            CheckSetupDialogBounds(ctx, "//Square Value", "##square-value");
            IM_CHECK_EQ(ctx->UiContext->ActiveId, active);
            IM_CHECK(cells() == initial);
        }
        ctx->ItemClick("Cancel");
        ctx->Yield(3);
        IM_CHECK(cells() == initial);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        OpenSquareDialog(ctx, grid);
        ctx->ItemInputValue("Value", 77);
        ResizeParameterDialog(ctx, "//Square Value", {430, 240});
        ctx->ItemClick("OK");
        ctx->Yield(4);
        const auto changed = cells();
        IM_CHECK(changed != initial && changed[0] == changed[1]);
        IM_CHECK_EQ(std::count(changed[0].begin(), changed[0].end(), 77), 1);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        OnMainThread(ctx, [&] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        IM_CHECK(cells() == initial);
        OnMainThread(ctx, [&] { IM_CHECK_NO_RET(App().State().History().Redo()); });
        IM_CHECK(cells() == changed);
        OnMainThread(ctx, [&] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        FinishSetupDialogs(ctx, original);
    };
    auto *gridRename = IM_REGISTER_TEST(
        engine, "appearancesetupdialogs", "grid_and_setup_rename_resize_cancel_and_single_undo");
    gridRename->GuiFunc = MainThreadGui;
    gridRename->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto original = SetupDialogTheme(ctx);
        const auto grid = MakeDialogGrid(ctx, false);
        const auto history = App().State().History().UndoCount();
        for (const bool classification : {false, true}) {
            const int type = classification ? grid.classification : grid.types.front();
            const auto name = [&] {
                return Read<std::string>(ctx, [&] {
                    SetupDetails state;
                    ReadSetupDetails(EditorCommands(), grid.object, "grid", state);
                    for (const auto &row : state.Values().Rows(classification ? "classifications" : "types"))
                        if (SetupFields(row.fields).Integer("type") == uint64_t(type))
                            return SetupFields(row.fields).Text("name");
                    return std::string();
                });
            };
            const auto before = name();
            IM_CHECK(!before.empty());
            const auto open = [&] {
                const auto tableName = classification ? "classifications" : "layertypes";
                ctx->SetRef(SetupRef(grid.view).c_str());
                ShowSetupTable(ctx, grid.view, tableName);
                auto *table = SetupTable(ctx, grid.view, tableName);
                const auto id = ImHashStr(before.c_str(), 0, ImHashData(&type, sizeof(type), table->ID));
                ctx->ItemClick(id, ImGuiMouseButton_Right, ImGuiTestOpFlags_MoveToEdgeL);
                ctx->Yield(3);
                IM_CHECK_NO_RET(!ctx->UiContext->OpenPopupStack.empty() &&
                    ctx->UiContext->OpenPopupStack.back().PopupId ==
                        ImHashStr(classification ? "classmenu" : "typemenu", 0,
                                  ImHashData(&type, sizeof(type), table->ID)));
                ctx->SetRef("//$FOCUSED");
                ctx->ItemClick("**/Rename");
                ctx->Yield(3);
                ctx->SetRef("//###gridrename");
            };
            open();
            ctx->ItemInput("##name");
            ctx->KeyCharsReplace("Cancelled Grid dialog name");
            const auto input = ctx->UiContext->ActiveId;
            IM_CHECK(input != 0);
            for (const float width : {1100.f, 430.f, 760.f, 430.f}) {
                ResizeParameterDialog(ctx, "//###gridrename", {width, 240});
                CheckSetupDialogBounds(ctx, "//###gridrename", "##grid-name");
                IM_CHECK_EQ(ctx->UiContext->ActiveId, input);
                IM_CHECK(name() == before);
            }
            ctx->KeyPress(ImGuiKey_Escape);
            ctx->Yield(3);
            IM_CHECK(name() == before);
            IM_CHECK_EQ(App().State().History().UndoCount(), history);
            open();
            ctx->ItemInputValue("##name", classification ? "Confirmed dialog classification" : "Confirmed dialog layer");
            ctx->Yield(4);
            IM_CHECK(name() != before);
            IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
            OnMainThread(ctx, [&] { IM_CHECK_NO_RET(App().State().History().Undo()); });
            IM_CHECK(name() == before);
            OnMainThread(ctx, [&] { IM_CHECK_NO_RET(App().State().History().Redo()); });
            IM_CHECK(name() != before);
            OnMainThread(ctx, [&] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        }
        const auto before = ObjectName(ctx, grid.object.object);
        const auto openName = [&] {
            ctx->WindowFocus(SetupRef(grid.view).c_str());
            const auto at = ctx->GetWindowByRef(SetupRef(grid.view).c_str())->Pos;
            ctx->MouseMoveToPos({at.x + 10, at.y + 10});
            ctx->KeyPress(ImGuiKey_F2);
            ctx->Yield(3);
            ctx->SetRef("//###setuprename");
        };
        openName();
        ctx->ItemInput("##name");
        ctx->KeyCharsReplace("Cancelled standalone Setup name");
        const auto input = ctx->UiContext->ActiveId;
        ResizeParameterDialog(ctx, "//###setuprename", {430, 240});
        CheckSetupDialogBounds(ctx, "//###setuprename", "##object-name");
        IM_CHECK_EQ(ctx->UiContext->ActiveId, input);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
        IM_CHECK(ObjectName(ctx, grid.object.object) == before);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        openName();
        ctx->ItemInputValue("##name", "Confirmed standalone Setup name");
        ctx->Yield(4);
        IM_CHECK(ObjectName(ctx, grid.object.object) == "Confirmed standalone Setup name");
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        OnMainThread(ctx, [&] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        IM_CHECK(ObjectName(ctx, grid.object.object) == before);
        FinishSetupDialogs(ctx, original);
    };
    auto *linkerSquare = IM_REGISTER_TEST(
        engine, "appearancesetupdialogs", "grid_linker_square_scroll_resize_cancel_and_single_undo");
    linkerSquare->GuiFunc = MainThreadGui;
    linkerSquare->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto original = SetupDialogTheme(ctx);
        const auto grid = MakeDialogGrid(ctx, true);
        SelectDialogGridLayers(ctx, grid);
        const auto state = Read<SetupDetails>(ctx, [&] {
            SetupDetails result;
            ReadSetupDetails(EditorCommands(), grid.object, "grid", result);
            return result;
        });
        std::string choice;
        int value = 0;
        for (const auto &row : state.Values().Rows("linker-values"))
            if (SetupFields(row.fields).Integer("value")) {
                choice = SetupFields(row.fields).Text("name");
                value = int(SetupFields(row.fields).Integer("value"));
            }
        IM_CHECK(!choice.empty());
        const auto cell = [&] {
            return Read<int>(ctx, [&] {
                auto *native = CKGrid::Cast(App().State().document->Resolve(grid.object));
                int result = 0;
                native->GetLayerByIndex(grid.indices.front())->GetValue2(0, 3, &result);
                return result;
            });
        };
        const auto history = App().State().History().UndoCount();
        const int before = cell();
        OpenSquareDialog(ctx, grid);
        ResizeParameterDialog(ctx, "//Square Value", {430, 200});
        CheckSetupDialogBounds(ctx, "//Square Value", "##square-value");
        IM_CHECK_GT(SetupDialogContent(ctx, "//Square Value", "##square-value")->ScrollMax.y, 0);
        ctx->ItemClick(choice.c_str());
        ctx->ItemClick("Cancel");
        ctx->Yield(3);
        IM_CHECK_EQ(cell(), before);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        OpenSquareDialog(ctx, grid);
        ctx->ItemClick(choice.c_str());
        ResizeParameterDialog(ctx, "//Square Value", {760, 300});
        ctx->ItemClick("OK");
        ctx->Yield(4);
        IM_CHECK_EQ(cell(), value);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        OnMainThread(ctx, [&] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        IM_CHECK_EQ(cell(), before);
        OnMainThread(ctx, [&] { IM_CHECK_NO_RET(App().State().History().Redo()); });
        IM_CHECK_EQ(cell(), value);
        OnMainThread(ctx, [&] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        FinishSetupDialogs(ctx, original);
    };
    auto *objectChoices = IM_REGISTER_TEST(
        engine, "appearancesetupdialogs", "setup_object_choice_lists_bound_scroll_cancel_and_single_undo");
    objectChoices->GuiFunc = MainThreadGui;
    objectChoices->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto original = SetupDialogTheme(ctx);
        for (int kind = 0; kind < 3; ++kind) {
            ObjectRef target, choice;
            std::string choiceName;
            OnMainThread(ctx, [&] {
                auto &doc = *App().State().document;
                auto *context = doc.Context();
                auto *owner = context->CreateObject(kind == 0 ? CKCID_3DOBJECT : kind == 1 ? CKCID_MESH : CKCID_CHARACTER,
                                                     "Setup choice owner");
                IM_CHECK_NO_RET(owner);
                doc.Include(owner->GetID());
                target = doc.Reference(owner->GetID());
                for (int i = 0; i < 65; ++i) {
                    const auto name = "Dialog choice " + std::to_string(kind) + " " + std::to_string(i) + std::string(250, 'Q');
                    auto *object = context->CreateObject(kind == 0 ? CKCID_MESH : kind == 1 ? CKCID_MATERIAL : CKCID_BODYPART,
                                                         name.c_str());
                    IM_CHECK_NO_RET(object);
                    doc.Include(object->GetID());
                    if (i == 0) {
                        if (kind == 0) IM_CHECK_NO_RET(CK3dObject::Cast(owner)->AddMesh(CKMesh::Cast(object)) == CK_OK);
                        else if (kind == 1) IM_CHECK_NO_RET(CKMesh::Cast(owner)->AddChannel(CKMaterial::Cast(object)) >= 0);
                        else IM_CHECK_NO_RET(CKCharacter::Cast(owner)->AddBodyPart(CKBodyPart::Cast(object)) == CK_OK);
                    }
                    if (i == 1) {
                        choice = doc.Reference(object->GetID());
                        choiceName = name;
                    }
                }
            });
            const char *role = kind == 0 ? "setup3d" : kind == 1 ? "setupmesh" : "setupcharacter";
            const char *tableName = kind == 0 ? "meshes" : kind == 1 ? "channels" : "bodyparts";
            const char *title = kind == 0 ? "Add Mesh" : kind == 1 ? "Add Channel" : "Add Body Part";
            const auto view = OpenSetupDialogPage(ctx, target, role);
            const auto count = [&] {
                return Read<int>(ctx, [&] {
                    SetupDetails state;
                    ReadSetupDetails(EditorCommands(), target, kind == 0 ? "entity3d" : kind == 1 ? "mesh" : "character", state);
                    return kind == 1 ? int(state.Values().Integer("channel-count"))
                                     : int(state.Values().Rows(kind == 0 ? "meshes" : "bodies").size());
                });
            };
            const auto before = count();
            const auto history = App().State().History().UndoCount();
            const auto open = [&] {
                ctx->SetRef(SetupRef(view).c_str());
                ImRect list;
                if (kind == 0) {
                    ShowSetupTable(ctx, view, "##entity-details");
                    list = SetupChild(ctx, view, "meshes")->InnerRect;
                } else {
                    ShowSetupTable(ctx, view, tableName);
                    list = SetupTable(ctx, view, tableName)->InnerWindow->InnerRect;
                }
                ctx->MouseMoveToPos({list.Min.x + 15, list.Max.y - 20});
                ctx->MouseClick(ImGuiMouseButton_Right);
                ctx->Yield(3);
                ctx->SetRef("//$FOCUSED");
                ctx->ItemClick(("**/" + std::string(title)).c_str());
                ctx->Yield(3);
                ctx->SetRef("//$FOCUSED");
            };
            open();
            auto *popup = ctx->UiContext->OpenPopupStack.back().Window;
            const auto *viewport = ImGui::GetMainViewport();
            IM_CHECK(popup->WasActive && popup->Pos.x >= viewport->WorkPos.x - 1 && popup->Pos.y >= viewport->WorkPos.y - 1 &&
                     popup->Pos.x + popup->Size.x <= viewport->WorkPos.x + viewport->WorkSize.x + 1 &&
                     popup->Pos.y + popup->Size.y <= viewport->WorkPos.y + viewport->WorkSize.y + 1);
            IM_CHECK_GT(popup->ScrollMax.x, 0);
            IM_CHECK_GT(popup->ScrollMax.y, 0);
            const auto id = ctx->UiContext->OpenPopupStack.back().PopupId;
            ResizeSetup(ctx, view, 690, false);
            IM_CHECK_EQ(ctx->UiContext->OpenPopupStack.back().PopupId, id);
            ctx->KeyPress(ImGuiKey_Escape);
            ctx->Yield(3);
            IM_CHECK_EQ(count(), before);
            IM_CHECK_EQ(App().State().History().UndoCount(), history);
            ResizeSetup(ctx, view, 1500);
            open();
            popup = ctx->UiContext->OpenPopupStack.back().Window;
            ctx->ScrollToTop(popup->ID);
            ctx->ScrollToX(popup->ID, 0);
            ctx->Yield(3);
            const int key = int(choice.object);
            ctx->ItemClick(ImHashStr(choiceName.c_str(), 0, ImHashData(&key, sizeof(key), popup->ID)), 0,
                           ImGuiTestOpFlags_MoveToEdgeL);
            ctx->Yield(4);
            IM_CHECK_EQ(count(), before + 1);
            IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
            OnMainThread(ctx, [&] { IM_CHECK_NO_RET(App().State().History().Undo()); });
            IM_CHECK_EQ(count(), before);
            OnMainThread(ctx, [&] { IM_CHECK_NO_RET(App().State().History().Redo()); });
            IM_CHECK_EQ(count(), before + 1);
            OnMainThread(ctx, [&] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        }
        FinishSetupDialogs(ctx, original);
    };
    auto *graphProperties =
        IM_REGISTER_TEST(engine, "appearancegraphdialogs", "rename_priority_and_delay_resize_cancel_and_single_undo");
    graphProperties->GuiFunc = MainThreadGui;
    graphProperties->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto objects = MakeGraphDialogs(ctx);
        const auto open = [&](int kind) {
            SelectGraphDialog(ctx, objects, kind == 2 ? objects.linkElement : objects.graphElement);
            if (kind == 0)
                OnMainThread(ctx, [] { App().Canvas().BeginRename(); });
            else if (kind == 1)
                OnMainThread(ctx, [&] {
                    App().Canvas().Navigation().selection.Add(objects.values.script.object, objects.otherElement);
                    App().Canvas().OpenPriorityDialog();
                });
            else
                ctx->KeyPress(ImGuiKey_E);
            ctx->Yield(4);
        };
        for (int kind = 0; kind < 3; ++kind) {
            const char *ref = kind == 0 ? "//Rename" : kind == 1 ? "//Priority" : "//Link Delay";
            const char *label = kind == 0 ? "Name" : kind == 1 ? "Priority" : "Frames";
            const auto name = ObjectName(ctx, objects.graph.object);
            const auto value = [&] {
                return Read<int>(ctx, [&] {
                    auto &doc = *App().State().document;
                    return kind == 2 ? CKBehaviorLink::Cast(doc.Resolve(objects.link))->GetInitialActivationDelay()
                                     : CKBehavior::Cast(doc.Resolve(objects.graph))->GetPriority();
                });
            };
            const int before = value();
            const auto history = App().State().History().UndoCount();
            const auto revision = App().State().document->ChangeRevision();
            open(kind);
            ctx->SetRef(ref);
            IM_CHECK(ctx->ItemExists(label));
            if (kind != 0) {
                ctx->KeyDown(ImGuiMod_Ctrl);
                ctx->ItemClick(label);
                ctx->KeyUp(ImGuiMod_Ctrl);
            }
            TypeGraphDialog(ctx, label, kind == 0 ? "Cancelled graph name" : "17");
            const auto id = ctx->ItemInfo(label).ID;
            ResizeGraphDialog(ctx, ref, "##property");
            IM_CHECK_EQ(ctx->UiContext->ActiveId, id);
            ctx->ItemClick("Cancel");
            ctx->Yield(3);
            IM_CHECK(ObjectName(ctx, objects.graph.object) == name && value() == before);
            IM_CHECK_EQ(App().State().History().UndoCount(), history);
            IM_CHECK_EQ(App().State().document->ChangeRevision(), revision);
            open(kind);
            ctx->SetRef(ref);
            IM_CHECK(ctx->ItemExists(label));
            if (kind != 0) {
                ctx->KeyDown(ImGuiMod_Ctrl);
                ctx->ItemClick(label);
                ctx->KeyUp(ImGuiMod_Ctrl);
            }
            TypeGraphDialog(ctx, label, kind == 0 ? "Accepted graph name" : "17");
            ctx->ItemClick("OK");
            ctx->Yield(4);
            IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
            if (kind == 0)
                IM_CHECK(ObjectName(ctx, objects.graph.object) == "Accepted graph name");
            else
                IM_CHECK_EQ(value(), 17);
            if (kind == 1)
                IM_CHECK_EQ(
                    Read<int>(
                        ctx,
                        [&] {
                            return CKBehavior::Cast(App().State().document->Resolve(objects.other))->GetPriority();
                        }),
                    17);
            OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Undo()); });
            IM_CHECK(ObjectName(ctx, objects.graph.object) == name && value() == before);
            OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Redo()); });
            if (kind == 0)
                IM_CHECK(ObjectName(ctx, objects.graph.object) == "Accepted graph name");
            else
                IM_CHECK_EQ(value(), 17);
            OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        }
        CloseGraphDialogs(ctx, objects);
    };
    auto *graphComment = IM_REGISTER_TEST(engine, "appearancegraphdialogs",
                                          "comment_and_script_color_resize_scroll_cancel_and_single_undo");
    graphComment->GuiFunc = MainThreadGui;
    graphComment->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto objects = MakeGraphDialogs(ctx);
        SelectGraphDialog(ctx, objects, objects.graphElement);
        OnMainThread(ctx, [&] { App().Canvas().AddComment(objects.values.script.object, {300, 600}); });
        ctx->Yield(4);
        const auto comment = App().Canvas().Navigation().selection.items.front();
        IM_CHECK(comment.kind == ElementKind::Comment);
        OnMainThread(ctx, [&] { App().Canvas().EditElement(objects.values.script.object, comment); });
        ctx->Yield(4);
        const auto text = [&] {
            return Read<std::string>(ctx, [&] {
                const auto *record =
                    FindRecord(App().State().document->Session(objects.values.script.object)->Data(), comment.owner);
                return record ? record->comments[size_t(comment.index)].text : "missing";
            });
        };
        const auto original = text();
        const auto history = App().State().History().UndoCount();
        ctx->SetRef("//Edit Comment");
        TypeGraphDialog(ctx, "##text", "Cancelled multiline comment");
        const auto id = ctx->ItemInfo("##text").ID;
        ResizeGraphDialog(ctx, "//Edit Comment", "##comment", "OK", "Cancel", false, 200);
        IM_CHECK_EQ(ctx->UiContext->ActiveId, id);
        IM_CHECK_GT(SetupDialogContent(ctx, "//Edit Comment", "##comment")->ScrollMax.y, 0);
        ctx->ItemClick("Cancel");
        ctx->Yield(3);
        IM_CHECK(text() == original && App().State().History().UndoCount() == history);
        OnMainThread(ctx, [&] { App().Canvas().EditElement(objects.values.script.object, comment); });
        ctx->Yield(3);
        TypeGraphDialog(ctx, "//Edit Comment/##text", "Accepted multiline comment");
        ctx->ItemClick("//Edit Comment/OK");
        ctx->Yield(3);
        IM_CHECK(text() == "Accepted multiline comment" && App().State().History().UndoCount() == history + 1);
        OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        IM_CHECK(text() == original);
        const auto color = [&] {
            return Read<CKDWORD>(ctx, [&] {
                return App().State().document->Session(objects.values.script.object)->Data().header.color;
            });
        };
        const auto oldColor = color();
        for (bool accept : {false, true}) {
            GraphHeaderMenu(ctx, objects, "Set Color");
            ctx->SetRef("//Script Color");
            ctx->ScrollToBottom(SetupDialogContent(ctx, "//Script Color", "##script-color")->ID);
            ctx->Yield(3);
            IM_CHECK(ctx->ItemExists("**/##color/##hex/##Text"));
            TypeGraphDialog(ctx, "**/##color/##hex/##Text", "FE10AB");
            ResizeGraphDialog(ctx, "//Script Color", "##script-color", "OK", "Cancel", false, 200);
            IM_CHECK(color() == oldColor);
            ctx->ItemClick(accept ? "OK" : "Cancel");
            ctx->Yield(4);
            IM_CHECK_EQ(App().State().History().UndoCount(), history + (accept ? 1 : 0));
            if (accept) {
                IM_CHECK(color() != oldColor);
                OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Undo()); });
                IM_CHECK(color() == oldColor);
            }
        }
        CloseGraphDialogs(ctx, objects);
    };
    auto *graphCreate = IM_REGISTER_TEST(engine, "appearancegraphdialogs",
                                         "local_and_graph_pin_picker_resize_filter_cancel_and_single_undo");
    graphCreate->GuiFunc = MainThreadGui;
    graphCreate->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto objects = MakeGraphDialogs(ctx);
        for (int kind = 0; kind < 3; ++kind) {
            const auto count = [&] {
                return Read<int>(ctx, [&] {
                    auto &doc = *App().State().document;
                    auto *behavior = CKBehavior::Cast(doc.Resolve(kind == 0 ? objects.values.script : objects.graph));
                    return kind == 0   ? behavior->GetLocalParameterCount()
                           : kind == 1 ? behavior->GetInputParameterCount()
                                       : behavior->GetOutputParameterCount();
                });
            };
            const int before = count();
            const auto history = App().State().History().UndoCount();
            const auto open = [&] {
                SelectGraphDialog(ctx, objects, objects.graphElement);
                OnMainThread(ctx, [&] {
                    if (kind == 0)
                        App().Canvas().OpenLocalParameterPicker(objects.values.script.object, {40, 100});
                    else
                        App().Canvas().OpenGraphParameterPicker(kind == 1);
                });
                ctx->Yield(4);
                ctx->SetRef("//Add Parameter");
                TypeGraphDialog(ctx, "Name", "Graph dialog created parameter");
                TypeGraphDialog(ctx, "##filter", "Integer");
                ctx->ItemClick("**/Integer");
                ctx->Yield(2);
            };
            open();
            TypeGraphDialog(ctx, "Name", "Cancelled parameter");
            const auto id = ctx->ItemInfo("Name").ID;
            ResizeGraphDialog(ctx, "//Add Parameter", "##local-parameter", "Add", "Cancel", false, 240);
            IM_CHECK_EQ(ctx->UiContext->ActiveId, id);
            ctx->ItemClick("Cancel");
            ctx->Yield(3);
            IM_CHECK(count() == before && App().State().History().UndoCount() == history);
            open();
            ResizeParameterDialog(ctx, "//Add Parameter", {760, 430});
            ctx->ItemClick("Add");
            ctx->Yield(4);
            IM_CHECK(count() == before + 1 && App().State().History().UndoCount() == history + 1);
            OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Undo()); });
            IM_CHECK_EQ(count(), before);
            OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Redo()); });
            IM_CHECK_EQ(count(), before + 1);
            OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        }
        CloseGraphDialogs(ctx, objects);
    };
    auto *graphOperation = IM_REGISTER_TEST(engine, "appearancegraphdialogs",
                                            "operation_picker_resize_keeps_signature_cancel_and_single_undo");
    graphOperation->GuiFunc = MainThreadGui;
    graphOperation->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto objects = MakeGraphDialogs(ctx);
        const auto count = [&] {
            return Read<int>(ctx, [&] {
                return CKBehavior::Cast(App().State().document->Resolve(objects.values.script))
                    ->GetParameterOperationCount();
            });
        };
        const auto before = count();
        const auto history = App().State().History().UndoCount();
        const auto open = [&] {
            SelectGraphDialog(ctx, objects, objects.graphElement);
            OnMainThread(ctx, [&] { App().Canvas().OpenOperationPicker(objects.values.script.object, {40, 100}); });
            ctx->Yield(4);
            ctx->SetRef("//###Operation");
        };
        open();
        for (const char *part : {"input1", "input2", "output"}) {
            ctx->ItemClick((std::string(part) + "/##part").c_str());
            ctx->Yield(2);
            ctx->KeyChars("Integer");
            ctx->Yield(3);
            ctx->KeyPress(ImGuiKey_Enter);
            ctx->Yield(3);
        }
        ctx->ItemClick("operation/##part");
        ctx->Yield(2);
        ctx->KeyChars("Addition");
        ctx->Yield(3);
        ctx->KeyPress(ImGuiKey_Enter);
        ctx->Yield(3);
        const auto combo = ctx->ItemInfo("input1/##part").ID;
        ResizeGraphDialog(ctx, "//###Operation", "##operation-fields");
        IM_CHECK_EQ(ctx->ItemInfo("input1/##part").ID, combo);
        ctx->ItemClick("Cancel");
        ctx->Yield(3);
        IM_CHECK(count() == before && App().State().History().UndoCount() == history);
        open();
        ctx->ItemClick("OK");
        ctx->Yield(4);
        IM_CHECK(count() == before + 1 && App().State().History().UndoCount() == history + 1);
        OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        IM_CHECK_EQ(count(), before);
        OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Redo()); });
        IM_CHECK_EQ(count(), before + 1);
        OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        CloseGraphDialogs(ctx, objects);
    };
    auto *graphDelete =
        IM_REGISTER_TEST(engine, "appearancegraphdialogs", "graph_and_script_deletion_resize_cancel_and_single_undo");
    graphDelete->GuiFunc = MainThreadGui;
    graphDelete->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto objects = MakeGraphDialogs(ctx);
        for (bool script : {false, true}) {
            const auto target = script ? objects.values.script : objects.graph;
            const auto history = App().State().History().UndoCount();
            const auto revision = App().State().document->ChangeRevision();
            const char *ref = script ? "//Confirm Script(s) Deletion" : "//Warning!###confirmgraphs";
            const char *content = script ? "##delete-script" : "##delete-graphs";
            const auto open = [&] {
                SelectGraphDialog(ctx, objects, script ? Selection::HeaderOf(target.object) : objects.graphElement);
                OnMainThread(ctx, [] { App().Canvas().DeleteSelection(); });
                ctx->Yield(4);
                ctx->SetRef(ref);
            };
            open();
            ResizeGraphDialog(ctx, ref, content, script ? "OK" : "Yes", script ? "Cancel" : "No");
            ctx->ItemClick(script ? "Cancel" : "No");
            ctx->Yield(3);
            IM_CHECK(GraphObjectAlive(ctx, target) && App().State().History().UndoCount() == history);
            IM_CHECK_EQ(App().State().document->ChangeRevision(), revision);
            open();
            ctx->ItemClick(script ? "OK" : "Yes");
            ctx->Yield(4);
            IM_CHECK(!GraphObjectAlive(ctx, target) && App().State().History().UndoCount() == history + 1);
            OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Undo()); });
            IM_CHECK(GraphObjectAlive(ctx, target));
            OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Redo()); });
            IM_CHECK(!GraphObjectAlive(ctx, target));
            OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        }
        CloseGraphDialogs(ctx, objects);
    };
    auto *graphQueries =
        IM_REGISTER_TEST(engine, "appearancegraphdialogs", "find_and_ck_properties_resize_keep_document_and_history");
    graphQueries->GuiFunc = MainThreadGui;
    graphQueries->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto objects = MakeGraphDialogs(ctx);
        const auto history = App().State().History().UndoCount();
        const auto revision = App().State().document->ChangeRevision();
        SelectGraphDialog(ctx, objects, objects.values.element);
        ctx->KeyPress(ImGuiKey_K);
        ctx->Yield(4);
        ResizeGraphDialog(ctx, "//CK Properties", "##ck-properties", "Close", "Close", false, 200);
        ctx->ItemClick("//CK Properties/Close");
        ctx->Yield(3);
        OnMainThread(ctx, [] { App().Canvas().OpenFind(); });
        ctx->Yield(3);
        ctx->SetRef("//Find###SchematicFind");
        TypeGraphDialog(ctx, "Find", "Graph dialog");
        const auto id = ctx->ItemInfo("Find").ID;
        ResizeGraphDialog(ctx, "//Find###SchematicFind", "##find", "Find Next", "Cancel");
        IM_CHECK_EQ(ctx->UiContext->ActiveId, id);
        ctx->ItemClick(ctx->GetWindowByRef("//Find###SchematicFind")->GetID("Search Closed Scripts/Graphs"));
        ctx->ItemClick("Cancel");
        ctx->Yield(3);
        IM_CHECK(App().State().History().UndoCount() == history &&
                 App().State().document->ChangeRevision() == revision);
        SelectGraphDialog(ctx, objects, Selection::HeaderOf(objects.values.script.object));
        OnMainThread(ctx, [] { App().Canvas().OpenFind(); });
        ctx->Yield(3);
        TypeGraphDialog(ctx, "//Find###SchematicFind/Find", objects.values.name.c_str());
        ctx->ItemClick("//Find###SchematicFind/Find Next");
        ctx->Yield(4);
        IM_CHECK(App().Canvas().Found().IsValid());
        IM_CHECK(App().State().History().UndoCount() == history &&
                 App().State().document->ChangeRevision() == revision);
        CloseGraphDialogs(ctx, objects);
    };
    auto *graphReplace =
        IM_REGISTER_TEST(engine, "appearancegraphdialogs", "block_and_graph_replacement_resize_cancel_and_single_undo");
    graphReplace->GuiFunc = MainThreadGui;
    graphReplace->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto objects = MakeGraphDialogs(ctx);
        ResizeSetup(ctx, App().WorkspaceForTests().Role("main.schematic"), 1800);
        const auto menu = [&](ObjectRef target, bool recent) {
            const ElementRef element{ElementKind::Behavior, objects.values.script.object, target.object};
            SelectGraphDialog(ctx, objects, element);
            const auto shown =
                Read<std::vector<SchematicCanvas::ShownBehavior>>(ctx, [] { return App().Canvas().ShownBehaviors(); });
            const auto found =
                std::find_if(shown.begin(), shown.end(), [&](const auto &b) { return b.behavior == target.object; });
            if (found == shown.end()) return false;
            ctx->MouseMoveToPos(recent ? Center(found->min, found->max) : ImVec2(found->min.x + 12, found->min.y + 12));
            if (recent) ctx->KeyDown(ImGuiMod_Shift);
            ctx->MouseClick(ImGuiMouseButton_Right);
            if (recent) ctx->KeyUp(ImGuiMod_Shift);
            ctx->Yield(3);
            return true;
        };
        const auto priorRecent = Read<std::vector<CKGUID>>(ctx, [] { return App().RecentBlocksForTests(); });
        struct RestoreRecent {
            ImGuiTestContext *ctx;
            std::vector<CKGUID> value;
            ~RestoreRecent() {
                OnMainThread(ctx, [&] { App().RecentBlocksForTests() = value; });
            }
        } restore{ctx, priorRecent};
        OnMainThread(ctx, [&] {
            auto *block = CKBehavior::Cast(App().State().document->Resolve(objects.values.block));
            App().RecentBlocksForTests() = {block->GetPrototypeGuid()};
        });
        const auto steps = App().State().History().UndoCount();
        for (bool accept : {false, true}) {
            IM_CHECK(menu(objects.values.block, true));
            ctx->ItemClick("//$FOCUSED/Bezier Transform");
            ctx->Yield(4);
            ResizeGraphDialog(ctx, "//Replace Building Block", "##replace-block", "Yes", "No");
            auto *window = ctx->GetWindowByRef("//Replace Building Block");
            const auto all = ctx->ItemInfo(window->GetID("Apply to All")).RectFull;
            IM_CHECK(all.Min.x >= window->Pos.x && all.Max.x <= window->Pos.x + window->Size.x);
            ctx->ItemClick(accept ? "//Replace Building Block/Yes" : "//Replace Building Block/No");
            ctx->Yield(5);
            IM_CHECK_EQ(App().State().History().UndoCount(), steps + (accept ? 1 : 0));
            IM_CHECK(GraphObjectAlive(ctx, objects.values.block) != accept);
            if (accept) {
                OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Undo()); });
                IM_CHECK(GraphObjectAlive(ctx, objects.values.block));
                OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Redo()); });
                IM_CHECK(!GraphObjectAlive(ctx, objects.values.block));
                OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Undo()); });
            }
        }
        OnMainThread(ctx, [&] { App().Canvas().ToggleFold(objects.values.script.object, objects.graph.object); });
        ctx->Yield(4);
        const auto graphSteps = App().State().History().UndoCount();
        const auto sourceName = ObjectName(ctx, objects.other.object);
        const int sourceIndex = Read<int>(ctx, [&] {
            const auto result =
                EditorCommands().Query("schematic.replacements", {ObjectArgument("target", objects.graph)});
            const auto *rows = EditorClient::Find(result, "graphs");
            IM_CHECK_NO_RET(rows);
            for (size_t i = 0; rows && i < rows->fields.size(); ++i) {
                EditorResponse row;
                row.fields = rows->fields[i].fields;
                const auto *object = EditorClient::Find(row, "object");
                if (object && object->object == objects.other) {
                    IM_CHECK_NO_RET(EditorClient::Text(row, "name") == sourceName);
                    IM_CHECK_NO_RET(
                        EditorClient::Text(row, "script") ==
                        CKBehavior::Cast(App().State().document->Resolve(objects.values.script))->GetName());
                    return int(i);
                }
            }
            return -1;
        });
        IM_CHECK(sourceIndex >= 0);
        for (bool accept : {false, true}) {
            IM_CHECK(menu(objects.graph, false));
            ctx->MenuClick("//$FOCUSED/Replace with/Replace with existing Graph");
            ctx->Yield(4);
            ResizeParameterDialog(ctx, "//Replace with", {1200, 600});
            ctx->SetRef("//Replace with");
            auto *listTable = ctx->UiContext->Tables.GetByKey(ctx->GetWindowByRef("//Replace with")->GetID("graphs"));
            IM_CHECK(listTable);
            ctx->ScrollToY(listTable->InnerWindow->ID,
                           sourceIndex * (ImGui::GetFontSize() + 2 * ImGui::GetStyle().CellPadding.y));
            ctx->Yield(3);
            const auto sourceId =
                ImHashStr(sourceName.c_str(), 0, ImHashData(&sourceIndex, sizeof(sourceIndex), listTable->ID));
            const auto item = ctx->ItemInfo(sourceId);
            IM_CHECK(item.ID == sourceId && item.ID != 0);
            ctx->ScrollToItemY(item.ID);
            ctx->Yield(2);
            auto *table = ctx->UiContext->Tables.GetByKey(ctx->GetWindowByRef("//Replace with")->GetID("graphs"));
            IM_CHECK(table);
            const auto row = ctx->ItemInfo(item.ID).RectFull;
            ctx->MouseMoveToPos({table->InnerClipRect.Min.x + 12, (row.Min.y + row.Max.y) * .5f});
            ctx->MouseClick(ImGuiMouseButton_Left);
            ctx->Yield(2);
            ResizeGraphDialog(ctx, "//Replace with", "##replace-graph", "Ok", "Cancel");
            ctx->ItemClick(accept ? "Ok" : "Cancel");
            ctx->Yield(5);
            IM_CHECK_EQ(App().State().History().UndoCount(), graphSteps + (accept ? 1 : 0));
            IM_CHECK(GraphObjectAlive(ctx, objects.graph) != accept);
            IM_CHECK(GraphObjectAlive(ctx, objects.other));
            if (accept) {
                OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Undo()); });
                IM_CHECK(GraphObjectAlive(ctx, objects.graph));
                OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Redo()); });
                IM_CHECK(!GraphObjectAlive(ctx, objects.graph));
                OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Undo()); });
            }
        }
        CloseGraphDialogs(ctx, objects);
    };
    auto *graphQuickAdd =
        IM_REGISTER_TEST(engine, "appearancegraphdialogs", "quick_add_bound_filter_cancel_and_single_undo");
    graphQuickAdd->GuiFunc = MainThreadGui;
    graphQuickAdd->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto objects = MakeGraphDialogs(ctx);
        const auto count = [&] {
            return Read<int>(ctx, [&] {
                std::function<int(CKBehavior *)> descendants = [&](CKBehavior *owner) {
                    int total = owner->GetSubBehaviorCount();
                    for (int i = 0; i < owner->GetSubBehaviorCount(); ++i)
                        total += descendants(owner->GetSubBehavior(i));
                    return total;
                };
                return descendants(CKBehavior::Cast(App().State().document->Resolve(objects.values.script)));
            });
        };
        const auto before = count();
        const auto history = App().State().History().UndoCount();
        for (bool accept : {false, true}) {
            SelectGraphDialog(ctx, objects, objects.values.element);
            const auto point = EmptySchematicPoint(objects.values.script.object);
            IM_CHECK(point.has_value());
            ctx->MouseMoveToPos(point->second);
            ctx->KeyDown(ImGuiMod_Ctrl);
            ctx->MouseDoubleClick(ImGuiMouseButton_Left);
            ctx->KeyUp(ImGuiMod_Ctrl);
            ctx->Yield(4);
            IM_CHECK(!ctx->UiContext->OpenPopupStack.empty());
            auto *window = ctx->UiContext->OpenPopupStack.back().Window;
            IM_CHECK(window && window->WasActive);
            ctx->SetRef(window);
            IM_CHECK(ctx->ItemExists("##name"));
            TypeGraphDialog(ctx, "##name", "Bezier Transform");
            const auto *viewport = ImGui::GetMainViewport();
            IM_CHECK(window->WasActive && window->Pos.x >= viewport->WorkPos.x &&
                     window->Pos.y >= viewport->WorkPos.y &&
                     window->Pos.x + window->Size.x <= viewport->WorkPos.x + viewport->WorkSize.x &&
                     window->Pos.y + window->Size.y <= viewport->WorkPos.y + viewport->WorkSize.y);
            IM_CHECK(count() == before && App().State().History().UndoCount() == history);
            ctx->KeyPress(accept ? ImGuiKey_Enter : ImGuiKey_Escape);
            ctx->Yield(5);
            IM_CHECK(count() == before + (accept ? 1 : 0) &&
                     App().State().History().UndoCount() == history + (accept ? 1 : 0));
            if (accept) {
                OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Undo()); });
                IM_CHECK_EQ(count(), before);
                OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Redo()); });
                IM_CHECK_EQ(count(), before + 1);
                OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Undo()); });
            }
        }
        CloseGraphDialogs(ctx, objects);
    };
    auto *breakpointConfig =
        IM_REGISTER_TEST(engine, "appearancebreakpointdialogs", "conditions_resize_cell_edit_cancel_and_single_undo");
    breakpointConfig->GuiFunc = MainThreadGui;
    breakpointConfig->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto objects = MakeGraphDialogs(ctx);
        OnMainThread(ctx, [&] {
            auto &doc = *App().State().document;
            IM_CHECK_NO_RET(
                ToggleBreakpoint(doc, objects.values.script.object, EXTRA_DATA_BEHAVIOR, objects.graph.object));
            ExtraSubData condition;
            condition.id1 = objects.values.local.object;
            const int value = 7;
            condition.buffer.resize(sizeof(value));
            std::memcpy(condition.buffer.data(), &value, sizeof(value));
            IM_CHECK_NO_RET(SetBreakpointConditions(doc, objects.values.script.object, EXTRA_DATA_BEHAVIOR,
                                                    objects.graph.object, {condition}));
        });
        const auto read = [&] {
            return Read<int>(ctx, [&] {
                const auto *b = FindBreakpoint(App().State().document->Session(objects.values.script.object)->Data(),
                                               EXTRA_DATA_BEHAVIOR, objects.graph.object);
                if (!b || b->subData.empty()) return -1;
                int value = 0;
                std::memcpy(&value, b->subData.front().buffer.data(), sizeof(value));
                return value;
            });
        };
        const auto history = App().State().History().UndoCount();
        const auto revision = App().State().document->ChangeRevision();
        const auto open = [&] {
            SelectGraphDialog(ctx, objects, objects.graphElement);
            OnMainThread(ctx, [] { App().Canvas().ConfigureBreakpoints(); });
            ctx->Yield(4);
            ResizeParameterDialog(ctx, "//###BreakpointsConfiguration", {1250, 650});
            ctx->SetRef(
                GraphDialogRows(ctx, "//###BreakpointsConfiguration", "##breakpoint-configuration", "##conditions"));
            ctx->ItemClick("$$0/##row");
            ctx->Yield(2);
            ctx->SetRef("//###BreakpointsConfiguration");
        };
        const auto edit = [&](const char *text) {
            ctx->ItemClick("Edit Reference Parameter");
            ctx->Yield(3);
            TypeGraphDialog(ctx, "//$FOCUSED/##value", text);
            ctx->KeyPress(ImGuiKey_Enter);
            ctx->Yield(3);
        };
        open();
        edit("19");
        ResizeGraphDialog(ctx, "//###BreakpointsConfiguration", "##breakpoint-configuration", "OK", "Cancel", true,
                          240);
        IM_CHECK_GT(SetupDialogContent(ctx, "//###BreakpointsConfiguration", "##breakpoint-configuration")->ScrollMax.x,
                    0);
        IM_CHECK_GT(SetupDialogContent(ctx, "//###BreakpointsConfiguration", "##breakpoint-configuration")->ScrollMax.y,
                    0);
        IM_CHECK_EQ(read(), 7);
        ctx->ItemClick("Cancel/##dialogbutton");
        ctx->Yield(3);
        IM_CHECK(read() == 7 && App().State().History().UndoCount() == history &&
                 App().State().document->ChangeRevision() == revision);
        open();
        edit("23");
        ctx->ItemClick("OK/##dialogbutton");
        ctx->Yield(4);
        IM_CHECK(read() == 23 && App().State().History().UndoCount() == history + 1);
        OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        IM_CHECK_EQ(read(), 7);
        OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Redo()); });
        IM_CHECK_EQ(read(), 23);
        OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        CloseGraphDialogs(ctx, objects);
    };
    auto *breakpointList =
        IM_REGISTER_TEST(engine, "appearancebreakpointdialogs", "list_resize_remove_cancel_goto_and_single_undo");
    breakpointList->GuiFunc = MainThreadGui;
    breakpointList->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto objects = MakeGraphDialogs(ctx);
        OnMainThread(ctx, [&] {
            auto &doc = *App().State().document;
            IM_CHECK_NO_RET(
                ToggleBreakpoint(doc, objects.values.script.object, EXTRA_DATA_BEHAVIOR, objects.graph.object));
            IM_CHECK_NO_RET(
                ToggleBreakpoint(doc, objects.values.script.object, EXTRA_DATA_BEHAVIOR, objects.other.object));
        });
        const auto count = [&] {
            return Read<size_t>(ctx, [] {
                const auto result = EditorCommands().Query("schematic.breakpoints");
                const auto *items = EditorClient::Find(result, "breakpoints");
                return items ? items->fields.size() : size_t(0);
            });
        };
        const auto before = count();
        const auto history = App().State().History().UndoCount();
        const auto open = [&] {
            OnMainThread(ctx, [] { App().Canvas().OpenBreakpointList(); });
            ctx->Yield(4);
            ctx->SetRef("//###BreakpointList");
        };
        open();
        ResizeGraphDialog(ctx, "//###BreakpointList", "##breakpoint-list", "Ok", "Cancel", true, 230);
        IM_CHECK_GT(SetupDialogContent(ctx, "//###BreakpointList", "##breakpoint-list")->ScrollMax.x, 0);
        IM_CHECK_GT(SetupDialogContent(ctx, "//###BreakpointList", "##breakpoint-list")->ScrollMax.y, 0);
        auto *content = SetupDialogContent(ctx, "//###BreakpointList", "##breakpoint-list");
        ctx->ScrollToBottom(content->ID);
        ctx->ScrollToX(content->ID, 0);
        ctx->Yield(3);
        ctx->ItemClick("Remove All");
        IM_CHECK_EQ(count(), before);
        ctx->ItemClick("Cancel/##dialogbutton");
        ctx->Yield(3);
        IM_CHECK(count() == before && App().State().History().UndoCount() == history);
        open();
        ResizeParameterDialog(ctx, "//###BreakpointList", {1200, 650});
        ctx->ItemClick("Remove All");
        ctx->ItemClick("Ok/##dialogbutton");
        ctx->Yield(4);
        IM_CHECK(count() == 0 && App().State().History().UndoCount() == history + 1);
        OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        IM_CHECK_EQ(count(), before);
        OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Redo()); });
        IM_CHECK_EQ(count(), size_t(0));
        OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        CloseGraphDialogs(ctx, objects);
    };

    auto *explorerList = IM_REGISTER_TEST(engine, "appearanceapplicationdialogs", "explorer_list_cancel_refresh_and_single_undo");
    explorerList->GuiFunc = MainThreadGui;
    explorerList->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        const auto objects = OpenExplorerDialog(ctx, false);
        SeedApplicationDialogRedo(ctx, objects.other);
        OpenExplorerRename(ctx, objects, false);
        const auto before = ReadApplicationDialogState(ctx);
        TypeGraphDialog(ctx, "##name", "Cancelled Explorer name");
        const auto active = ctx->UiContext->ActiveId;
        ResizeGraphDialog(ctx, "//Rename###explorerrename", "##explorer-name", "OK", "Cancel", false, 170);
        IM_CHECK(ctx->UiContext->ActiveId == active && std::string(ctx->UiContext->InputTextState.GetText()) == "Cancelled Explorer name");
        ctx->ItemClick("Cancel");
        ctx->Yield(3);
        IM_CHECK(ObjectName(ctx, objects.target.object) == objects.name && ReadApplicationDialogState(ctx) == before);
        OpenExplorerRename(ctx, objects, false);
        const auto history = ReadApplicationDialogState(ctx).undo;
        TypeGraphDialog(ctx, "##name", "Accepted Explorer name");
        RenameDialogObject(ctx, objects.target, "Outside the Explorer filter");
        ctx->Yield(4);
        const auto rows = Read<std::vector<vdev::SearchResult>>(ctx, [] { return App().Panels().SearchResults(); });
        IM_CHECK(rows.size() == 1 && rows.front().objectReference == objects.other);
        ResizeParameterDialog(ctx, "//Rename###explorerrename", {850, 180});
        ctx->ItemClick("OK");
        ctx->Yield(4);
        IM_CHECK(ObjectName(ctx, objects.target.object) == "Accepted Explorer name" &&
                 ObjectName(ctx, objects.other.object) == objects.otherName && ReadApplicationDialogState(ctx).undo == history + 2);
        OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        IM_CHECK(ObjectName(ctx, objects.target.object) == "Outside the Explorer filter");
        OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Redo()); });
        IM_CHECK(ObjectName(ctx, objects.target.object) == "Accepted Explorer name");
        OnMainThread(ctx, [] {
            IM_CHECK_NO_RET(App().State().History().Undo());
            IM_CHECK_NO_RET(App().State().History().Undo());
        });
        IM_CHECK(ObjectName(ctx, objects.target.object) == objects.name);
        CloseExplorerDialog(ctx, objects);
    };
    auto *explorerTree = IM_REGISTER_TEST(engine, "appearanceapplicationdialogs", "explorer_tree_cancel_reopen_and_single_undo");
    explorerTree->GuiFunc = MainThreadGui;
    explorerTree->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        const auto objects = OpenExplorerDialog(ctx, true);
        SeedApplicationDialogRedo(ctx, objects.other);
        OpenExplorerRename(ctx, objects, true);
        const auto before = ReadApplicationDialogState(ctx);
        TypeGraphDialog(ctx, "##name", "Discarded tree name");
        ResizeGraphDialog(ctx, "//Rename###explorertreerename", "##explorer-name", "OK", "Cancel", false, 170);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
        IM_CHECK(ApplicationDialogShown(ctx, "//Schematic Explorer") &&
                 ObjectName(ctx, objects.target.object) == objects.name && ReadApplicationDialogState(ctx) == before);
        OpenExplorerRename(ctx, objects, true);
        TypeGraphDialog(ctx, "##name", "Renamed tree Script");
        ctx->KeyPress(ImGuiKey_Enter);
        ctx->Yield(4);
        IM_CHECK(ApplicationDialogShown(ctx, "//Schematic Explorer") &&
                 ObjectName(ctx, objects.target.object) == "Renamed tree Script" && ReadApplicationDialogState(ctx).undo == before.undo + 1);
        // The selected tree row survived the refresh. F2 reads its current name.
        ctx->WindowFocus("//Schematic Explorer");
        ctx->KeyPress(ImGuiKey_F2);
        ctx->Yield(4);
        ctx->SetRef("//Rename###explorertreerename");
        IM_CHECK(ApplicationDialogShown(ctx, "//Rename###explorertreerename") &&
                 std::string(ctx->UiContext->InputTextState.GetText()) == "Renamed tree Script");
        const auto renamed = ReadApplicationDialogState(ctx);
        ctx->ItemClick("OK");
        ctx->Yield(3);
        IM_CHECK(ReadApplicationDialogState(ctx) == renamed);
        OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        IM_CHECK(ObjectName(ctx, objects.target.object) == objects.name);
        OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Redo()); });
        IM_CHECK(ObjectName(ctx, objects.target.object) == "Renamed tree Script");
        OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        CloseExplorerDialog(ctx, objects);
    };
    auto *explorerDeleted = IM_REGISTER_TEST(engine, "appearanceapplicationdialogs", "explorer_deleted_target_does_not_rename_replacement_row");
    explorerDeleted->GuiFunc = MainThreadGui;
    explorerDeleted->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        const auto objects = OpenExplorerDialog(ctx, false);
        OpenExplorerRename(ctx, objects, false);
        TypeGraphDialog(ctx, "##name", "Do not rename the next row");
        OnMainThread(ctx, [&] { IM_CHECK_NO_RET(vdev::DeleteScript(*App().State().document, objects.target.object).ok); });
        ctx->Yield(4);
        const auto before = ReadApplicationDialogState(ctx);
        const auto rows = Read<std::vector<vdev::SearchResult>>(ctx, [] { return App().Panels().SearchResults(); });
        IM_CHECK(rows.size() == 1 && rows.front().objectReference == objects.other);
        ctx->ItemClick("OK");
        ctx->Yield(4);
        IM_CHECK(!GraphObjectAlive(ctx, objects.target) && ObjectName(ctx, objects.other.object) == objects.otherName &&
                 ReadApplicationDialogState(ctx) == before);
        OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        IM_CHECK(GraphObjectAlive(ctx, objects.target) && ObjectName(ctx, objects.target.object) == objects.name);
        CloseExplorerDialog(ctx, objects);
    };
    auto *timeSettings = IM_REGISTER_TEST(engine, "appearanceapplicationdialogs", "time_settings_reflow_cancel_validation_and_atomic_apply");
    timeSettings->GuiFunc = MainThreadGui;
    timeSettings->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        const auto configuration = SetupDialogTheme(ctx);
        vdev::ObjectRef object;
        OnMainThread(ctx, [&] {
            const auto made = vdev::CreateLayoutObject(*App().State().document, vdev::LayoutObject::Frame3D);
            IM_CHECK_NO_RET(made.ok);
            object = App().State().document->Reference(made.created);
        });
        SeedApplicationDialogRedo(ctx, object);
        const auto before = ReadApplicationDialogState(ctx);
        const auto values = ReadDialogTime(ctx);
        const auto original = Read<std::vector<vdev::EditorField>>(ctx, [] { return EditorCommands().Query("runtime.time").fields; });
        const auto open = [&] {
            ctx->ItemClick("//##StatusBar/play/##bitmap", 1);
            ctx->Yield(4);
            ctx->SetRef("//###TimeSettings");
            ResizeParameterDialog(ctx, "//###TimeSettings", {1400, 560});
        };
        const auto edit = [&] {
            ctx->ItemClick("##free");
            ctx->ItemClick("##sync");
            ctx->ItemClick("##limit");
            ctx->ItemClick("##linked");
            ctx->ItemClick("##behlimit");
            TypeGraphDialog(ctx, "##frame", "75");
            TypeGraphDialog(ctx, "##behavior", "90");
            TypeGraphDialog(ctx, "##min", "2");
            TypeGraphDialog(ctx, "##max", "80");
            ctx->ItemClick("##hidefps");
            TypeGraphDialog(ctx, "##scale", "1.5");
        };
        open(); edit();
        const auto active = ctx->UiContext->ActiveId;
        ResizeGraphDialog(ctx, "//###TimeSettings", "##time-settings", "OK", "Cancel", true, 230);
        auto *content = SetupDialogContent(ctx, "//###TimeSettings", "##time-settings");
        const auto table = ctx->UiContext->Tables.GetByKey(ctx->GetWindowByRef("//###TimeSettings")->GetID("##time-sections"));
        IM_CHECK(table && table->ColumnsCount == 1 && content->ScrollMax.y > 0 && ctx->UiContext->ActiveId == active);
        IM_CHECK(ReadDialogTime(ctx) == values && ReadApplicationDialogState(ctx) == before);
        ctx->ItemClick("Cancel/##dialogbutton");
        ctx->Yield(4);
        IM_CHECK(ReadDialogTime(ctx) == values && ReadApplicationDialogState(ctx) == before);
        open(); edit();
        TypeGraphDialog(ctx, "##min", "100000");
        ctx->ItemClick("OK/##dialogbutton");
        ctx->Yield(4);
        IM_CHECK(ApplicationDialogShown(ctx, "//###TimeSettings") && ReadDialogTime(ctx) == values && ReadApplicationDialogState(ctx) == before);
        TypeGraphDialog(ctx, "##min", "2");
        ctx->ItemClick("OK/##dialogbutton");
        ctx->Yield(4);
        IM_CHECK(!ApplicationDialogShown(ctx, "//###TimeSettings"));
        const std::vector<double> expected{2, 1, 75, 90, 1.5, 2, 80, values.back() == 0 ? 1. : 0.};
        IM_CHECK(ReadDialogTime(ctx) == expected && ReadApplicationDialogState(ctx) == before);
        OnMainThread(ctx, [&] {
            const auto restore = vdev::BorrowEditorFields(original);
            IM_CHECK_NO_RET(EditorCommands().Command("runtime.set-time", {restore.data(), restore.data() + restore.size()}).status == EDITOR_OK);
        });
        FinishSetupDialogs(ctx, configuration);
    };
    auto *duplicateTree = IM_REGISTER_TEST(engine, "appearanceapplicationdialogs", "duplicate_tree_long_names_paging_selection_and_cancel");
    duplicateTree->GuiFunc = MainThreadGui;
    duplicateTree->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto configuration = SetupDialogTheme(ctx);
        const DuplicateDialogFile file;
        struct Choices { unsigned calls = 0; std::vector<DuplicateAction> actions; };
        const auto choices = std::make_shared<Choices>();
        OnMainThread(ctx, [&] {
            auto &doc = *App().State().document;
            std::vector<CK_ID> objects;
            for (int i = 0; i < 140; ++i) {
                const auto kind = i < 135 ? LayoutObject::Material : LayoutObject::Frame3D;
                const auto made = CreateLayoutObject(doc, kind);
                IM_CHECK_NO_RET(made.ok);
                doc.Context()->GetObject(made.created)->SetName(("Duplicate " + std::to_string(i) + " " + std::string(160, 'D')).c_str());
                objects.push_back(made.created);
            }
            std::string error;
            IM_CHECK_NO_RET(SaveObjectsAs(doc, objects, file.path.u8string(), &error));
        });
        const auto before = ReadApplicationDialogState(ctx);
        const auto begin = [&] {
            OnMainThread(ctx, [&] {
                App().State().files.WithDuplicates(file.path.u8string(), [choices](const std::vector<DuplicateName> *names) {
                    ++choices->calls;
                    choices->actions.clear();
                    if (names) for (const auto &name : *names) choices->actions.push_back(name.action);
                });
            });
            WaitForDuplicateDialog(ctx);
            ResizeGraphDialog(ctx, "//###DuplicateNames", "##duplicate-names", "OK", "Cancel", true, 260);
            ResizeParameterDialog(ctx, "//###DuplicateNames", {850, 600});
        };
        begin();
        auto *rows = GraphDialogRows(ctx, "//###DuplicateNames", "##duplicate-names", "##duplicates");
        IM_CHECK(rows && rows->ScrollMax.x > 0 && rows->ScrollMax.y > 0);
        OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().files.PendingDuplicates()->duplicates.size() == 140); });
        ctx->ScrollToBottom(rows->ID);
        ctx->ScrollToX(rows->ID, rows->ScrollMax.x);
        ctx->Yield(3);
        IM_CHECK(rows->Scroll.x > 0 && rows->Scroll.y > 0);
        ctx->ScrollToTop(rows->ID);
        ctx->ScrollToX(rows->ID, 0);
        ctx->Yield(3);
        ChooseDuplicateDialogAction(ctx, 0, "Use Current");
        ChooseDuplicateDialogAction(ctx, 1, "Rename");
        // A Shift range and Ctrl toggle select the first and third leaves.
        ctx->ItemClick("**/$$2/##row", 0, ImGuiTestOpFlags_MoveToEdgeL | ImGuiTestOpFlags_NoScroll);
        ctx->KeyDown(ImGuiMod_Shift);
        ctx->ItemClick("**/$$4/##row", 0, ImGuiTestOpFlags_MoveToEdgeL | ImGuiTestOpFlags_NoScroll);
        ctx->KeyUp(ImGuiMod_Shift);
        ctx->KeyDown(ImGuiMod_Ctrl);
        ctx->ItemClick("**/$$3/##row", 0, ImGuiTestOpFlags_MoveToEdgeL | ImGuiTestOpFlags_NoScroll);
        ctx->KeyUp(ImGuiMod_Ctrl);
        ChooseDuplicateDialogAction(ctx, 2, "Ignore");
        // Closing and reopening a category retains its choices.
        ctx->ItemClick("**/$$1/##arrow");
        ctx->Yield(2);
        IM_CHECK(!ctx->ItemExists("**/$$2/##row"));
        ChooseDuplicateDialogAction(ctx, 1, "Expand Children");
        IM_CHECK(ctx->ItemExists("**/$$2/##row"));
        OnMainThread(ctx, [] {
            const auto *pending = App().State().files.PendingDuplicates();
            IM_CHECK_NO_RET(std::all_of(pending->duplicates.begin(), pending->duplicates.end(),
                [](const auto &d) { return d.action == DuplicateAction::Ignore; }));
        });
        ctx->ItemClick("Cancel/##dialogbutton");
        ctx->Yield(4);
        IM_CHECK(!ApplicationDialogShown(ctx, "//###DuplicateNames") && choices->calls == 0 && ReadApplicationDialogState(ctx) == before);
        begin();
        ChooseDuplicateDialogAction(ctx, 0, "Use Current");
        ChooseDuplicateDialogAction(ctx, 1, "Rename");
        ctx->ItemClick("**/$$2/##row", 0, ImGuiTestOpFlags_MoveToEdgeL | ImGuiTestOpFlags_NoScroll);
        ctx->KeyDown(ImGuiMod_Shift);
        ctx->ItemClick("**/$$4/##row", 0, ImGuiTestOpFlags_MoveToEdgeL | ImGuiTestOpFlags_NoScroll);
        ctx->KeyUp(ImGuiMod_Shift);
        ctx->KeyDown(ImGuiMod_Ctrl);
        ctx->ItemClick("**/$$3/##row", 0, ImGuiTestOpFlags_MoveToEdgeL | ImGuiTestOpFlags_NoScroll);
        ctx->KeyUp(ImGuiMod_Ctrl);
        ChooseDuplicateDialogAction(ctx, 2, "Ignore");
        ctx->ItemClick("OK/##dialogbutton");
        ctx->Yield(4);
        IM_CHECK(choices->calls == 1 && choices->actions.size() == 140 && ReadApplicationDialogState(ctx) == before);
        IM_CHECK(choices->actions[0] == DuplicateAction::Ignore && choices->actions[1] == DuplicateAction::Rename &&
                 choices->actions[2] == DuplicateAction::Ignore && choices->actions[134] == DuplicateAction::Rename &&
                 choices->actions[135] == DuplicateAction::UseCurrent);
        FinishSetupDialogs(ctx, configuration);
    };
    auto *duplicateImport = IM_REGISTER_TEST(engine, "appearanceapplicationdialogs", "duplicate_import_cancel_keeps_history_and_existing_import_behavior");
    duplicateImport->GuiFunc = MainThreadGui;
    duplicateImport->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto configuration = SetupDialogTheme(ctx);
        const DuplicateDialogFile file;
        ObjectRef source;
        OnMainThread(ctx, [&] {
            auto &doc = *App().State().document;
            doc.History().SetLimit(1000);
            const auto made = CreateLayoutObject(doc, LayoutObject::Frame3D);
            IM_CHECK_NO_RET(made.ok);
            source = doc.Reference(made.created);
            doc.Resolve(source)->SetName(("Imported duplicate " + std::string(160, 'I')).c_str());
            std::string error;
            IM_CHECK_NO_RET(SaveObjectsAs(doc, {source.object}, file.path.u8string(), &error));
        });
        SeedApplicationDialogRedo(ctx, source);
        const auto before = ReadApplicationDialogState(ctx);
        const auto count = [&] { return Read<size_t>(ctx, [] {
            std::vector<SetupObjectRow> rows;
            IM_CHECK_NO_RET(ReadSetupChoices(EditorCommands(), CKCID_3DENTITY, rows));
            return rows.size();
        }); };
        const auto original = count();
        for (bool accept : {false, true}) {
            const auto operation = Read<EditorOperation>(ctx, [&] {
                const auto result = EditorCommands().Command("document.import", {TextArgument("path", file.path.u8string())});
                IM_CHECK_NO_RET(result.status == EDITOR_QUEUED);
                return result.operation;
            });
            WaitForDuplicateDialog(ctx);
            ResizeGraphDialog(ctx, "//###DuplicateNames", "##duplicate-names", "OK", "Cancel", true, 260);
            ResizeParameterDialog(ctx, "//###DuplicateNames", {850, 600});
            ChooseDuplicateDialogAction(ctx, 0, "Rename");
            IM_CHECK(ReadApplicationDialogState(ctx) == before);
            ctx->ItemClick(accept ? "OK/##dialogbutton" : "Cancel/##dialogbutton");
            EditorStatus completed = EDITOR_QUEUED;
            const auto deadline = SDL_GetTicks() + 10000;
            while (completed == EDITOR_QUEUED && SDL_GetTicks() < deadline) {
                ctx->Sleep(.01f);
                completed = Read<EditorStatus>(ctx, [&] { return EditorCommands().Completion(operation).status; });
            }
            IM_CHECK(completed == (accept ? EDITOR_OK : EDITOR_CANCELLED) && !ApplicationDialogShown(ctx, "//###DuplicateNames"));
            IM_CHECK(count() == original + (accept ? 1 : 0));
            if (!accept) { IM_CHECK(ReadApplicationDialogState(ctx) == before); continue; }
            const auto after = ReadApplicationDialogState(ctx);
            IM_CHECK(after.undo == before.undo && after.redo == before.redo && after.undoLabel == before.undoLabel &&
                     after.redoLabel == before.redoLabel && after.revision > before.revision && after.dirty);
            const auto name = ObjectName(ctx, source.object);
            // Import keeps its existing non-undoable behavior. Older records
            // remain usable, including Redo retained while the prompt was open.
            OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Redo()); });
            IM_CHECK(ObjectName(ctx, source.object) == name + " temporary" && count() == original + 1);
            OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Undo()); });
            IM_CHECK(ObjectName(ctx, source.object) == name && count() == original + 1 && GraphObjectAlive(ctx, source));
        }
        OnMainThread(ctx, [] { App().State().History().SetLimit(50); });
        FinishSetupDialogs(ctx, configuration);
    };

    auto *webDraft = IM_REGISTER_TEST(engine, "appearanceoutputdialogs", "web_page_resize_cancel_validate_and_export");
    webDraft->GuiFunc = MainThreadGui;
    webDraft->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto objects = MakeOutputDialogs(ctx);
        const OutputDialogFiles files;
        const auto page = files.Long("page.htm"), composition = page.parent_path() / "page.cmo";
        const auto before = ReadApplicationDialogState(ctx);
        const std::string title = "Web dialog " + std::string(110, 'T');
        for (bool accept : {false, true}) {
            const auto operation = StartOutputDialog(ctx, "document.export-web", {TextArgument("path", page.u8string()),
                IntegerArgument("ask-options", 1), IntegerArgument("cmo", 1)});
            WaitOutputWindow(ctx, "//###CreateWebPage");
            ResizeParameterDialog(ctx, "//###CreateWebPage", {1400, 700});
            TypeGraphDialog(ctx, "##width", "640");
            TypeGraphDialog(ctx, "##height", "480");
            TypeGraphDialog(ctx, "##title", title.c_str());
            const auto active = ctx->UiContext->ActiveId;
            ResizeGraphDialog(ctx, "//###CreateWebPage", "##web-page", "OK", "Cancel", true, 250);
            IM_CHECK(ctx->UiContext->ActiveId == active && std::string(ctx->UiContext->InputTextState.GetText()) == title);
            IM_CHECK(ReadApplicationDialogState(ctx) == before && !std::filesystem::exists(page));
            ResizeParameterDialog(ctx, "//###CreateWebPage", {1400, 700});
            if (accept) {
                TypeGraphDialog(ctx, "##width", "invalid");
                ctx->ItemClick("OK/##dialogbutton");
                ctx->Yield(3);
                IM_CHECK(ApplicationDialogShown(ctx, "//###CreateWebPage") && !std::filesystem::exists(page));
                TypeGraphDialog(ctx, "##width", "640");
            } else ctx->ItemClick("##preview");
            ctx->ItemClick(accept ? "OK/##dialogbutton" : "Cancel/##dialogbutton");
            IM_CHECK(WaitOutputOperation(ctx, operation) == (accept ? EDITOR_OK : EDITOR_CANCELLED));
            IM_CHECK(ReadApplicationDialogState(ctx) == before);
            if (accept) {
                const auto html = OutputDialogBytes(page);
                IM_CHECK(html.find(title) != std::string::npos && html.find("WIDTH=\"640\" HEIGHT=\"480\"") != std::string::npos);
                IM_CHECK(std::filesystem::exists(composition) && !IsPlayerFile(composition.u8string()));
            } else IM_CHECK(!std::filesystem::exists(page) && !std::filesystem::exists(composition));
        }
        CloseOutputDialogs(ctx, objects);
    };
    auto *webOverwrite = IM_REGISTER_TEST(engine, "appearanceoutputdialogs", "web_overwrite_cancel_returns_to_draft_and_preserves_files");
    webOverwrite->GuiFunc = MainThreadGui;
    webOverwrite->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto objects = MakeOutputDialogs(ctx);
        const OutputDialogFiles files;
        const auto page = files.Long("existing.htm"), composition = page.parent_path() / "existing.vmo";
        WriteOutputDialogFile(page, "original HTML");
        WriteOutputDialogFile(composition, "original composition");
        const auto before = ReadApplicationDialogState(ctx);
        const auto operation = StartOutputDialog(ctx, "document.export-web", {TextArgument("path", page.u8string()),
            IntegerArgument("ask-options", 1), IntegerArgument("preview", 1)});
        WaitOutputWindow(ctx, "//###CreateWebPage");
        ResizeParameterDialog(ctx, "//###CreateWebPage", {1200, 600});
        TypeGraphDialog(ctx, "##title", "Keep this web draft");
        TypeGraphDialog(ctx, "##width", "800");
        TypeGraphDialog(ctx, "##height", "600");
        ctx->ItemClick("OK/##dialogbutton");
        WaitOutputWindow(ctx, "//###DevMessage");
        ResizeGraphDialog(ctx, "//###DevMessage", "##message", "OK", "Cancel", true, 180);
        ctx->KeyPress(ImGuiKey_Escape);
        WaitOutputWindow(ctx, "//###CreateWebPage");
        const auto options = ReadOutputDialogOptions(ctx);
        IM_CHECK(EditorClient::Text(options, "title") == "Keep this web draft" &&
                 EditorClient::Integer(options, "width") == 800 && EditorClient::Integer(options, "height") == 600 &&
                 EditorClient::Integer(options, "preview") == 1 && EditorClient::Text(options, "path") == page.u8string());
        IM_CHECK(ReadApplicationDialogState(ctx) == before && OutputDialogBytes(page) == "original HTML" &&
                 OutputDialogBytes(composition) == "original composition");
        ctx->ItemClick("Cancel/##dialogbutton");
        IM_CHECK(WaitOutputOperation(ctx, operation) == EDITOR_CANCELLED && ReadApplicationDialogState(ctx) == before);
        IM_CHECK(OutputDialogBytes(page) == "original HTML" && OutputDialogBytes(composition) == "original composition");
        CloseOutputDialogs(ctx, objects);
    };
    auto *printOptions = IM_REGISTER_TEST(engine, "appearanceoutputdialogs", "print_resize_coupled_settings_cancel_and_pages_keep_history");
    printOptions->GuiFunc = MainThreadGui;
    printOptions->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto objects = MakeOutputDialogs(ctx);
        const OutputDialogFiles files;
        const auto pages = files.root / "pages";
        const auto before = ReadApplicationDialogState(ctx);
        const auto begin = [&] {
            const auto operation = StartOutputDialog(ctx, "document.print", {TextArgument("path", pages.u8string())});
            WaitOutputWindow(ctx, "//###PrintParameters");
            ResizeParameterDialog(ctx, "//###PrintParameters", {1500, 700});
            return operation;
        };
        const auto cancelled = begin();
        const auto initial = ReadOutputDialogOptions(ctx);
        const char *ids[] = {"##perMmH", "##perMmV", "##resH", "##resV", "##pagesH", "##pagesV"};
        const char *values[] = {"5", "6", "400", "300", "2", "2"};
        for (int i = 0; i < 6; ++i) TypeGraphDialog(ctx, ids[i], values[i]);
        const auto edited = ReadOutputDialogOptions(ctx);
        IM_CHECK(EditorClient::Integer(edited, "pages-h") == 2 && EditorClient::Integer(edited, "pages-v") == 2 &&
                 EditorClient::Integer(edited, "resolution-h") > 0 && EditorClient::Integer(edited, "resolution-v") > 0);
        const auto active = ctx->UiContext->ActiveId;
        ResizeGraphDialog(ctx, "//###PrintParameters", "##print-parameters", "OK", "Cancel", true, 250);
        IM_CHECK(ctx->UiContext->ActiveId == active && ReadApplicationDialogState(ctx) == before);
        auto *content = SetupDialogContent(ctx, "//###PrintParameters", "##print-parameters");
        IM_CHECK(content->ScrollMax.y > 0);
        const auto *table = ctx->UiContext->Tables.GetByKey(ctx->GetWindowByRef("//###PrintParameters")->GetID("##print-sections"));
        IM_CHECK(table && table->ColumnsCount == 1);
        ResizeParameterDialog(ctx, "//###PrintParameters", {1500, 700});
        for (const char *id : {"##fillHeader", "##fillBody", "##fillShapes", "##bitmaps", "##colorLinks"}) ctx->ItemClick(id);
        ctx->ItemClick("Cancel/##dialogbutton");
        IM_CHECK(WaitOutputOperation(ctx, cancelled) == EDITOR_CANCELLED && !std::filesystem::exists(pages / "page1.png") &&
                 ReadApplicationDialogState(ctx) == before);
        const auto accepted = begin();
        const auto retained = ReadOutputDialogOptions(ctx);
        for (const char *field : {"fill-header", "fill-body", "fill-shapes", "bitmaps", "color-links"})
            IM_CHECK(EditorClient::Integer(retained, field) != EditorClient::Integer(initial, field));
        TypeGraphDialog(ctx, "##pagesH", "2");
        TypeGraphDialog(ctx, "##pagesV", "1");
        ctx->ItemClick("OK/##dialogbutton");
        IM_CHECK(WaitOutputOperation(ctx, accepted) == EDITOR_OK && ReadApplicationDialogState(ctx) == before);
        for (int i = 1; i <= 2; ++i) {
            std::vector<unsigned char> pixels; int width = 0, height = 0;
            IM_CHECK(ReadImageRgba((pages / ("page" + std::to_string(i) + ".png")).u8string(), pixels, width, height));
            IM_CHECK(width > 0 && height > 0 && pixels.size() == size_t(width) * height * 4);
            bool opaque = true;
            for (size_t a = 3; a < pixels.size(); a += 4) opaque &= pixels[a] == 255;
            IM_CHECK(opaque);
        }
        IM_CHECK(!std::filesystem::exists(pages / "page3.png"));
        CloseOutputDialogs(ctx, objects);
    };
    auto *objectExport = IM_REGISTER_TEST(engine, "appearanceoutputdialogs", "object_export_resize_cancel_preserves_file_and_accept_keeps_undo");
    objectExport->GuiFunc = MainThreadGui;
    objectExport->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto objects = MakeOutputDialogs(ctx);
        const OutputDialogFiles files;
        const auto path = files.root / "object.nmo";
        WriteOutputDialogFile(path, "original NMO");
        const auto before = ReadApplicationDialogState(ctx);
        for (bool accept : {false, true}) {
            const auto operation = StartFileExportDialog(ctx, "object.export", objects.object, path, false);
            WaitOutputWindow(ctx, "//###LevelSaveAs");
            ResizeGraphDialog(ctx, "//###LevelSaveAs", "##file-export", "Save...", "Cancel", false, 190);
            ResizeParameterDialog(ctx, "//###LevelSaveAs", {900, 350});
            ctx->ItemClick("Hide script representation in Schematic view");
            IM_CHECK(ReadApplicationDialogState(ctx) == before && OutputDialogBytes(path) == "original NMO");
            ctx->ItemClick(accept ? "Save..." : "Cancel");
            IM_CHECK(WaitOutputOperation(ctx, operation) == (accept ? EDITOR_OK : EDITOR_CANCELLED) &&
                     ReadApplicationDialogState(ctx) == before);
            if (accept) IM_CHECK(std::filesystem::file_size(path) > 64 && OutputDialogBytes(path) != "original NMO");
            else IM_CHECK(OutputDialogBytes(path) == "original NMO");
        }
        CloseOutputDialogs(ctx, objects);
    };
    auto *behaviorExport = IM_REGISTER_TEST(engine, "appearanceoutputdialogs", "behavior_export_resize_invalid_version_cancel_and_description");
    behaviorExport->GuiFunc = MainThreadGui;
    behaviorExport->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto objects = MakeOutputDialogs(ctx);
        const OutputDialogFiles files;
        const auto path = files.root / "script.nms";
        WriteOutputDialogFile(path, "original NMS");
        const auto before = ReadApplicationDialogState(ctx);
        const std::string author = "Export author " + std::string(100, 'A');
        const std::string description = "Description\n" + std::string(200, 'D') + "\nEnd";
        for (bool accept : {false, true}) {
            const auto operation = StartFileExportDialog(ctx, "script.export", objects.script, path, true);
            WaitOutputWindow(ctx, "//###behaviorinfo");
            ResizeParameterDialog(ctx, "//###behaviorinfo", {1200, 650});
            TypeGraphDialog(ctx, "##author", author.c_str());
            TypeGraphDialog(ctx, "##description", description.c_str());
            TypeGraphDialog(ctx, "##version", "1e");
            ResizeGraphDialog(ctx, "//###behaviorinfo", "##file-export", "OK", "Cancel", false, 220);
            IM_CHECK(SetupDialogContent(ctx, "//###behaviorinfo", "##file-export")->ScrollMax.y > 0);
            ctx->ItemClick("OK");
            ctx->Yield(3);
            IM_CHECK(ApplicationDialogShown(ctx, "//###behaviorinfo") && ReadApplicationDialogState(ctx) == before &&
                     OutputDialogBytes(path) == "original NMS");
            ResizeParameterDialog(ctx, "//###behaviorinfo", {1200, 650});
            TypeGraphDialog(ctx, "##version", "2.5");
            ctx->ItemClick("Hide script representation in schematic view");
            ctx->ItemClick(accept ? "OK" : "Cancel");
            IM_CHECK(WaitOutputOperation(ctx, operation) == (accept ? EDITOR_OK : EDITOR_CANCELLED) &&
                     ReadApplicationDialogState(ctx) == before);
            if (accept) {
                BehaviorDescription saved;
                IM_CHECK(ReadBehaviorDescription(path.u8string(), saved) && saved.author == author &&
                         saved.version == 2.5f && saved.description == description);
            } else IM_CHECK(OutputDialogBytes(path) == "original NMS");
        }
        CloseOutputDialogs(ctx, objects);
    };
    auto *unsavedCancel = IM_REGISTER_TEST(engine, "appearanceoutputdialogs", "unsaved_choices_resize_cancel_and_discard_keep_original_behavior");
    unsavedCancel->GuiFunc = MainThreadGui;
    unsavedCancel->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto objects = MakeOutputDialogs(ctx);
        const auto before = ReadApplicationDialogState(ctx);
        const auto cancelled = StartOutputDialog(ctx, "document.new");
        WaitOutputWindow(ctx, "//Unsaved changes");
        const auto save = Read<std::string>(ctx, [] {
            return EditorClient::Text(EditorCommands().Query("document.input"), "initial").empty() ? "Save..." : "Save";
        });
        ResizeGraphDialog(ctx, "//Unsaved changes", "##save-changes", save.c_str(), "Cancel", false, 230);
        CheckUnsavedDialogButtons(ctx, save.c_str());
        ctx->ItemClick("Cancel");
        IM_CHECK(WaitOutputOperation(ctx, cancelled) == EDITOR_CANCELLED && ReadApplicationDialogState(ctx) == before);
        const auto discarded = StartOutputDialog(ctx, "document.new");
        WaitOutputWindow(ctx, "//Unsaved changes");
        ctx->ItemClick("Discard");
        IM_CHECK(WaitOutputOperation(ctx, discarded) == EDITOR_OK);
        ctx->Yield(6);
        const auto after = ReadApplicationDialogState(ctx);
        IM_CHECK(after.document != before.document && after.undo == 0 && after.objects.empty() &&
                 !GraphObjectAlive(ctx, objects.object));
        CloseOutputDialogs(ctx, objects);
    };
    auto *unsavedSave = IM_REGISTER_TEST(engine, "appearanceoutputdialogs", "unsaved_save_before_new_preserves_saved_object_and_history_rules");
    unsavedSave->GuiFunc = MainThreadGui;
    unsavedSave->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto objects = MakeOutputDialogs(ctx);
        const OutputDialogFiles files;
        const auto path = files.root / "saved.cmo";
        const auto saved = StartOutputDialog(ctx, "document.save-as", {TextArgument("path", path.u8string())});
        IM_CHECK(WaitOutputOperation(ctx, saved) == EDITOR_OK);
        RenameDialogObject(ctx, objects.object, "Saved choice target");
        SeedApplicationDialogRedo(ctx, objects.object);
        const auto before = ReadApplicationDialogState(ctx);
        const auto created = StartOutputDialog(ctx, "document.new");
        WaitOutputWindow(ctx, "//Unsaved changes");
        ResizeGraphDialog(ctx, "//Unsaved changes", "##save-changes", "Save", "Cancel", false, 230);
        CheckUnsavedDialogButtons(ctx, "Save");
        IM_CHECK(ReadApplicationDialogState(ctx) == before);
        ctx->ItemClick("Save");
        IM_CHECK(WaitOutputOperation(ctx, created) == EDITOR_OK && ReadApplicationDialogState(ctx).document != before.document);
        const auto reopened = StartOutputDialog(ctx, "document.open", {TextArgument("path", path.u8string())});
        IM_CHECK(WaitOutputOperation(ctx, reopened) == EDITOR_OK);
        ctx->Yield(6);
        const auto found = Read<bool>(ctx, [] {
            std::vector<SetupObjectRow> rows;
            return ReadSetupChoices(EditorCommands(), CKCID_3DENTITY, rows) &&
                std::any_of(rows.begin(), rows.end(), [](const auto &row) { return row.name == "Saved choice target"; });
        });
        IM_CHECK(found && !ReadApplicationDialogState(ctx).dirty && ReadApplicationDialogState(ctx).undo == 0);
        CloseOutputDialogs(ctx, objects);
    };
    auto *fileConfirm = IM_REGISTER_TEST(engine, "appearanceoutputdialogs", "long_file_confirmation_scroll_cancel_save_and_existing_undo");
    fileConfirm->GuiFunc = MainThreadGui;
    fileConfirm->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        const auto objects = MakeOutputDialogs(ctx);
        const OutputDialogFiles files;
        const auto path = files.Long("version.cmo", 10);
        const auto saved = StartOutputDialog(ctx, "document.save-as", {TextArgument("path", path.u8string())});
        IM_CHECK(WaitOutputOperation(ctx, saved) == EDITOR_OK);
        RenameDialogObject(ctx, objects.object, "Versioned dialog object");
        SeedApplicationDialogRedo(ctx, objects.object);
        const auto before = ReadApplicationDialogState(ctx);
        const auto version = std::filesystem::u8path(VersionedPath(path.u8string(), 1));
        WriteOutputDialogFile(version, "original version file");
        const auto original = OutputDialogBytes(path);
        for (bool accept : {false, true}) {
            const auto operation = StartOutputDialog(ctx, "document.save-version");
            WaitOutputWindow(ctx, "//###DevMessage");
            ResizeGraphDialog(ctx, "//###DevMessage", "##message", "OK", "Cancel", true, 180);
            auto *content = SetupDialogContent(ctx, "//###DevMessage", "##message");
            IM_CHECK(content->ScrollMax.y > 0 && content->ScrollMax.x == 0);
            ctx->ScrollToBottom(content->ID);
            ctx->Yield(3);
            CheckSetupDialogBounds(ctx, "//###DevMessage", "##message", "OK", "Cancel", true);
            IM_CHECK(ReadApplicationDialogState(ctx) == before && OutputDialogBytes(version) == "original version file");
            ctx->KeyPress(accept ? ImGuiKey_Enter : ImGuiKey_Escape);
            IM_CHECK(WaitOutputOperation(ctx, operation) == (accept ? EDITOR_OK : EDITOR_CANCELLED));
            if (accept) {
                const auto after = ReadApplicationDialogState(ctx);
                IM_CHECK(std::filesystem::file_size(version) > 64 && OutputDialogBytes(path) == original &&
                         after.document == before.document && after.undo == before.undo && after.redo == before.redo &&
                         after.objects == before.objects && !after.dirty);
                OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Undo()); });
                IM_CHECK(ObjectName(ctx, objects.object.object) != "Versioned dialog object");
                OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Redo()); });
                IM_CHECK(ObjectName(ctx, objects.object.object) == "Versioned dialog object");
            } else IM_CHECK(ReadApplicationDialogState(ctx) == before && OutputDialogBytes(version) == "original version file");
        }
        CloseOutputDialogs(ctx, objects);
    };

    auto *startup = IM_REGISTER_TEST(engine, "appearancestartup", "appearance_startup_state");
    startup->GuiFunc = MainThreadGui;
    startup->TestFunc = [](ImGuiTestContext *ctx) {
        ctx->Yield(8);
        const auto *phase = std::getenv("NEMODEV_APPEARANCE_PHASE");
        IM_CHECK(phase);
        if (std::string(phase) == "basic") {
            OnMainThread(ctx, [&] {
                IM_CHECK_NO_RET(App().WorkspaceForTests().Application().Themes().empty());
                IM_CHECK_NO_RET(vdev::DevBitmap(777) == ImTextureID());
                IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && ImGui::GetIO().Fonts->Fonts.Size != 0);
            });
            return;
        }
        const bool missing = std::string(phase) == "missing";
        if (std::string(phase) == "save") {
            OnMainThread(ctx, [&] {
                auto &workspace = App().WorkspaceForTests();
                auto config = workspace.Application().Configuration();
                Alternate(config);
                std::string error;
                IM_CHECK_NO_RET(workspace.Configure(config, error));
            });
            ctx->Yield(4);
        }
        CheckAppearance(ctx, !missing);
    };
}
} // namespace vdevuitest
