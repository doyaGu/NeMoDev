#pragma once

#include <map>
#include <memory>

#include "core/document/EditResult.h"

#include "CKAll.h"

namespace vdev {

class Document;

// The objects' pictures (Dev.exe: a Snapshot Buffer image dropped on a
// Setup or on an object of the 3D Layout, sub_5D1E20; shown in the Setup):
// saved in a CKInterfaceObjectManager of GUID OBJECT_PICTURES_GUID
// (DoSaveObjects, sub_5D1C90), a chunk a picture: identifier 2, the object's
// ID, the bitmap; read back on load and merge (sub_5D1D60).
const CKGUID OBJECT_PICTURES_GUID(0x5D567DF5, 0x46CE6366);

using ObjectPictures = std::map<CK_ID, std::shared_ptr<void>>;

// Decodes the pictures stored in the runtime. Returned entries own their bitmaps.
ObjectPictures ReadObjectPictures(Document &document);

// Writes pictures (of the objects still there) into the composition's
// pictures object, made when there is none, before a save.
void WriteObjectPictures(Document &document, const ObjectPictures &pictures);

// Adopts an SDK bitmap (nullptr removes the picture), as one undoable edit.
EditResult SetObjectPicture(Document &document, CK_ID object, void *bitmap);

} // namespace vdev
