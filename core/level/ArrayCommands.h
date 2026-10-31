#pragma once
#include "core/document/EditResult.h"
#include <vector>
namespace vdev {
class Document;
EditResult InsertArrayColumn(Document &, CK_ID, int column, const std::string &name, CK_ARRAYTYPE, CKGUID);
EditResult RemoveArrayColumn(Document &, CK_ID, int column);
EditResult InsertArrayRow(Document &, CK_ID, int row = -1);
// The Array Setup's menus (Dev.exe sub_43F120, its commands in sub_43DE70):
// the column's, the rows' (the rows selected).
// Sorting takes integer, float and string columns only (documentation "Array Setup").
EditResult SortArray(Document &document, CK_ID array, int column, bool ascending);
bool SortableColumn(CKDataArray *array, int column);
// Make Key Column / Remove Key Column (-1).
EditResult SetArrayKeyColumn(Document &document, CK_ID array, int column);
// Move Column Left (step -1) / Right (+1).
EditResult MoveArrayColumn(Document &document, CK_ID array, int column, int step);
// Edit Column Properties: the column's name, its type (and parameter type).
EditResult EditArrayColumn(Document &document, CK_ID array, int column, const std::string &name, CK_ARRAYTYPE type,
                           const CKGUID &parameter);
// Delete Row: the rows; Duplicate Row: each a copy after the last row.
EditResult RemoveArrayRows(Document &document, CK_ID array, const std::vector<int> &rows);
EditResult DuplicateArrayRows(Document &document, CK_ID array, const std::vector<int> &rows);
// Move Row Up (step -1) / Down (+1): swapped with its neighbour.
EditResult MoveArrayRow(Document &document, CK_ID array, int row, int step);
// Edit / Edit Multiple: the text in the column of each row; refused when a
// key column has the value already ("Value Already in the Array!").
EditResult SetArrayCells(Document &document, CK_ID array, const std::vector<int> &rows, int column,
                         const std::string &text);
// Paste: the i-th parameter on the i-th row's cell of a parameter column
// when its type derives from the column's (SetElementValueFromParameter).
EditResult PasteArrayParameters(Document &document, CK_ID array, const std::vector<int> &rows, int column,
                                const std::vector<CK_ID> &parameters);
// Paste As Shortcut (Dev.exe sub_4413B0, CKDataArray::PasteShortcut): the
// i-th row's cell is the i-th parameter (another's, of a type the column
// takes); Remove Shortcut (sub_43DE70 2011, CKDataArray::RemoveShortcut):
// the cell is a copy of it, the array's. Undo gives each cell back its
// parameter (the array's own one is kept meanwhile, not destroyed as
// PasteShortcut does).
EditResult PasteArrayShortcuts(Document &document, CK_ID array, const std::vector<int> &rows, int column,
                               const std::vector<CK_ID> &parameters);
EditResult RemoveArrayShortcuts(Document &document, CK_ID array, const std::vector<int> &rows, int column);

} // namespace vdev
