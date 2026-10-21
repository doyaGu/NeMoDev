// The File menu's Save Version, Revert to Saved, Export to Virtools Player
// and Create Web Page (Dev.exe sub_5C8BC0, sub_5C8ED0, sub_5C6400,
// sub_5C7240 and its dialog 710), and Dev's message box (sub_5D5E60).
#include "ui/app/EditorApp.h"


#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "imgui.h"

#include "ui/controls/DevBitmaps.h"
#include "ui/controls/DevControls.h"
#include "core/document/DocumentSaver.h"
#include "core/document/FileCommands.h"
#include "ui/controls/FileDialogs.h"
#include "ui/app/DesktopFiles.h"
#include "core/runtime/TextEncoding.h"

namespace vdev {

namespace {

namespace fs = std::filesystem;

bool Exists(const std::string &path) {
    std::error_code ec;
    return fs::exists(fs::u8path(path), ec);
}

std::string Format(const char *format, const std::string &value) {
    char text[1024];
    std::snprintf(text, sizeof(text), format, value.c_str());
    return text;
}

// Dev's icon types (sub_5A0B10): resvis.dll's 32 x 32 icons.
int MessageIconId(int type) {
    static const int ICONS[7] = {0, 0x529, 0x528, 0x52A, 0x525, 0x527, 0x526};
    return type >= 1 && type <= 6 ? ICONS[type] : 0;
}

} // namespace

std::string EditorApp::CurrentFilePath() const {
    if (!m_LastSavedPath.empty()) return m_LastSavedPath;
    return m_State.HasDocument() ? m_State.document->SourcePath() : std::string();
}

// Save Version (sub_5C8BC0): the composition under its name's next version,
// "File Exists" first when that file is there; without a file, Save As.
void EditorApp::SaveVersion() {
    if (!m_State.HasDocument()) return;
    const std::string current = CurrentFilePath();
    if (current.empty()) {
        SaveAs();
        return;
    }
    const std::string versioned = VersionedPath(current, 1);
    if (versioned.empty()) return;
    auto save = [this, versioned] {
        if (m_State.HasDocument()) SaveTo(versioned, true);
    };
    if (!Exists(versioned)) {
        save();
        return;
    }
    DevMessage message;
    message.text = Format("%s Exists! Do you want to overwrite it ?", versioned);
    message.title = "File Exists";
    message.icon = 6;
    message.cancel = true;
    message.onOk = save;
    ShowMessage(std::move(message));
}

// Revert to Saved (sub_5C8ED0): the version before the composition's name,
// after a confirmation (none for unsaved changes, as in Dev).
void EditorApp::RevertToSaved() {
    const std::string current = CurrentFilePath();
    if (current.empty()) return;
    const std::string previous = VersionedPath(current, -1);
    if (previous.empty()) return;
    DevMessage message;
    message.title = "Revert to Saved";
    if (!Exists(previous)) {
        message.text = Format("%s does not exists", previous);
        message.icon = 5;
        ShowMessage(std::move(message));
        return;
    }
    message.text = Format("Revert to last saved version of %s?", current);
    message.icon = 2;
    message.cancel = true;
    message.onOk = [this, previous] { OpenFile(previous); };
    ShowMessage(std::move(message));
}

bool EditorApp::ExportTo(const std::string &path, unsigned int writeMode, std::string *error) {
    if (!m_State.HasDocument()) return false;
    SaveOptions options;
    options.saveScriptSnapshots = m_State.preferences.Bool("Schematic\\Save Script Snapshot");
    options.allowOverwrite = true; // asked before
    options.writeMode = writeMode;
    options.markSaved = false;
    const LayoutInterfaceData layout = LayoutDataToSave();
    SaveReport report;
    const bool saved = SaveComposition(*m_State.document, path, CompositionToSave(layout), options, report);
    std::string text = (saved ? "Exported " : "Export failed: ") + path;
    text += "\nParameters verified: " + std::to_string(report.parametersVerified);
    for (const auto &message : report.errors) text += "\nError: " + AnsiToUtf8(message);
    for (const auto &note : report.notes) text += "\nNote: " + AnsiToUtf8(note);
    m_State.Log(saved ? LogLevel::Info : LogLevel::Error, text);
    if (error) *error = report.errors.empty() ? std::string() : AnsiToUtf8(report.errors.front());
    UpdateTitle();
    return saved;
}

// Export to Virtools Player (sub_5C6400): a .vmo next to the composition's
// file by default, saved for the player (CKFILE_FORVIEWER) and compressed.
void EditorApp::ExportToPlayer() {
    if (!m_State.HasDocument()) return;
    std::string initial = CurrentFilePath();
    if (initial.size() >= 3) initial[initial.size() - 3] = 'v'; // .cmo: .vmo
    FileDialogOptions options;
    options.save = true;
    options.owner = m_Window;
    options.filter = L"Virtools Player File(*.vmo)\0*.vmo\0";
    options.extension = L"vmo";
    options.initial = Utf8ToWide(initial);
    m_State.RequestFile(options, [this](const FileDialogResult &result) {
        if (result.status != FileDialogResult::Status::Selected) return;
        std::string error;
        if (ExportTo(result.paths.front(), CKFILE_FORVIEWER | CKFILE_WHOLECOMPRESSED, &error)) return;
        DevMessage message;
        message.text = Format("Save Level Error: %s", error);
        ShowMessage(std::move(message));
    });
}

void EditorApp::ShowMessage(DevMessage message) { m_Messages.push_back(std::move(message)); }

// Dev's message box (dialog 727, sub_5A0DD0's size): the icon at the left,
// the text's lines centered, OK (and Cancel) below.
void EditorApp::MessageDialog() {
    const char *id = "###DevMessage";
    if (m_Messages.empty()) return;
    const DevMessage &message = m_Messages.front();
    // Open until answered (the startup's window focus would close it).
    if (m_FocusSchematic <= 0 && !ImGui::IsPopupOpen(id)) ImGui::OpenPopup(id);
    const float s = ImGui::GetFontSize() / 13.0f;
    const std::string text = message.text;
    std::vector<std::string> lines;
    for (size_t at = 0; at <= text.size();) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        lines.push_back(text.substr(at, end - at));
        at = end + 1;
    }
    float width = 0.0f;
    for (const std::string &line : lines) width = std::max(width, vi::TextWidth(line.c_str(), false));
    const float lineHeight = ImGui::GetFontSize(), margin = 10.0f * s;
    const float textHeight = lineHeight * lines.size();
    const int icon = MessageIconId(message.icon);
    const float iconSpace = icon ? 40.0f * s : 0.0f;
    const float body = std::max(icon ? 40.0f * s : 0.0f, textHeight);
    const float buttons = message.cancel ? 140.0f * s : 70.0f * s;
    const ImVec2 client(std::max(margin * 2.0f + iconSpace + width, buttons + 10.0f * s),
                        margin * 2.0f + body + 26.0f * s);
    ImVec2 o;
    int closed = 0; // 1 OK, 2 Cancel
    if (vi::BeginDialog(id, message.title.empty() ? "Warning!" : message.title, client, s, o)) {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        if (icon)
            if (ImTextureID texture = DevMessageIcon(icon)) {
                const ImVec2 at(o.x + margin, o.y + margin + (body - 32.0f * s) * 0.5f);
                dl->AddImage(texture, at, ImVec2(at.x + 32.0f * s, at.y + 32.0f * s));
            }
        const float left = o.x + margin + iconSpace, right = o.x + client.x - margin;
        float y = o.y + margin + (body - textHeight) * 0.5f;
        for (const std::string &line : lines) {
            vi::Text(ImVec2(left, y), ImVec2(right, y + lineHeight), line.c_str(), false, vi::CENTER);
            y += lineHeight;
        }
        const float by = o.y + client.y - 23.0f * s;
        if (message.cancel) {
            if (vi::DialogButton("Cancel", ImVec2(o.x + client.x - 70.0f * s, by), s, ImGuiKey_Escape)) closed = 2;
            if (vi::DialogButton("OK", ImVec2(o.x + client.x - 140.0f * s, by), s, ImGuiKey_Enter)) closed = 1;
        } else if (vi::DialogButton("OK", ImVec2(o.x + client.x - 70.0f * s, by), s, ImGuiKey_Enter) ||
                   ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            closed = 1;
        }
        if (closed) ImGui::CloseCurrentPopup();
        vi::EndDialog();
    }
    if (!closed) return;
    std::function<void()> then = std::move(closed == 1 ? m_Messages.front().onOk : m_Messages.front().onCancel);
    m_Messages.erase(m_Messages.begin());
    if (then) then();
}

