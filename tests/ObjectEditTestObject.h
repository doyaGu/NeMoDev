#pragma once

#include <stdexcept>
#include "CKAll.h"

namespace vdevtest {

// A real CK object whose own serialization can fail at a chosen call. No
// production hooks or replacement runtime are needed to exercise recovery.
class PropertyTestObject : public CKObject {
public:
    explicit PropertyTestObject(CKContext *context) : CKObject(context, "Property failure test") {}

    int value = 0;
    int saves = 0, loads = 0, writes = 0;
    int failSave = 0, failLoad = 0, throwSave = 0;

    CKStateChunk *Save(CKFile *, CKDWORD) override {
        ++saves;
        if (saves == throwSave) throw std::runtime_error("Property capture failed");
        if (saves == failSave) return nullptr;
        CKStateChunk *state = CreateCKStateChunk(CKCID_OBJECT, nullptr);
        state->StartWrite();
        state->WriteIdentifier(0x7161);
        state->WriteInt(value);
        state->CloseChunk();
        return state;
    }

    CKERROR Load(CKStateChunk *state, CKFile *) override {
        ++loads;
        state->StartRead();
        if (!state->SeekIdentifier(0x7161)) return CKERR_INVALIDPARAMETER;
        value = state->ReadInt();
        if (loads == failLoad) {
            value = -100; // A failing reader may already have written part of its state.
            return CKERR_INVALIDPARAMETER;
        }
        return CK_OK;
    }
};

} // namespace vdevtest
