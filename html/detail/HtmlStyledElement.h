#pragma once
#include <litehtml/html_tag.h>
#include <set>

// Preserve the native parser's presentational hints at their original cascade
// priority. litehtml's pseudo-class refresh otherwise clears attribute styles
// (cell heights, image dimensions, font colors and table spacing).
template <class Native> class HtmlStyledElement : public Native {
    litehtml::style m_Presentation;
    std::set<const litehtml::style *> m_UserAgentStyles;
    bool m_Parsed = false, m_Refreshing = false, m_Restored = false;

public:
    using Native::Native;
    void parse_attributes() override {
        Native::parse_attributes();
        m_Presentation = this->m_style;
        m_Parsed = true;
    }
    void add_style(const litehtml::style &style) override {
        if (!m_Parsed)
            m_UserAgentStyles.insert(&style);
        if (m_Refreshing) {
            // The captured hints already include the user-agent rules. Reapply
            // only author rules over them, preserving the native selector order.
            if (m_UserAgentStyles.count(&style))
                return;
            if (!m_Restored) {
                this->m_style = m_Presentation;
                m_Restored = true;
            }
        }
        Native::add_style(style);
    }
    void refresh_styles() override {
        m_Refreshing = true;
        m_Restored = false;
        Native::refresh_styles();
        if (!m_Restored)
            this->m_style = m_Presentation;
        m_Refreshing = false;
    }
};
