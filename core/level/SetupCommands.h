#pragma once

#include <string>
#include <vector>

#include "CKAll.h"
#include "core/document/EditResult.h"

namespace vdev {

class Document;

// The Setups' actions (not a property set to a value; ObjectProperties has
// those), each one undo step of the object's state (EditObjectState); no
// change: NoChange.

// 3D Object / 3D Frame / 3D Sprite Setup: Reset World Matrix (the identity)
// and Set as Unit (scale 1, the children kept where they are not).
EditResult ResetWorldMatrix(Document &document, CK_ID entity);
EditResult SetScaleToUnit(Document &document, CK_ID entity);
// Mesh Setup: Consolidate (CKMesh::Consolidate).
EditResult ConsolidateMesh(Document &document, CK_ID mesh);
// Texture Setup: Add Slot (an empty slot after the last, made current).
EditResult AddBitmapSlot(Document &document, CK_ID texture);
// Texture and 2D Sprite Setup: Remove Current Slot (the slot before it made
// current); refused with one slot.
EditResult RemoveCurrentSlot(Document &document, CK_ID bitmap);
// The Texture / 2D Sprite Setup's slot menu (Dev.exe sub_485C10 / sub_476FA0,
// their commands sub_487640 and sub_485AD0): each one undo step holding the
// images of every slot before and after (their pixels, sizes and file names,
// the slot count and the current slot: a texture's own state may keep only
// its file). Refused on a movie ("Cannot modify avi").
enum class SlotImageOp {
    InvertColors,      // Invert Slot: the current slot's RGB inverted
    NormalMap,         // HeightMap => NormalMap (VxConvertToNormalMap)
    BumpMap,           // HeightMap => BumpMap (VxConvertToBumpMap)
    AlphaOpaque,       // Set alpha to opaque (255)
    AlphaTranslucent,  // Set alpha to translucent (128)
    AlphaTransparent,  // Set alpha to transparent (0)
    InvertAlpha,       // Invert alpha
};
EditResult EditSlotImage(Document &document, CK_ID bitmap, SlotImageOp op);
// Add new slot: each file in a new slot after the last, the last made
// current; Replace slot: the file in the given slot (-1: current). Refused when a file
// cannot be read (nothing is changed then).
EditResult AddSlotImages(Document &document, CK_ID bitmap, const std::vector<std::string> &paths);
EditResult ReplaceSlotImage(Document &document, CK_ID bitmap, const std::string &path, int slot = -1);
// Reload slot: the current slot's file read again.
EditResult ReloadSlotImage(Document &document, CK_ID bitmap);
// Resize Slot (a texture's every slot, CKBitmapData::ResizeImages).
EditResult ResizeSlotImages(Document &document, CK_ID texture, int width, int height);
// Replace alpha: the image's channels (mask: 2 blue, 4 green, 8 red; their
// mean) as the current slot's alpha, the image scaled to the slot
// (sub_485630).
EditResult ReplaceSlotAlpha(Document &document, CK_ID texture, const std::string &path, int channels = 14, int slot = -1);
// Save slot / Save Alpha Channel: the current slot (or its alpha) in a file.
bool SaveSlotImage(CKObject *bitmap, const std::string &path, bool alpha, int slot = -1);

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
// A bitmap reader's save format (by its extension): its default
// properties; the 2.1 readers give none, then one made with the reader's
// GUID and extension and its options' default values (SetReaderOptionData
// from a parameter of their type). The bytes of a CKBitmapProperties
// (SetSaveFormat copies them); empty without such a reader.
std::vector<char> MakeSaveFormat(CKContext *context, const std::string &extension);

EditResult PasteArrayShortcuts(Document &document, CK_ID array, const std::vector<int> &rows, int column,
                               const std::vector<CK_ID> &parameters);
EditResult RemoveArrayShortcuts(Document &document, CK_ID array, const std::vector<int> &rows, int column);

// Grid Setup: a layer of a layer type, a classification (an attribute of
// the grid category), added or removed.
EditResult AddGridLayer(Document &document, CK_ID grid, int type);
EditResult RemoveGridLayer(Document &document, CK_ID grid, int type);
EditResult AddGridClassification(Document &document, CK_ID grid, int classification);
EditResult RemoveGridClassification(Document &document, CK_ID grid, int classification);
// The Grid Cross (documentation "Grid Setup": large arrows add columns /
// rows, small ones delete them): side 0 left, 1 right, 2 bottom (row 0), 3
// top; the squares keep their values and their place in the world (the
// grid moves by a square when the left or bottom side changes; its squares
// keep their size). The layers are made again (CKGrid::SetDimensions);
// one undo step. At least one column and one row stay.
EditResult ResizeGrid(Document &document, CK_ID grid, int side, bool add);
// The Global Layer List and Global Classification List (CKGridManager):
// layer types registered ("New Layer Type", a free name made from it),
// renamed (refused when the name is taken), their color and parameter
// (Integer, or Linker: CKPGUID_LINKERGRAPH_ENUM); classifications
// registered and renamed (attributes of the grid category). Manager data,
// not undone (the composition becomes modified). created: the type.
EditResult RegisterGridLayerType(Document &document, const std::string &name);
EditResult RenameGridLayerType(Document &document, int type, const std::string &name);
EditResult SetGridLayerTypeColor(Document &document, int type, const VxColor &color);
EditResult SetGridLayerTypeLinker(Document &document, int type, bool linker);
EditResult RegisterGridClassification(Document &document, const std::string &name);
EditResult RenameGridClassification(Document &document, int classification, const std::string &name);

} // namespace vdev
