#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/document/Document.h"

namespace vdev {

// Copied behaviors, Comments and parameters with their Script's display data.
// Retain the new sources before releasing the previous clipboard. Display data
// is destroyed before the runtime objects it may refer to.
class Clipboard {
    std::shared_ptr<ObjectReferences> m_Objects;

public:
    InterfaceData data;
    std::vector<CK_ID> behaviors;
    std::vector<Comment> comments;
    std::vector<CK_ID> parameters;
    std::vector<CK_ID> operations;
    // Attribute names (ANSI) for Paste Attributes; empty for other copies.
    std::vector<std::string> attributes;

    bool Empty() const {
        return behaviors.empty() && comments.empty() && parameters.empty() && operations.empty();
    }

    void Set(Document &document, Clipboard &&replacement) {
        std::vector<CK_ID> sources = replacement.behaviors;
        sources.insert(sources.end(), replacement.parameters.begin(), replacement.parameters.end());
        sources.insert(sources.end(), replacement.operations.begin(), replacement.operations.end());
        replacement.m_Objects = document.RetainObjects(sources);
        auto previousSources = std::move(m_Objects);
        *this = std::move(replacement);
    }

    void SetParameters(Document &document, std::vector<CK_ID> ids, std::vector<std::string> names = {}) {
        Clipboard replacement;
        replacement.parameters = std::move(ids);
        replacement.attributes = std::move(names);
        Set(document, std::move(replacement));
    }
};

// The Level Manager, 3D Layout and parameter windows share one native-object
// clipboard. Its contents and retention always change together.
class ObjectClipboard {
public:
    void SetObjects(Document &document, std::vector<CK_ID> objects) {
        Set(document, Kind::Objects, std::move(objects));
    }
    void SetScripts(Document &document, std::vector<CK_ID> scripts) {
        Set(document, Kind::Scripts, std::move(scripts));
    }
    const std::vector<CK_ID> &Objects() const { return Get(Kind::Objects); }
    const std::vector<CK_ID> &Scripts() const { return Get(Kind::Scripts); }

private:
    enum class Kind { Scripts, Objects };

    void Set(Document &document, Kind kind, std::vector<CK_ID> objects) {
        auto retained = document.RetainObjects(objects);
        m_Objects = std::move(objects);
        m_Kind = kind;
        m_Retained = std::move(retained);
    }
    const std::vector<CK_ID> &Get(Kind kind) const {
        static const std::vector<CK_ID> empty;
        return m_Kind == kind ? m_Objects : empty;
    }

    std::shared_ptr<ObjectReferences> m_Retained;
    std::vector<CK_ID> m_Objects;
    Kind m_Kind = Kind::Scripts;
};

} // namespace vdev
