#pragma once

#include "application/EditorApplication.h"
#include <algorithm>
#include <set>
#include <utility>

namespace vdev {
struct EditorApplication::CachedQuery {
    explicit CachedQuery(EditorResponse result);
    const EditorResponse response;
    const EditorFieldValues values;
};

class EditorApplication::QueryState {
  public:
    QueryState() = default;
    QueryState(const QueryState &) = delete;
    QueryState &operator=(const QueryState &) = delete;
    const std::shared_ptr<const CachedQuery> *Find(const EditorQueryDesc *query, EditorScope scope,
                                                   const std::vector<EditorField> &arguments, uint64_t version) const {
        const auto found = m_Cache.find(query);
        if (found == m_Cache.end()) return nullptr;
        for (const auto &entry : found->second)
            if (Same(entry, scope, arguments) && entry.version == version) return &entry.result;
        return nullptr;
    }
    const std::shared_ptr<const CachedQuery> &Store(const EditorQueryDesc *query, EditorScope scope,
                                                    std::vector<EditorField> arguments, uint64_t version,
                                                    EditorResponse response) {
        auto result = std::make_shared<const CachedQuery>(std::move(response));
        auto &entries = m_Cache[query];
        for (auto &entry : entries)
            if (Same(entry, scope, arguments)) {
                entry.result = std::move(result);
                entry.version = version;
                return entry.result;
            }
        if (entries.size() >= 256) entries.erase(entries.begin());
        entries.push_back({scope, std::move(arguments), std::move(result), version});
        return entries.back().result;
    }
    const EditorResponse &Transient(EditorResponse result) {
        m_Transient = std::move(result);
        return m_Transient;
    }
    bool IsTransient(const EditorResponse &result) const { return &result == &m_Transient; }
    EditorResponse TakeTransient() { return std::exchange(m_Transient, {}); }
    void Invalidate() { m_Cache.clear(); }
    void Changed() {
        Invalidate();
        ++m_Published;
    }
    uint64_t Revision() const { return m_Published; }
    uint64_t Version(const std::string &id, const EditorQueryDesc &source, uint64_t value) {
        auto &revision = m_Versions[id];
        if (revision.source != &source || revision.value != value) {
            revision.source = &source;
            revision.value = value;
            ++revision.published;
        }
        return revision.published;
    }
    void Subscribe(EditorSubscription id, EditorScope scope, EditorChanged changed, void *data) {
        m_Watches.emplace(id, Watch{scope, changed, data});
    }
    void Unsubscribe(EditorSubscription id) { m_Watches.erase(id); }
    void DocumentChanged(DocumentToken document) {
        Changed();
        for (auto &entry : m_Watches)
            entry.second.scope.document = document;
    }
    void CloseScope(EditorView view) {
        m_Transient = {};
        for (auto &query : m_Cache)
            query.second.erase(std::remove_if(query.second.begin(), query.second.end(),
                                              [view](const Cached &entry) { return entry.scope.view == view; }),
                               query.second.end());
        for (auto it = m_Watches.begin(); it != m_Watches.end();)
            if (it->second.scope.view == view)
                it = m_Watches.erase(it);
            else
                ++it;
    }
    bool Notifying() const { return m_Notifying; }
    void Notify(const EditorHost *host, const std::set<EditorView> &scopes) {
        if (m_Delivered == m_Published || m_Notifying) return;
        m_Delivered = m_Published;
        std::vector<EditorSubscription> notices;
        for (const auto &entry : m_Watches)
            notices.push_back(entry.first);
        struct Notification {
            bool &active;
            explicit Notification(bool &active) : active(active) { active = true; }
            ~Notification() { active = false; }
        } notifying(m_Notifying);
        for (auto id : notices) {
            const auto found = m_Watches.find(id);
            if (found == m_Watches.end() || !scopes.count(found->second.scope.view)) continue;
            const auto watcher = found->second;
            const EditorChange change{watcher.scope, m_Published};
            try {
                watcher.changed(watcher.data, host, &change);
            } catch (...) {
                m_Watches.erase(id);
            }
        }
    }
    void Clear() {
        m_Cache.clear();
        m_Versions.clear();
        m_Watches.clear();
        m_Transient = {};
    }

  private:
    struct Cached {
        EditorScope scope;
        std::vector<EditorField> arguments;
        std::shared_ptr<const CachedQuery> result;
        uint64_t version;
    };
    static bool Same(const Cached &entry, EditorScope scope, const std::vector<EditorField> &arguments) {
        return entry.scope.document == scope.document && entry.scope.view == scope.view && entry.arguments == arguments;
    }
    struct Versioned {
        const EditorQueryDesc *source = nullptr;
        uint64_t value = 0, published = 0;
    };
    struct Watch {
        EditorScope scope;
        EditorChanged changed;
        void *data;
    };
    std::map<const EditorQueryDesc *, std::vector<Cached>> m_Cache;
    std::map<std::string, Versioned> m_Versions;
    EditorResponse m_Transient;
    std::map<EditorSubscription, Watch> m_Watches;
    uint64_t m_Published = 1, m_Delivered = 0;
    bool m_Notifying = false;
};
} // namespace vdev