// Create Web Page (sub_5C7240, dialog 710 in its dialog units): the page's
// file, its title, the player's size; OK saves the composition next to the
// page (sub_598700: .vmo for the player, or .cmo with Save as Cmo), writes
// the page from Dev's template and with Preview In Browser opens it.
void EditorApp::WebPageDialog() {
    const char *id = "###CreateWebPage";
    if (m_OpenWebPage && m_FocusSchematic <= 0 && m_WebPageResume) {
        m_OpenWebPage = m_WebPageResume = false;
        if (m_State.HasDocument()) ImGui::OpenPopup(id);
    } else if (m_OpenWebPage && m_FocusSchematic <= 0) {
        ++m_WebPageRevision;
        m_OpenWebPage = false;
        if (!m_State.HasDocument()) return;
        if (m_WebPagePath.empty()) {
            // The first page: Virtools.htm in the current folder.
            std::error_code ec;
            m_WebPagePath = (fs::current_path(ec) / "Virtools.htm").string();
        }
        m_WebPageEditPath = m_WebPagePath;
        m_WebPageEditTitle = m_WebPageTitle;
        m_WebPageWidth = "320";
        m_WebPageHeight = "240";
        m_WebPageCmo = m_WebPagePreview = false;
        ImGui::OpenPopup(id);
    }
    if (!ImGui::IsPopupOpen(id)) return;
    const float s = ImGui::GetFontSize() / 13.0f;
    const ImVec2 client(254.0f * 1.5f * s, 172.0f * 1.625f * s + 26.0f * s);
    ImVec2 o;
    if (!vi::BeginDialog(id, "Create a Web Page", client, s, o)) return;
    auto P = [&](float x, float y) { return ImVec2(o.x + x * 1.5f * s, o.y + y * 1.625f * s); };
    ImDrawList *dl = ImGui::GetWindowDrawList();
    vi::Panel(ImVec2(o.x + 2.0f * s, o.y + 2.0f * s), ImVec2(o.x + client.x - 2.0f * s, P(0, 172).y), s);
    // The frames (SS_BLACKFRAME): the dialog's, Location's and Settings'.
    dl->AddRect(P(4, 5), P(249, 149), vi::Color(0));
    dl->AddRect(P(9, 20), P(242, 58), vi::Color(0));
    dl->AddRect(P(9, 75), P(242, 140), vi::Color(0));
    vi::Text(P(9, 9), P(47, 18), "Location", true);
    if (vi::Button("##destination", P(18, 27), P(101, 39), "- Choose Destination -")) {
        // sub_598420: the page's file (Html Files, OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT).
        FileDialogOptions options;
        options.save = true;
        options.owner = m_Window;
        options.filter = L"Html Files (*.htm;*.html)\0*.htm;*.html\0";
        options.extension = L"htm";
        options.title = L"Select file to save";
        options.initial = Utf8ToWide(m_WebPageEditPath);
        const unsigned revision = m_WebPageRevision;
        m_State.RequestFile(options, [this, revision](const FileDialogResult &result) {
            if (result.status == FileDialogResult::Status::Selected && revision == m_WebPageRevision)
                m_WebPageEditPath = result.paths.front();
        });
    }
    vi::Text(P(17, 45), P(234, 54), m_WebPageEditPath.c_str());
    vi::Text(P(9, 63), P(47, 72), "Settings", true);
    vi::Text(P(11, 79), P(227, 89), "  Web Page Title");
    std::string title = AnsiToUtf8(m_WebPageEditTitle);
    if (vi::Edit("##title", P(15, 91), P(232, 103), title, vi::LEFT)) m_WebPageEditTitle = Utf8ToAnsi(title);
    vi::Text(P(16, 108), P(87, 117), "Window Size");
    vi::Edit("##width", P(17, 121), P(53, 133), m_WebPageWidth);
    vi::Edit("##height", P(61, 121), P(97, 133), m_WebPageHeight);
    const float box = 8.0f * 1.5f * s;
    vi::Check("##cmo", P(18, 155), box, m_WebPageCmo);
    vi::Text(ImVec2(P(18, 155).x + box + 3.0f * s, P(18, 155).y), P(79, 164), "Save as Cmo");
    vi::Check("##preview", P(86, 155), box, m_WebPagePreview);
    vi::Text(ImVec2(P(86, 155).x + box + 3.0f * s, P(86, 155).y), P(171, 164), "Preview In Browser");
    const float by = o.y + client.y - 23.0f * s;
    const bool cancel = vi::DialogButton("Cancel", ImVec2(o.x + client.x - 70.0f * s, by), s, ImGuiKey_Escape);
    const bool ok = vi::DialogButton("OK", ImVec2(o.x + client.x - 140.0f * s, by), s, ImGuiKey_Enter);
    if (ok && m_State.HasDocument()) {
        // The page's file and title are kept after OK only.
        m_WebPagePath = m_WebPageEditPath;
        m_WebPageTitle = m_WebPageEditTitle;
        // An existing composition: Dev asks (MessageBox, MB_OKCANCEL) and stays open on Cancel; here
        // Dev's message box, the dialog opened again as it was on Cancel.
        if (Exists(WebPageCompositionPath(m_WebPagePath, m_WebPageCmo))) {
            DevMessage message;
            message.title = "Overwrite File";
            message.text = "The Virtools file already exists, do you want to overwrite it ?";
            message.cancel = true;
            message.onOk = [this] { WriteWebPage(); };
            message.onCancel = [this] { m_OpenWebPage = m_WebPageResume = true; };
            ShowMessage(std::move(message));
        } else {
            WriteWebPage();
        }
    }
    if (ok || cancel) ImGui::CloseCurrentPopup();
    vi::EndDialog();
}

