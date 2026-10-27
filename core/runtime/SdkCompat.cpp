#include <filesystem>
#include "core/runtime/SdkCompat.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/runtime/CkRuntimeHost.h"

namespace vdev {

CKSPRITETEXT_ALIGNMENT SpriteTextAlign(CKSpriteText *text) {
    if (!text) return static_cast<CKSPRITETEXT_ALIGNMENT>(0);
    return text->GetAlign();
}

namespace {

// The choices not given yet, in the 2.1 load callback.
struct DuplicateChoices {
    CKContext *context;
    std::vector<DuplicateName> pending;
};

// CKFile clears the callback on some exits, but not on every failure. Keep
// the previous callback pair alive outside the protected runtime call: SEH
// recovery does not unwind C++ objects created inside GuardedCall.
class LoadCallbackScope {
public:
    explicit LoadCallbackScope(CKContext *context)
        : m_Context(context), m_Callback(context->m_UserLoadCallBack), m_Data(context->m_UserLoadCallBackArgs) {}
    ~LoadCallbackScope() { m_Context->SetUserLoadCallback(m_Callback, m_Data); }
    LoadCallbackScope(const LoadCallbackScope &) = delete;
    LoadCallbackScope &operator=(const LoadCallbackScope &) = delete;

private:
    CKContext *m_Context;
    CK_USERLOADCALLBACK m_Callback;
    void *m_Data;
};

// The new name of a renamed duplicate as CK2 2.1 CKContext::GetSecureName
// (not exported) makes it: the name and one more than the largest number
// following the name in the objects of its class ("%s%03d"). buffer: CK's
// 512 characters (CKContext::GetStringBuffer).
void SecureName(CKContext *context, char *buffer, const char *name, CK_CLASSID cid) {
    const size_t length = std::strlen(name);
    int largest = -1;
    const CK_ID *ids = context->GetObjectsListByClassID(cid);
    for (int i = 0; ids && i < context->GetObjectsCountByClassID(cid); ++i) {
        CKObject *object = context->GetObject(ids[i]);
        const char *other = object ? object->GetName() : nullptr;
        if (other && std::strncmp(other, name, length) == 0 && other[length])
            largest = std::max(largest, std::atoi(other + length));
    }
    std::snprintf(buffer, 512, "%s%03d", name, largest + 1);
}

CK_LOADMODE AnswerDuplicate(CK_CLASSID cid, CKSTRING oldName, CKSTRING newName, CKObject **existing, void *data) {
    DuplicateChoices &choices = *static_cast<DuplicateChoices *>(data);
    for (auto it = choices.pending.begin(); oldName && it != choices.pending.end(); ++it) {
        if (it->cid != cid || it->name != oldName) continue;
        const DuplicateAction action = it->action;
        choices.pending.erase(it);
        switch (action) {
        case DuplicateAction::Rename:
            if (!newName) return CKLOAD_OK;
            SecureName(choices.context, const_cast<char *>(newName), oldName, cid);
            if (existing) *existing = nullptr;
            return CKLOAD_RENAME;
        case DuplicateAction::UseCurrent: return existing && *existing ? CKLOAD_USECURRENT : CKLOAD_OK;
        default: return CKLOAD_OK;
        }
    }
    return CKLOAD_OK;
}

} // namespace

CKERROR CreateCKContext(CKContext **context, void *window, int renderEngine) {
    return CKCreateContext(context, window, renderEngine, 0);
}

CKERROR LoadFileChoosingDuplicates(CkRuntimeHost &host, CKFile *file, const std::string &path, CKObjectArray *objects,
                                   CK_LOAD_FLAGS flags, const std::vector<DuplicateName> *duplicates) {
    const std::string native = std::filesystem::u8path(path).string();
    CKContext *context = host.Context();
    DuplicateChoices choices{context, duplicates ? *duplicates : std::vector<DuplicateName>()};
    LoadCallbackScope callback(context);
    if (duplicates) {
        context->SetUserLoadCallback(&AnswerDuplicate, &choices);
        flags = static_cast<CK_LOAD_FLAGS>(flags | CK_LOAD_CHECKDUPLICATES);
    }
    CKERROR loaded = CKERR_INVALIDFILE;
    if (!GuardedCall([&] { loaded = file->Load(const_cast<CKSTRING>(native.c_str()), objects, flags); },
                     &host, "CKFile::Load"))
        return CKERR_INVALIDFILE;
    return loaded;
}

} // namespace vdev
