#pragma once
#include "workspace/EditorClient.h"
#include <limits>

namespace vdev {
// A view keeps a complete, read-only preferences projection. Stable frames
// read these copied values; only a changed provider version fetches a new copy.
class PreferencesData {
  public:
    EditorStatus Refresh(const EditorClient &client) {
        const auto version = client.Revision("preferences.display");
        if (!version) return EDITOR_REFUSED;
        if (version == m_Version) return EDITOR_OK;
        const auto response = client.Query("preferences.display");
        const auto *value = EditorClient::Find(response, "value");
        if (response.status != EDITOR_OK || !value || value->type != EDITOR_RECORD) return EDITOR_INVALID;
        m_Values = value->fields;
        m_Version = version;
        return EDITOR_OK;
    }
    int Int(const char *key, int fallback = 0) const {
        for (const auto &field : m_Values)
            if (field.key == key && field.type == EDITOR_INTEGER_VALUE && int64_t(field.integer) >= std::numeric_limits<int>::min() &&
                int64_t(field.integer) <= std::numeric_limits<int>::max()) return int(int64_t(field.integer));
        return fallback;
    }
    bool Bool(const char *key, bool fallback = false) const { return Int(key, fallback) != 0; }
    std::string Text(const char *key) const {
        for (const auto &field : m_Values) if (field.key == key && field.type == EDITOR_TEXT_VALUE) return field.text;
        return {};
    }
    const std::vector<EditorField> &Values() const { return m_Values; }
  private:
    uint64_t m_Version = 0;
    std::vector<EditorField> m_Values;
};
} // namespace vdev
