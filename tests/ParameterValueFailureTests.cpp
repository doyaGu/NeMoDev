#include "VirtoolsDevTestSupport.h"

namespace vdevtest {

static CK_PARAMETERSTRINGFUNCTION ValueTestStringFunction = nullptr;
struct ParameterStringFailures {
    int reads = 0, writes = 0, failRead = 0, failWrite = 0;
    bool writeThenReject = false;
};
static ParameterStringFailures ValueTestFailures;
static void ValueTestDelete(CKParameter *) {}
static int ValueTestString(CKParameter *parameter, char *text, CKBOOL fromString) {
    if (!fromString) {
        if (++ValueTestFailures.reads == ValueTestFailures.failRead) return 0;
    } else if (++ValueTestFailures.writes == ValueTestFailures.failWrite) {
        if (ValueTestFailures.writeThenReject) ValueTestStringFunction(parameter, text, TRUE);
        return CKERR_INVALIDPARAMETER;
    }
    return ValueTestStringFunction(parameter, text, fromString);
}

void ParameterValueFailures(CkRuntimeHost &host) {
    Document document(host);
    CHECK(document.New(nullptr));
    auto *manager = document.Context()->GetParameterManager();
    CKParameterTypeDesc description = *manager->GetParameterTypeDescription(manager->ParameterGuidToType(CKPGUID_STRING));
    ValueTestStringFunction = description.StringFunction;
    description.Guid = CKGUID(0x41676129, 0x15462738);
    description.DerivedFrom = CKPGUID_STRING;
    description.TypeName = "Parameter read regression";
    description.StringFunction = ValueTestString;
    description.DeleteFunction = ValueTestDelete;
    CHECK(manager->RegisterParameterType(&description) == CK_OK);
    auto *parameter = document.Context()->CreateCKParameterOut("Value", description.Guid);
    CHECK(parameter && ManagesMemory(document.Context(), parameter->GetType()));
    document.Include(parameter->GetID());
    auto &history = document.History();
    const auto reset = [&] {
        ValueTestFailures = {};
        CHECK(parameter->SetStringValue("Before") == CK_OK);
        history.Clear();
        document.MarkSaved();
        history.MarkSavePoint();
        // A failed edit must preserve an existing redo as well as the value.
        CHECK(SetParameterText(document, 0, parameter->GetID(), "Redo value").ok);
        CHECK(history.Undo() && ParameterText(parameter) == "Before" && !document.Dirty());
        ValueTestFailures = {};
    };
    for (int read : {1, 2, 3, 4}) {
        reset();
        ValueTestFailures.failRead = read;
        const auto result = SetParameterText(document, 0, parameter->GetID(), "After");
        CHECK(result.Status() == EditStatus::Error);
        CHECK(read > 2 || ValueTestFailures.writes == 0);
        ValueTestFailures = {};
        CHECK(ParameterText(parameter) == "Before" && !document.Dirty());
        CHECK(!history.CanUndo() && history.CanRedo());
        CHECK(history.Redo() && ParameterText(parameter) == "Redo value");
    }
    reset();
    ValueTestFailures.failWrite = 1;
    ValueTestFailures.writeThenReject = true;
    CHECK(SetParameterText(document, 0, parameter->GetID(), "Rejected").Status() == EditStatus::Refused);
    ValueTestFailures = {};
    CHECK(ParameterText(parameter) == "Before" && !document.Dirty() && history.CanRedo());

    reset();
    ValueTestFailures.failRead = 3;
    ValueTestFailures.failWrite = 2;
    CHECK(SetParameterText(document, 0, parameter->GetID(), "After").Status() == EditStatus::Error);
    ValueTestFailures = {};
    CHECK(ParameterText(parameter) == "After" && document.Dirty());
    CHECK(!history.CanUndo() && !history.CanRedo());

    reset();
    ValueTestFailures.failWrite = 3;
    ValueTestFailures.writeThenReject = true;
    CHECK(SetParameterText(document, 0, parameter->GetID(), "After").Status() == EditStatus::Error);
    ValueTestFailures = {};
    CHECK(ParameterText(parameter) == "Before" && !document.Dirty() && history.CanRedo());

    reset();
    CHECK(SetParameterText(document, 0, parameter->GetID(), "After").ok);
    ValueTestFailures = {};
    ValueTestFailures.failWrite = 1;
    ValueTestFailures.writeThenReject = true;
    CHECK(!history.Undo());
    ValueTestFailures = {};
    CHECK(ParameterText(parameter) == "After" && document.Dirty());
    CHECK(!history.CanUndo() && !history.CanRedo());

    reset();
    CHECK(SetParameterText(document, 0, parameter->GetID(), "Before").Unchanged());
    CHECK(!document.Dirty() && history.CanRedo());
    CHECK(SetParameterText(document, 0, parameter->GetID(), "").ok);
    CHECK(ParameterText(parameter).empty() && document.Dirty());
    CHECK(history.Undo() && ParameterText(parameter) == "Before" && !document.Dirty());
    CHECK(history.Redo() && ParameterText(parameter).empty() && document.Dirty());
}

} // namespace vdevtest
