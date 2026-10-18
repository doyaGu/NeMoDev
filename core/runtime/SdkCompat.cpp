#include <filesystem>
#include "core/runtime/SdkCompat.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

CKERROR LoadFileChoosingDuplicates(CKContext *context, CKFile *file, const std::string &path, CKObjectArray *objects,
                                   CK_LOAD_FLAGS flags, const std::vector<DuplicateName> *duplicates) {
    const std::string native = std::filesystem::u8path(path).string();
    if (!duplicates) return file->Load(const_cast<CKSTRING>(native.c_str()), objects, flags);
    DuplicateChoices choices{context, *duplicates};
    context->SetUserLoadCallback(&AnswerDuplicate, &choices);
    const CKERROR loaded = file->Load(const_cast<CKSTRING>(native.c_str()), objects,
                                      static_cast<CK_LOAD_FLAGS>(flags | CK_LOAD_CHECKDUPLICATES));
    context->SetUserLoadCallback(nullptr, nullptr);
    return loaded;
}

} // namespace vdev
