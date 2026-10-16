#pragma once

#include <map>

#include "CKAll.h"

namespace vdev {

class Document;

// The objects' pictures (Dev.exe: a Snapshot Buffer image dropped on a
// Setup or on an object of the 3D Layout, sub_5D1E20; shown in the Setup):
// saved in a CKInterfaceObjectManager of GUID OBJECT_PICTURES_GUID
// (DoSaveObjects, sub_5D1C90), a chunk a picture: identifier 2, the object's
// ID, the bitmap; read back on load and merge (sub_5D1D60).
const CKGUID OBJECT_PICTURES_GUID(0x5D567DF5, 0x46CE6366);

// The pictures of the composition's interface objects (bitmap handles the
// document keeps until it closes).
std::map<CK_ID, void *> ReadObjectPictures(Document &document);

// Writes pictures (of the objects still there) into the composition's
// pictures object, made when there is none, before a save.
void WriteObjectPictures(Document &document, const std::map<CK_ID, void *> &pictures);

} // namespace vdev
