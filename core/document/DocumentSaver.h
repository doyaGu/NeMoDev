#pragma once

#include <map>
#include <string>
#include <vector>

#include "core/layout/LayoutInterface.h"

namespace vdev {

class Document;

struct SaveReport {
    // True only after the verified composition was published at the requested path.
    bool saved = false;
    int chunksWritten = 0;
    int scriptsVerified = 0;
    int parametersVerified = 0;
    int spriteTextsVerified = 0;
    std::vector<std::string> errors;
    std::vector<std::string> notes;
};

struct SaveOptions {
    // The loaded source is never overwritten; another existing file only
    // with this flag (the UI asks first).
    bool allowOverwrite = false;
    // Dev's Save Script Snapshot (Schematic preference; Dev.exe
    // SaveScriptHeader writes the header's bitmap only with it): without it
    // the Scripts are saved with no snapshot, the untouched ones too.
    bool saveScriptSnapshots = true;
    // CK_FILE_WRITEMODE flags added to the context's for this save: Dev.exe's
    // Export to Player (sub_5C6400) adds CKFILE_FORVIEWER and
    // CKFILE_WHOLECOMPRESSED, Create Web Page (sub_598700) CKFILE_WHOLECOMPRESSED
    // and CKFILE_FORVIEWER unless Save as Cmo.
    unsigned int writeMode = 0;
    // An export leaves the composition as it was: not marked saved.
    bool markSaved = true;
};

// Writes a whole composition (tools/InterfaceChunkProjectWriter.cpp flow):
// every edited Script session is validated, serialized and reloaded before
// any chunk is installed; untouched Scripts keep their original chunk bytes.
// A temporary file in the destination directory is loaded in a separate CKContext,
// where parameter types, object references, Sprite Text alignment and every rewritten
// InterfaceChunk are checked (a player file loads outside interface mode and has no InterfaceChunk).
// Only the verified file replaces the destination. Failure retains its previous
// contents and does not mark the document saved; notes report cleanup failures.
bool SaveDocumentAs(Document &document, const std::string &path, const SaveOptions &options,
                    SaveReport &report);

// The editor's own data saved with the composition (Dev.exe DoSaveObjects):
// the 3D Layout's interface data (LayoutInterface) and the objects' pictures
// (ObjectPictures); null: kept as the composition has them.
struct CompositionData {
    const LayoutInterfaceData *layout = nullptr;
    const std::map<CK_ID, void *> *pictures = nullptr;
};

// Save, Save As, the exports: data written into the composition (it stays
// there, saved or not), then SaveDocumentAs.
bool SaveComposition(Document &document, const std::string &path, const CompositionData &data,
                     const SaveOptions &options, SaveReport &report);

} // namespace vdev
