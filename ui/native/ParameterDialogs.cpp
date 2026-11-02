#include "ui/controls/ParameterDialog.h"
#include "ui/native/ParameterCopiesDetail.h"
#include "core/level/SetupCommands.h"
#include "core/graph/RuntimeGraph.h"
#include "core/document/Edit.h"
#include "core/runtime/TextEncoding.h"
#include "workspace/EditorClient.h"
#include "editor/ImGuiDrawing.h"
#include <cmath>

namespace vdev {
namespace { uint64_t Packed(CKGUID guid) { return uint64_t(guid.d1) << 32 | guid.d2; } }
ParameterDialog::ParameterDialog(ParameterCopies &s): copies(s), box(std::make_unique<ParameterBox>(s)) {}
ParameterDialog::~ParameterDialog() { Clear(); }

void ParameterDialog::ClearBitmapOptions() {
        if (bitmapOptions && copies.m_Data->document())
            if (auto *parameter =
                    copies.m_Data->document()->Resolve(bitmapOptions, CKCID_PARAMETER, Document::ObjectScope::Existing))
                copies.m_Data->document()->Context()->DestroyObject(parameter);
        bitmapOptions = {};
    }

void ParameterDialog::Clear() {
        box->Clear();
        ClearBitmapOptions();
        objects.clear();
        target = {};
        attributeOwner = {};
        attributeType = -1;
        attributeTypeName.clear();
        scope = {};
        open = shown = false;
        view = 0;
    }

EditorStatus ParameterDialog::Navigate(void *data,const EditorHost *host,const EditorRequest *request){
        auto &d=*static_cast<ParameterDialog*>(data);if(!d.copies.m_Data->document()||d.copies.m_Data->document()->Token()!=request->scope.document)return EDITOR_STALE;
        ObjectRef target{};std::vector<ObjectRef> objects;int kind=0;
        for(uint32_t i=0;i<request->count;++i){const auto&v=request->values[i];const std::string key(v.key.data,v.key.size);
            if(key=="target")target=v.object;else if(key=="objects"&&v.objects.count)objects.assign(v.objects.data,v.objects.data+v.objects.count);else if(key=="kind")kind=int(v.integer);}
        if (kind < 0 || kind > 4)
            return EDITOR_INVALID;
        for(auto object:objects)if(!d.copies.m_Data->document()->Resolve(object))return EDITOR_STALE;
        auto *parameter=CKParameter::Cast(d.copies.m_Data->document()->Resolve(target));
        if ((kind == 0 || kind == 4) && !parameter)
            return EDITOR_STALE;
        if(kind==1&&!d.copies.m_Data->document()->Resolve(target))return EDITOR_STALE;
        auto next=std::make_unique<ParameterBox>(d.copies);
        bool added=false;
        ObjectRef bitmapOptions{};
        ObjectRef attributeOwner{};
        int attributeType = -1;
        std::string attributeTypeName;
        const auto destroy = [&](CKParameterOut *value) {
            if (value)
                d.copies.m_Data->document()->Context()->DestroyObject(value);
        };
        std::unique_ptr<CKParameterOut, decltype(destroy)> ownedOptions(nullptr, destroy);
        if (kind == 4) {
            if (objects.size() != 1)
                return EDITOR_INVALID;
            auto *owner = CKBeObject::Cast(d.copies.m_Data->document()->Resolve(objects.front()));
            if (!owner)
                return EDITOR_STALE;
            for (int i = 0; i < owner->GetAttributeCount(); ++i)
                if (owner->GetAttributeParameterByIndex(i) == parameter) {
                    attributeType = owner->GetAttributeType(i);
                    break;
                }
            if (attributeType < 0)
                return EDITOR_STALE;
            attributeOwner = objects.front();
            auto *context = d.copies.m_Data->document()->Context();
            const auto *typeName = context->GetParameterManager()->ParameterTypeToName(parameter->GetType());
            attributeTypeName = typeName ? AnsiToUtf8(typeName) : std::string();
            const auto *name = parameter->GetName();
            if (!name || !*name)
                name = context->GetAttributeManager()->GetAttributeNameByType(attributeType);
            added = next->Add(d.copies.m_Data->document()->Reference(parameter->GetID()), AnsiToUtf8(name ? name : ""));
        } else if (kind == 3) {
            auto *bitmap = d.copies.m_Data->document()->Resolve(target);
            if (!bitmap || (!CKTexture::Cast(bitmap) && !CKSprite::Cast(bitmap)))
                return EDITOR_STALE;
            auto *options = ReadBitmapOptions(*d.copies.m_Data->document(), bitmap);
            if (!options)
                return EDITOR_REFUSED;
            ownedOptions.reset(options);
            options->ModifyObjectFlags(CK_OBJECT_PRIVATE, 0);
            bitmapOptions = d.copies.m_Data->document()->Reference(options->GetID());
            added = next->Add(d.copies.m_Data->document()->Reference(options->GetID()), "Format Options");
        } else if (kind == 0)
            added = next->Add(d.copies.m_Data->document()->Reference(parameter->GetID()), AnsiToUtf8(parameter->GetName() ? parameter->GetName() : ""));
        else {
            ParameterDependencies options;
            if(kind==1)options=d.copies.m_Data->copyOptions();
            else{
                EditorClient client(host,d.view);const auto result=client.Query("object.delete-options");if(result.status!=EDITOR_OK)return result.status;
                for(const auto&f:result.fields){if(f.key=="flags")options.flags=uint32_t(f.integer);else if(f.key=="options"){options.options.assign(f.integers.begin(), f.integers.end());}}
            }
            added=next->AddDependencies(kind==1?"Copy options":"Delete Options",kind==1?Packed(CKPGUID_COPYDEPENDENCIES):Packed(CKPGUID_DELETEDEPENDENCIES),options);
        }
        if(!added)return EDITOR_ERROR;
        d.box->Clear();
        d.ClearBitmapOptions();
        d.box = std::move(next);
        d.bitmapOptions = bitmapOptions;
        d.bitmapRevision = d.copies.m_Data->document()->ChangeRevision();
        d.attributeOwner = attributeOwner;
        d.attributeType = attributeType;
        d.attributeTypeName = std::move(attributeTypeName);
        d.objects = std::move(objects);
        d.target = target;
        d.kind = kind;
        d.scope = request->scope;
        ownedOptions.release();
        d.open=true;d.shown=false;return EDITOR_OK;
    }

EditorStatus ParameterDialog::Open(void *data,const EditorHost *host,const EditorRequest *request,EditorSink sink){
        const int kind=*static_cast<const int*>(data);std::vector<EditorValue> values(request->values,request->values+request->count);
        values.push_back({EDITOR_TEXT("kind"),EDITOR_INTEGER_VALUE,{},{},uint64_t(kind)});
        const EditorRequest forwarded{request->scope,values.data(),uint32_t(values.size()),{},request->operation};
        return host->navigateView(host->data,EditorString EDITOR_TEXT("parameter.dialog"),0,&forwarded,sink);
    }

EditorStatus ParameterDialog::Accept(void *data,const EditorHost *host,const EditorRequest *request,EditorSink sink){
        auto&d=*static_cast<ParameterDialog*>(data);if(!d.open||!d.copies.m_Data->document()||d.copies.m_Data->document()->Token()!=d.scope.document||request->scope.view!=d.view)return EDITOR_STALE;
        if (d.kind == 3) {
            if (!d.copies.m_Data->document()->Resolve(d.target) || d.copies.m_Data->document()->ChangeRevision() != d.bitmapRevision ||
                !d.box->Valid() || !d.box->Count()) {
                d.Clear();
                return EDITOR_STALE;
            }
            auto *work = d.copies.m_Data->Parameter(d.box->WorkReference());
            if (!work)
                return EDITOR_INVALID;
            const EditorValue target{EDITOR_TEXT("target"), EDITOR_OBJECT, d.target};
            const EditorRequest read{request->scope, &target, 1};
            EditorResponse projection;
            auto status =
                host->query(host->data, EditorString EDITOR_TEXT("setup.bitmap.format-options-value"), &read,
                            {&projection, [](void *p, const EditorValue *values, uint32_t count) {
                                 return CopyEditorFields(static_cast<EditorResponse *>(p)->fields, values, count);
                             }});
            if (status != EDITOR_OK) {
                d.shown = false;
                return status;
            }
            std::vector<EditorField> fields{{"target", EDITOR_OBJECT, d.target}};
            for (auto &field : projection.fields) {
                if (field.key == "schema")
                    continue;
                if (field.key == "value")
                    field = {"value", EDITOR_OBJECT, d.copies.m_Data->document()->Reference(work->GetID())};
                fields.push_back(std::move(field));
            }
            const auto values = BorrowEditorFields(fields);
            const EditorRequest write{
                request->scope, values.data(), uint32_t(values.values.size()), {}, request->operation};
            status =
                host->execute(host->data, EditorString EDITOR_TEXT("setup.bitmap.format-options-set"), &write, sink);
            if (status == EDITOR_OK || status == EDITOR_NO_CHANGE) {
                d.box->Clear();
                d.ClearBitmapOptions();
                d.open = false;
            } else
                d.shown = false;
            return status;
        }
        if (d.kind == 0 || d.kind == 4) {
            auto *parameter = CKParameter::Cast(d.copies.m_Data->document()->Resolve(d.target));
            auto *owner = CKBeObject::Cast(d.copies.m_Data->document()->Resolve(d.attributeOwner));
            if (!parameter || (d.kind == 4 && (!owner || owner->GetAttributeParameter(d.attributeType) != parameter))) {
                d.Clear();
                return EDITOR_STALE;
            }
            if (!d.box->Valid()) {
                d.shown = false;
                return EDITOR_INVALID;
            }
            const bool changed = d.box->Changed();
            auto *script = OwningScript(parameter);
            size_t steps = 0;
            if (d.kind == 4) {
                EditGroup group(*d.copies.m_Data->document(), "Edit Attribute");
                steps = d.box->Apply(script ? script->GetID() : 0);
            } else
                steps = d.box->Apply(script ? script->GetID() : 0);
            if (!steps && changed) {
                d.shown = false;
                return EDITOR_ERROR;
            }
            d.box->Clear();
            d.open = false;
            return EDITOR_OK;
        }
        ParameterDependencies options;if(!d.box->Dependencies(0,options))return EDITOR_INVALID;
        std::vector<uint64_t> flags;for(int i=0;i<int(options.options.size());++i)flags.push_back(options.options[size_t(i)]);
        const EditorValue values[]={ObjectsArgument("objects",d.objects),ObjectArgument("target",d.target),IntegerArgument("custom",1),IntegerArgument("flags",uint64_t(options.flags)),
            {EDITOR_TEXT("options"),EDITOR_INTEGER_LIST,{},{},0,{}, {flags.data(),uint32_t(flags.size())}}};
        std::vector<EditorValue> arguments(values,values+5);if(d.kind==2)arguments.erase(arguments.begin()+1);
        const EditorRequest forwarded{request->scope,arguments.data(),uint32_t(arguments.size()),{},request->operation};
        const auto status=host->execute(host->data,d.kind==1?EditorString EDITOR_TEXT("scene.duplicate"):EditorString EDITOR_TEXT("object.destroy"),&forwarded,sink);
        if((status==EDITOR_OK||status==EDITOR_NO_CHANGE)&&d.kind==2)d.copies.m_Data->clearSelection();
        d.box->Clear();d.open=false;
        return status;
    }
bool RegisterParameterDialogs(EditorApplication &editor,ParameterCopies &copies,std::string &error){
    auto *data=new ParameterDialog(copies);
    const EditorParameter parameter{EDITOR_TEXT("target"),EDITOR_OBJECT};
    const EditorParameter objects{EDITOR_TEXT("objects"),EDITOR_OBJECT_LIST};
    const EditorParameter pickArgs[] = {{EDITOR_TEXT("point"), EDITOR_REAL_LIST},
                                        {EDITOR_TEXT("class"), EDITOR_INTEGER_VALUE}};
    const EditorQueryDesc pick{
        sizeof(EditorQueryDesc),
        EDITOR_TEXT("parameter.object-at"),
        EDITOR_TEXT("builtin.parameters"),
        pickArgs,
        2,
        [](void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
            auto &copies = *static_cast<ParameterCopies *>(data);
            const auto &point = request->values[0];
            const auto cid = request->values[1].integer;
            if (point.reals.count != 2 || !std::isfinite(point.reals.data[0]) || !std::isfinite(point.reals.data[1]) || cid > INT32_MAX)
                return EDITOR_INVALID;
            const EditorValue result{EDITOR_TEXT("target"), EDITOR_OBJECT,
                copies.Pick(float(point.reals.data[0]), float(point.reals.data[1]), int32_t(cid))};
            return sink.write ? sink.write(sink.data, &result, 1) : EDITOR_INVALID;
        },
        &copies,
        EDITOR_QUERY_EACH_READ,
        EDITOR_DOCUMENT,
        &parameter,
        1};
    const EditorParameter copy[]={objects,parameter};
    static int parameterKind = 0, copyKind = 1, deleteKind = 2, bitmapKind = 3;
    const EditorCommandDesc commands[] = {
        {sizeof(EditorCommandDesc), EDITOR_TEXT("bitmap.edit-options"), EDITOR_TEXT("builtin.parameters"),
         EDITOR_TEXT("Format Options"), &parameter, 1, ParameterDialog::Open, &bitmapKind, EDITOR_DOCUMENT,
         EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("parameter.edit"), EDITOR_TEXT("builtin.parameters"),
         EDITOR_TEXT("Edit Parameter"), &parameter, 1, ParameterDialog::Open, &parameterKind, EDITOR_DOCUMENT,
         EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("scene.copy-options"), EDITOR_TEXT("builtin.parameters"),
         EDITOR_TEXT("Copy Options"), copy, 2, ParameterDialog::Open, &copyKind, EDITOR_DOCUMENT,
         EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("object.confirm-delete"), EDITOR_TEXT("builtin.parameters"),
         EDITOR_TEXT("Select Delete Options"), &objects, 1, ParameterDialog::Open, &deleteKind, EDITOR_DOCUMENT,
         EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("parameter.accept"), EDITOR_TEXT("builtin.parameters"),
         EDITOR_TEXT("Edit Parameter"), nullptr, 0, ParameterDialog::Accept, data, EDITOR_DOCUMENT,
         EDITOR_DOCUMENT_CHANGE}};
    const EditorParameter args[]={{EDITOR_TEXT("target"),EDITOR_OBJECT,1},{EDITOR_TEXT("objects"),EDITOR_OBJECT_LIST,1},{EDITOR_TEXT("kind"),EDITOR_INTEGER_VALUE}};
    EditorViewDesc view{};view.size=sizeof(view);view.imguiVersion=EDITOR_IMGUI_VERSION;view.id=EDITOR_TEXT("parameter.dialog");view.provider=EDITOR_TEXT("builtin.parameters");view.title=EDITOR_TEXT("Edit Parameter");view.kind=EDITOR_OVERLAY_VIEW;view.singleInstance=1;view.area=EDITOR_DOCUMENT;
    view.data=data;view.create=[](void*data,const EditorHost*,EditorView id)->void*{auto*d=static_cast<ParameterDialog*>(data);d->view=id;return d;};view.destroy=[](void*data){static_cast<ParameterDialog*>(data)->Clear();};
    view.draw=[](void*data,const EditorHost*host,EditorView,EditorViewFrame*frame){static_cast<ParameterDialog*>(data)->Draw(host,*frame);};view.parameters=args;view.count=3;view.navigate=ParameterDialog::Navigate;
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("parameters.dialogs");
    module.data = data;
    module.stop = [](void *data) { delete static_cast<ParameterDialog *>(data); };
    module.commands = commands;
    module.commandCount = 5;
    module.views = &view;
    module.viewCount = 1;
    module.queries = &pick;
    module.queryCount = 1;
    if(editor.Register(module,error))return true;delete data;return false;
}
} // namespace vdev
