#include "core/level/ArrayCommands.h"
#include "core/document/Document.h"
#include "core/document/Edit.h"
#include "core/runtime/CkRuntimeHost.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <stdexcept>

namespace vdev {
namespace {
using Chunk = std::unique_ptr<CKStateChunk, decltype(&DeleteCKStateChunk)>;
struct Column {
    std::string name;
    CK_ARRAYTYPE type;
    CKGUID parameter;
    bool operator==(const Column &b) const { return name == b.name && type == b.type && parameter == b.parameter; }
};
struct Cell {
    CKUINTPTR scalar = 0;
    std::string text;
    ObjectRef object{};
    bool operator==(const Cell &b) const { return scalar == b.scalar && text == b.text && object == b.object; }
};
struct Parameter {
    ObjectRef owner{};
    Chunk state{nullptr, DeleteCKStateChunk};
};
// DataArray's memory Save/Load embeds and recreates cell parameters. Keep
// table data separately so Undo restores the original owned and shortcut IDs.
void CaptureReferences(Document &doc, CKStateChunk &chunk, std::set<ObjectRef> &references, unsigned depth = 0) {
    if (depth > 256)
        throw std::runtime_error("Cell parameter nesting is too deep");
    chunk.StartRead();
    const auto remember = [&] {
        if (const auto ref = doc.Reference(chunk.ReadObjectID()))
            references.insert(ref);
    };
    if (auto *ids = chunk.GetIds())
        for (int i = 0; i < ids->Size; ++i) {
            if (ids->Data[i] >= 0) {
                chunk.Goto(ids->Data[i]);
                remember();
            } else if (++i < ids->Size) {
                chunk.Goto(ids->Data[i]);
                const int count = chunk.StartReadSequence();
                for (int j = 0; j < count; ++j)
                    remember();
            }
        }
    if (auto *chunks = chunk.GetChunks())
        for (int i = 0; i < chunks->Size; ++i) {
            int count = 1;
            if (chunks->Data[i] >= 0)
                chunk.Goto(chunks->Data[i]);
            else {
                if (++i >= chunks->Size)
                    break;
                chunk.Goto(chunks->Data[i]);
                count = chunk.StartReadSequence();
            }
            for (int j = 0; j < count; ++j) {
                Chunk child(chunk.ReadSubChunk(), DeleteCKStateChunk);
                if (child)
                    CaptureReferences(doc, *child, references, depth + 1);
            }
        }
}
struct ArrayState {
    std::vector<Column> columns;
    std::vector<std::vector<Cell>> rows;
    std::map<ObjectRef, Parameter> parameters;
    std::set<ObjectRef> references;
    int key = -1, sortColumn = 0;
    CKBOOL order = FALSE;
    void Capture(Document &doc, CKDataArray &array) {
        key = array.GetKeyColumn();
        order = array.m_Order;
        sortColumn = array.m_ColumnIndex;
        for (int c = 0; c < array.GetColumnCount(); ++c)
            columns.push_back({array.GetColumnName(c) ? array.GetColumnName(c) : "", array.GetColumnType(c),
                               array.GetColumnParameterGuid(c)});
        for (int r = 0; r < array.GetRowCount(); ++r) {
            std::vector<Cell> row;
            for (int c = 0; c < array.GetColumnCount(); ++c) {
                Cell cell;
                const auto raw = *array.GetElement(r, c);
                switch (columns[c].type) {
                case CKARRAYTYPE_STRING:
                    cell.text = raw ? reinterpret_cast<const char *>(raw) : "";
                    break;
                case CKARRAYTYPE_OBJECT:
                    cell.object = doc.Reference(CK_ID(raw));
                    break;
                case CKARRAYTYPE_PARAMETER:
                    if (auto *p = reinterpret_cast<CKParameter *>(raw)) {
                        cell.object = doc.Reference(p->GetID());
                        if (!parameters.count(cell.object)) {
                            Parameter saved;
                            saved.owner = p->GetOwner() ? doc.Reference(p->GetOwner()->GetID()) : ObjectRef{};
                            saved.state.reset(CKSaveObjectState(p));
                            if (!saved.state)
                                throw std::runtime_error("Cannot capture cell parameter");
                            CaptureReferences(doc, *saved.state, references);
                            parameters.emplace(cell.object, std::move(saved));
                        }
                    }
                    break;
                default:
                    cell.scalar = raw;
                    break;
                }
                row.push_back(std::move(cell));
            }
            rows.push_back(std::move(row));
        }
    }
    bool Same(ArrayState &b) {
        if (columns != b.columns || rows != b.rows || key != b.key || parameters.size() != b.parameters.size())
            return false;
        for (auto &entry : parameters) {
            auto p = b.parameters.find(entry.first);
            if (p == b.parameters.end() || p->second.owner != entry.second.owner)
                return false;
            auto &x = entry.second.state;
            auto &y = p->second.state;
            x->StartRead();
            y->StartRead();
            if (x->GetDataSize() != y->GetDataSize() ||
                (x->GetDataSize() && std::memcmp(x->LockReadBuffer(), y->LockReadBuffer(), x->GetDataSize())))
                return false;
        }
        return true;
    }
    bool Restore(Document &doc, CKDataArray &array) {
        // Resolve every identity before touching the table.
        for (auto ref : references)
            if (!doc.Resolve(ref, CKCID_OBJECT, Document::ObjectScope::Existing))
                return false;
        for (const auto &row : rows)
            for (const auto &cell : row)
                if (cell.object && !doc.Resolve(cell.object, CKCID_OBJECT, Document::ObjectScope::Existing))
                    return false;
        for (const auto &entry : parameters)
            if (entry.second.owner && !doc.Resolve(entry.second.owner, CKCID_OBJECT, Document::ObjectScope::Existing))
                return false;
        array.Clear(FALSE); // parameters remain owned by their history step
        while (array.GetColumnCount())
            array.RemoveColumn(array.GetColumnCount() - 1);
        for (const auto &col : columns)
            array.InsertColumn(-1, col.type, const_cast<char *>(col.name.c_str()), col.parameter);
        // Native row storage is public. Building the rows directly avoids
        // creating and destroying placeholder parameter objects on every Undo.
        for (const auto &saved : rows) {
            auto row = std::make_unique<CKDataRow>();
            row->Resize(int(columns.size()));
            array.m_DataMatrix.PushBack(row.get());
            row.release();
            auto *native = array.m_DataMatrix.Back();
            for (size_t c = 0; c < columns.size(); ++c) {
                const auto &cell = saved[c];
                switch (columns[c].type) {
                case CKARRAYTYPE_STRING:
                    (*native)[int(c)] = reinterpret_cast<CKUINTPTR>(CKStrdup(cell.text.c_str()));
                    break;
                case CKARRAYTYPE_OBJECT:
                    (*native)[int(c)] = cell.object ? doc.NativeId(cell.object, Document::ObjectScope::Existing) : 0;
                    break;
                case CKARRAYTYPE_PARAMETER:
                    (*native)[int(c)] = reinterpret_cast<CKUINTPTR>(
                        doc.Resolve(cell.object, CKCID_PARAMETER, Document::ObjectScope::Existing));
                    break;
                default:
                    (*native)[int(c)] = cell.scalar;
                    break;
                }
            }
        }
        for (auto &entry : parameters) {
            auto *p = CKParameter::Cast(doc.Resolve(entry.first, CKCID_PARAMETER, Document::ObjectScope::Existing));
            if (!p || CKReadObjectState(p, entry.second.state.get()) != CK_OK)
                return false;
            p->SetOwner(doc.Resolve(entry.second.owner, CKCID_OBJECT, Document::ObjectScope::Existing));
        }
        array.SetKeyColumn(key);
        array.m_Order = order;
        array.m_ColumnIndex = sortColumn;
        return true;
    }
    void Retain(const Document &doc, std::set<CK_ID> &objects) const {
        for (auto ref : references)
            if (const auto id = doc.NativeId(ref, Document::ObjectScope::Existing))
                objects.insert(id);
        for (const auto &row : rows)
            for (const auto &cell : row)
                if (const CK_ID id = doc.NativeId(cell.object, Document::ObjectScope::Existing))
                    objects.insert(id);
        for (const auto &entry : parameters)
            if (const CK_ID id = doc.NativeId(entry.second.owner, Document::ObjectScope::Existing))
                objects.insert(id);
    }
};
class ArrayAction final : public Action {
  public:
    ObjectRef array{};
    ArrayState before, after;
    bool Redo(Document &doc) override { return Restore(doc, after); }
    bool Undo(Document &doc) override { return Restore(doc, before); }
    void RetainObjects(const Document &doc, std::set<CK_ID> &objects) const override {
        if (const auto id = doc.NativeId(array, Document::ObjectScope::Existing))
            objects.insert(id);
        before.Retain(doc, objects);
        after.Retain(doc, objects);
    }