// sub_598700: the composition next to the page, then the page.
void EditorApp::WriteWebPage() {
    if (!m_State.HasDocument()) return;
    int major = 0, minor = 0;
    if (!RuntimeVersion(m_State.host.RuntimeDirectory(), major, minor)) {
        m_State.Log(LogLevel::Error, "Cannot create web page: invalid Ballanced runtime metadata");
        return;
    }
    const std::string composition = WebPageCompositionPath(m_WebPagePath, m_WebPageCmo);
    std::string error;
    if (!ExportTo(composition, CKFILE_WHOLECOMPRESSED | (m_WebPageCmo ? 0u : CKFILE_FORVIEWER), &error)) {
        DevMessage message;
        message.text = "Unable to save CMO file !";
        ShowMessage(std::move(message));
        return;
    }
    const int width = std::atoi(m_WebPageWidth.c_str()), height = std::atoi(m_WebPageHeight.c_str());
    std::ofstream page(fs::u8path(m_WebPagePath), std::ios::binary | std::ios::trunc);
    page << WebPageHtml(m_WebPageTitle, composition, width, height, major, minor);
    page.close();
    if (!page) m_State.Log(LogLevel::Error, "Cannot write " + m_WebPagePath);
    else m_State.Log(LogLevel::Info, "Web page written: " + m_WebPagePath);
    if (m_WebPagePreview) {
        std::string error;
        if (!OpenDesktopFile(m_WebPagePath, &error)) m_State.Log(LogLevel::Warning, error);
    }
}

} // namespace vdev
