#include "VirtoolsDevTestSupport.h"

namespace vdevtest {
namespace {

class LifecycleObserver : public CKBaseManager {
public:
    explicit LifecycleObserver(CKContext *context)
        : CKBaseManager(context, CKGUID(0x71d932a6, 0x1682c45f), "Editor lifecycle baseline") {}

    CKDWORD GetValidFunctionsMask() override {
        return CKMANAGER_FUNC_OnSequenceToBeDeleted | CKMANAGER_FUNC_OnSequenceDeleted |
               CKMANAGER_FUNC_PreClearAll | CKMANAGER_FUNC_PostClearAll;
    }
    CKERROR SequenceToBeDeleted(CK_ID *ids, int count) override {
        for (int i = 0; i < count; ++i) {
            before.push_back(ids[i]);
            beforeAlive = beforeAlive && m_Context->GetObject(ids[i]) != nullptr;
        }
        return CK_OK;
    }
    CKERROR SequenceDeleted(CK_ID *ids, int count) override {
        for (int i = 0; i < count; ++i) {
            after.push_back(ids[i]);
            afterGone = afterGone && m_Context->GetObject(ids[i]) == nullptr;
        }
        return CK_OK;
    }
    CKERROR PreClearAll() override { clearOrder.push_back(1); return CK_OK; }
    CKERROR PostClearAll() override { clearOrder.push_back(2); return CK_OK; }

    std::vector<CK_ID> before, after;
    std::vector<int> clearOrder;
    bool beforeAlive = true, afterGone = true;
};

} // namespace

void NativeObjectLifecycles(CkRuntimeHost &host) {
    Document document(host);
    CHECK(document.New(nullptr));
    CKContext *context = document.Context();
    auto *observer = new LifecycleObserver(context);
    CHECK(context->RegisterNewManager(observer) == CK_OK);

    CKObject *object = context->CreateObject(CKCID_OBJECT, (CKSTRING) "Lifecycle source");
    CHECK(object);
    const CK_ID oldId = object->GetID();
    CHECK(context->DestroyObject(oldId, CK_DESTROY_FREEID) == CK_OK);
    CHECK(observer->before == std::vector<CK_ID>{oldId});
    CHECK(observer->after == std::vector<CK_ID>{oldId});
    CHECK(observer->beforeAlive && observer->afterGone);

    // A generation made only from the Context and CK_ID cannot distinguish
    // these two objects. The future ObjectRef must observe actual destruction.
    CKObject *replacement = context->CreateObject(CKCID_OBJECT, (CKSTRING) "Lifecycle replacement");
    CHECK(replacement && replacement->GetID() == oldId);
    CKObject *other = context->CreateObject(CKCID_OBJECT, (CKSTRING) "Lifecycle batch");
    CHECK(other);
    CK_ID batch[] = {replacement->GetID(), other->GetID()};
    observer->before.clear();
    observer->after.clear();
    CHECK(context->DestroyObjects(batch, 2, CK_DESTROY_FREEID) == CK_OK);
    CHECK(observer->before == std::vector<CK_ID>(batch, batch + 2));
    CHECK(observer->after == std::vector<CK_ID>(batch, batch + 2));
    CHECK(observer->beforeAlive && observer->afterGone);

    CKObject *dynamic = context->CreateObject(CKCID_OBJECT, (CKSTRING) "Lifecycle dynamic", CK_OBJECTCREATION_DYNAMIC);
    CHECK(dynamic && dynamic->IsDynamic());
    const CK_ID dynamicId = dynamic->GetID();
    CHECK(context->DestroyObject(dynamicId, CK_DESTROY_FREEID) == CK_OK);
    CHECK(observer->before.back() == dynamicId && observer->after.back() == dynamicId);
    CHECK(observer->beforeAlive && observer->afterGone);

    CKObject *silent = context->CreateObject(CKCID_OBJECT, (CKSTRING) "Lifecycle without notifications");
    CHECK(silent);
    const CK_ID silentId = silent->GetID();
    const auto notified = observer->after.size();
    const auto notifiedBefore = observer->before.size();
    CHECK(context->DestroyObject(silentId, CK_DESTROY_FREEID | CK_DESTROY_NONOTIFY) == CK_OK);
    CHECK(observer->after.size() == notified && observer->before.size() == notifiedBefore);
    CHECK(context->GetObject(silentId) == nullptr);
    CKObject *reused = context->CreateObject(CKCID_OBJECT, (CKSTRING) "Lifecycle silent replacement");
    CHECK(reused && reused->GetID() == silentId);

    const auto generation = host.Generation();
    document.Close();
    CHECK(observer->clearOrder == std::vector<int>({1, 2}));
    CHECK(host.Generation() > generation);
    CHECK(document.New(nullptr));
    CHECK(context->GetManagerByGuid(observer->GetGuid()) == observer);
    document.Close();
    CHECK(observer->clearOrder == std::vector<int>({1, 2, 1, 2}));
}

} // namespace vdevtest