  private:
    bool Restore(Document &doc, ArrayState &state) {
        auto *a = CKDataArray::Cast(doc.Resolve(array, CKCID_DATAARRAY, Document::ObjectScope::Existing));
        return a && state.Restore(doc, *a);
    }
};
// Native row/column removal normally destroys owned parameters immediately.
// Detach only while applying the change; ownership is restored even on failure.
class KeepParameters {
  public:
    KeepParameters(Document &doc, CKDataArray &array, const ArrayState &state, bool protect)
        : doc(doc), array(array.GetID()) {
        if (!protect)
            return;
        for (const auto &entry : state.parameters)
            if (entry.second.owner == doc.Reference(this->array)) {
                owned.push_back(entry.first);
            }
    }
    void Detach() {
        for (auto ref : owned)
            if (auto *p = CKParameter::Cast(doc.Resolve(ref)))
                p->SetOwner(nullptr);
    }
    void Restore() {
        for (auto ref : owned)
            if (auto *p = CKParameter::Cast(doc.Resolve(ref, CKCID_PARAMETER, Document::ObjectScope::Existing)))
                p->SetOwner(doc.Context()->GetObject(array));
    }

  private:
    Document &doc;
    CK_ID array;
    std::vector<ObjectRef> owned;
};
CKDataArray *ArrayOf(Document &doc, CK_ID id) {
    return CKDataArray::Cast(doc.Resolve(doc.Reference(id), CKCID_DATAARRAY));
}
bool ValidColumn(CKDataArray *a, int c) { return a && c >= 0 && c < a->GetColumnCount(); }
struct Rejected : std::runtime_error {
    using std::runtime_error::runtime_error;
};
EditResult EditArray(Document &doc, CK_ID id, const std::string &label, const std::function<void(CKObject *)> &change,
                     bool protect = false) {
    auto *array = ArrayOf(doc, id);
    if (!array)
        return EditResult::Refused("The array is gone");
    auto action = std::make_unique<ArrayAction>();
    action->array = doc.Reference(id);
    if (!GuardedCall([&] { action->before.Capture(doc, *array); }, &doc.Host(), "Capture array"))
        return EditResult::Error("Cannot capture array");
    std::string refused;
    KeepParameters keep(doc, *array, action->before, protect);
    const bool called = GuardedCall(
        [&] {
            keep.Detach();
            try {
                change(array);
            } catch (const Rejected &e) {
                refused = e.what();
            }
        },
        &doc.Host(), label.c_str());
    // Finalize outside the protected call as native faults need not unwind
    // C++ stack objects on every supported platform.
    const bool owners = GuardedCall([&] { keep.Restore(); }, &doc.Host(), "Restore cell owners");
    const bool captured = GuardedCall([&] { action->after.Capture(doc, *array); }, &doc.Host(), "Capture array result");
    const bool ok = called && owners && captured;
    if (!ok || !refused.empty()) {
        const bool restored = GuardedCall(
            [&] {
                if (!action->before.Restore(doc, *array))
                    throw std::runtime_error("Cannot restore array");
            },
            &doc.Host(), "Restore array");
        // New cell parameters do not belong to a failed change.
        for (const auto &p : action->after.parameters)
            if (!action->before.parameters.count(p.first))
                if (auto *object = doc.Resolve(p.first, CKCID_PARAMETER, Document::ObjectScope::Existing))
                    doc.Context()->DestroyObject(object);
        if (!restored) {
            doc.InvalidateHistory();
            return EditResult::Error("Array edit failed and recovery was incomplete");
        }
        return ok ? EditResult::Refused(refused) : EditResult::Error("Array edit failed");
    }
    if (action->before.Same(action->after))
        return EditResult::NoChange();
    Edit edit(doc, label);
    for (const auto &p : action->before.parameters)
        if (!action->after.parameters.count(p.first) && p.second.owner == action->array)
            edit.Bury(doc.NativeId(p.first));
    for (const auto &p : action->after.parameters)
        if (!action->before.parameters.count(p.first) && p.second.owner == action->array)
            edit.Created(doc.NativeId(p.first));
    edit.Done(std::move(action));
    return edit.Commit();
}
void RequireRows(CKDataArray *a, const std::vector<int> &rows) {
    if (!a || rows.empty())
        throw Rejected("Select rows");
    for (int row : rows)
        if (row < 0 || row >= a->GetRowCount())
            throw Rejected("No such row");
}
} // namespace
bool SortableColumn(CKDataArray *a, int c) {
    return ValidColumn(a, c) && (a->GetColumnType(c) == CKARRAYTYPE_INT || a->GetColumnType(c) == CKARRAYTYPE_FLOAT ||
                                 a->GetColumnType(c) == CKARRAYTYPE_STRING);
}
EditResult SortArray(Document &d, CK_ID id, int col, bool ascending) {
    if (!SortableColumn(ArrayOf(d, id), col))
        return EditResult::Refused("Only integer, float and string columns can be sorted");
    return EditArray(d, id, ascending ? "Sort Ascending" : "Sort Descending",
                     [=](CKObject *o) { CKDataArray::Cast(o)->Sort(col, ascending); });
}
EditResult SetArrayKeyColumn(Document &d, CK_ID id, int col) {
    auto *a = ArrayOf(d, id);
    if (!a || (col != -1 && !ValidColumn(a, col)))
        return EditResult::Refused("No such column");
    return EditArray(d, id, col < 0 ? "Remove Key Column" : "Make Key Column",
                     [=](CKObject *o) { CKDataArray::Cast(o)->SetKeyColumn(col); });
}
EditResult MoveArrayColumn(Document &d, CK_ID id, int col, int step) {
    auto *a = ArrayOf(d, id);
    if (!ValidColumn(a, col) || !ValidColumn(a, col + step) || (step != 1 && step != -1))
        return EditResult::Refused("The column cannot move further");
    return EditArray(d, id, step < 0 ? "Move Column Left" : "Move Column Right", [=](CKObject *o) {
        auto *a = CKDataArray::Cast(o);
        if (step < 0)
            a->MoveColumn(col, col - 1);
        else if (col + 1 == a->GetColumnCount() - 1)
            a->MoveColumn(col, -1);
        else
            a->MoveColumn(col + 1, col);
    });
}
static bool ValidType(Document &d, CK_ARRAYTYPE type, CKGUID parameter) {
    if (type < CKARRAYTYPE_INT || type > CKARRAYTYPE_PARAMETER)
        return false;
    if (type != CKARRAYTYPE_PARAMETER)
        return true;
    auto *description = d.Context()->GetParameterManager()->GetParameterTypeDescription(parameter);
    return description && description->Valid;
}
EditResult InsertArrayColumn(Document &d, CK_ID id, int col, const std::string &name, CK_ARRAYTYPE type,
                             CKGUID parameter) {
    auto *a = ArrayOf(d, id);
    if (!a || col < -1 || col >= a->GetColumnCount() || !ValidType(d, type, parameter))
        return EditResult::Refused("Invalid column");
    return EditArray(d, id, "Insert Column", [=](CKObject *o) {
        CKDataArray::Cast(o)->InsertColumn(col, type, const_cast<char *>(name.c_str()), parameter);
    });
}
EditResult RemoveArrayColumn(Document &d, CK_ID id, int col) {
    if (!ValidColumn(ArrayOf(d, id), col))
        return EditResult::Refused("No such column");
    return EditArray(d, id, "Delete Column", [=](CKObject *o) { CKDataArray::Cast(o)->RemoveColumn(col); }, true);
}
EditResult EditArrayColumn(Document &d, CK_ID id, int col, const std::string &name, CK_ARRAYTYPE type,
                           const CKGUID &parameter) {
    if (!ValidColumn(ArrayOf(d, id), col) || !ValidType(d, type, parameter))
        return EditResult::Refused("Invalid column");
    return EditArray(
        d, id, "Edit Column Properties",
        [=](CKObject *o) {
            auto *a = CKDataArray::Cast(o);
            if (a->GetColumnType(col) != type ||
                (type == CKARRAYTYPE_PARAMETER && a->GetColumnParameterGuid(col) != parameter))
                a->SetColumnType(col, type, parameter);
            a->SetColumnName(col, const_cast<char *>(name.c_str()));
        },
        true);
}
EditResult InsertArrayRow(Document &d, CK_ID id, int row) {
    auto *a = ArrayOf(d, id);
    if (!a || !a->GetColumnCount() || row < -1 || row > a->GetRowCount())
        return EditResult::Refused("Invalid row");
    return EditArray(d, id, row == -1 ? "Add Row" : "Insert Row", [=](CKObject *o) {
        if (!CKDataArray::Cast(o)->InsertRow(row))
            throw Rejected("Cannot insert row");
    });
}
EditResult RemoveArrayRows(Document &d, CK_ID id, const std::vector<int> &rows) {
    auto sorted = rows;
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    return EditArray(
        d, id, "Delete Row",
        [=](CKObject *o) {
            auto *a = CKDataArray::Cast(o);
            RequireRows(a, sorted);
            for (auto it = sorted.rbegin(); it != sorted.rend(); ++it)
                a->RemoveRow(*it);
        },
        true);
}
EditResult DuplicateArrayRows(Document &d, CK_ID id, const std::vector<int> &rows) {
    return EditArray(d, id, "Duplicate Row", [=](CKObject *o) {
        auto *a = CKDataArray::Cast(o);
        RequireRows(a, rows);
        for (int r : rows) {
            if (!a->InsertRow(-1))
                throw Rejected("Cannot duplicate row");
            const int copy = a->GetRowCount() - 1;
            for (int c = 0; c < a->GetColumnCount(); ++c) {
                const auto raw = *a->GetElement(r, c);
                if (a->GetColumnType(c) == CKARRAYTYPE_STRING) {
                    *a->GetElement(copy, c) =
                        raw ? reinterpret_cast<CKUINTPTR>(CKStrdup(reinterpret_cast<const char *>(raw))) : 0;
                } else if (a->GetColumnType(c) == CKARRAYTYPE_PARAMETER) {
                    auto *from = reinterpret_cast<CKParameter *>(raw);
                    auto *to = reinterpret_cast<CKParameter *>(*a->GetElement(copy, c));
                    if (from && (!to || to->CopyValue(from, FALSE) != CK_OK))
                        throw Rejected("Cannot copy parameter");
                } else
                    *a->GetElement(copy, c) = raw;
            }
        }
    });
}
EditResult MoveArrayRow(Document &d, CK_ID id, int row, int step) {
    auto *a = ArrayOf(d, id);
    if (!a || row < 0 || row >= a->GetRowCount() || row + step < 0 || row + step >= a->GetRowCount() ||
        (step != 1 && step != -1))
        return EditResult::Refused("The row cannot move further");
    return EditArray(d, id, step < 0 ? "Move Row Up" : "Move Row Down",
                     [=](CKObject *o) { CKDataArray::Cast(o)->SwapRows(row, row + step); });
}
EditResult SetArrayCells(Document &d, CK_ID id, const std::vector<int> &rows, int col, const std::string &text) {
    if (!ValidColumn(ArrayOf(d, id), col))
        return EditResult::Refused("No such column");
    return EditArray(d, id, rows.size() > 1 ? "Edit Multiple" : "Edit Array", [&, col](CKObject *o) {
        auto *a = CKDataArray::Cast(o);
        RequireRows(a, rows);
        for (int row : rows) {
            // The native text setter returns FALSE even after writing object
            // and parameter cells. Use their native setters' actual result.
            if (a->GetColumnType(col) == CKARRAYTYPE_OBJECT) {
                if (!a->SetElementObject(row, col,
                                         d.Context()->GetObjectByName(const_cast<char *>(text.c_str()), nullptr)))
                    throw Rejected("Cannot set object cell");
            } else if (a->GetColumnType(col) == CKARRAYTYPE_PARAMETER) {
                auto *p = reinterpret_cast<CKParameter *>(*a->GetElement(row, col));
                if (!p || p->SetStringValue(const_cast<char *>(text.c_str())) != CK_OK)
                    throw Rejected("Cannot set parameter cell");
            } else if (!a->SetElementStringValue(row, col, const_cast<char *>(text.c_str())))
                throw Rejected("Value refused by the array");
        }
    });
}
EditResult PasteArrayParameters(Document &d, CK_ID id, const std::vector<int> &rows, int col,
                                const std::vector<CK_ID> &sources) {
    auto *a = ArrayOf(d, id);
    if (!ValidColumn(a, col) || a->GetColumnType(col) != CKARRAYTYPE_PARAMETER || sources.empty())
        return EditResult::Refused("Nothing to paste!");
    return EditArray(d, id, "Paste", [&](CKObject *o) {
        auto *a = CKDataArray::Cast(o);
        RequireRows(a, rows);
        for (size_t i = 0; i < rows.size() && i < sources.size(); ++i) {
            auto *p = CKParameter::Cast(
                d.Resolve(d.Reference(sources[i]), CKCID_PARAMETER, Document::ObjectScope::Existing));
            if (!p ||
                !d.Context()->GetParameterManager()->IsDerivedFrom(p->GetGUID(), a->GetColumnParameterGuid(col)) ||
                (!reinterpret_cast<CKParameter *>(*a->GetElement(rows[i], col)) ||
                 reinterpret_cast<CKParameter *>(*a->GetElement(rows[i], col))->CopyValue(p, TRUE) != CK_OK))
                throw Rejected("Cannot paste parameter");
        }
    });
}
EditResult PasteArrayShortcuts(Document &d, CK_ID id, const std::vector<int> &rows, int col,
                               const std::vector<CK_ID> &sources) {
    auto *a = ArrayOf(d, id);
    if (!ValidColumn(a, col) || a->GetColumnType(col) != CKARRAYTYPE_PARAMETER || sources.empty())
        return EditResult::Refused("Nothing to paste!");
    for (CK_ID source : sources) {
        auto *p = CKParameter::Cast(d.Resolve(d.Reference(source), CKCID_PARAMETER));
        if (!p || p->GetOwner() == a ||
            !d.Context()->GetParameterManager()->IsDerivedFrom(p->GetGUID(), a->GetColumnParameterGuid(col)))
            return EditResult::Refused("Invalid shortcut");
    }
    return EditArray(
        d, id, "Paste As Shortcut",
        [&](CKObject *o) {
            auto *a = CKDataArray::Cast(o);
            RequireRows(a, rows);
            for (size_t i = 0; i < rows.size() && i < sources.size(); ++i)
                if (!a->PasteShortcut(rows[i], col, CKParameter::Cast(d.Context()->GetObject(sources[i]))))
                    throw Rejected("Cannot paste shortcut");
        },
        true);
}
EditResult RemoveArrayShortcuts(Document &d, CK_ID id, const std::vector<int> &rows, int col) {
    auto *a = ArrayOf(d, id);
    if (!ValidColumn(a, col) || a->GetColumnType(col) != CKARRAYTYPE_PARAMETER)
        return EditResult::Refused("Not a parameter column");
    std::vector<int> shortcuts;
    for (int r : rows) {
        if (r < 0 || r >= a->GetRowCount())
            return EditResult::Refused("No such row");
        auto *p = reinterpret_cast<CKParameter *>(*a->GetElement(r, col));
        if (p && p->GetOwner() != a)
            shortcuts.push_back(r);
    }
    if (shortcuts.empty())
        return EditResult::Refused("No shortcut");
    return EditArray(d, id, "Remove Shortcut", [=](CKObject *o) {
        auto *a = CKDataArray::Cast(o);
        for (int row : shortcuts)
            if (!a->RemoveShortcut(row, col))
                throw Rejected("Cannot remove shortcut");
    });
}
} // namespace vdev
