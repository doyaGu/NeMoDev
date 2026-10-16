#include "ui/app/DesktopFiles.h"
#include "core/runtime/Resources.h"
#include <SDL3/SDL.h>
#include <filesystem>
#if defined(__linux__)
#include <gio/gdesktopappinfo.h>
#include <gio/gunixfdlist.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace vdev {
namespace {
#if defined(__linux__)
// Sandboxed browsers cannot necessarily read the installation directory.
// Export the whole help directory so relative topics, styles and images work.
// The document portal owns this read-only grant for the desktop session; it
// remains usable after the editor exits, without granting access to the home.
bool BrowserHelpDirectory(std::filesystem::path &directory, std::string *error) {
    GAppInfo *browser = g_app_info_get_default_for_type("text/html", FALSE);
    if (!browser) return true; // Let SDL report a missing desktop handler.
    std::string app;
    if (G_IS_DESKTOP_APP_INFO(browser)) {
        gchar *flatpak = g_desktop_app_info_get_string(G_DESKTOP_APP_INFO(browser), "X-Flatpak");
        gchar *snap = g_desktop_app_info_get_string(G_DESKTOP_APP_INFO(browser), "X-SnapInstanceName");
        if (flatpak && *flatpak) app = flatpak;
        else if (snap && *snap) app = std::string("snap.") + snap;
        g_free(flatpak); g_free(snap);
    }
    g_object_unref(browser);
    if (app.empty()) return true;

    GError *failure = nullptr;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &failure);
    GVariant *reply = nullptr;
    const int fd = open(directory.c_str(), O_PATH | O_DIRECTORY | O_CLOEXEC);
    if (bus && fd >= 0) {
        GUnixFDList *files = g_unix_fd_list_new();
        const int index = g_unix_fd_list_append(files, fd, &failure);
        if (index >= 0) {
            GVariantBuilder handles;
            g_variant_builder_init(&handles, G_VARIANT_TYPE("ah"));
            g_variant_builder_add(&handles, "h", index);
            const gchar *permissions[] = {"read", nullptr};
            reply = g_dbus_connection_call_with_unix_fd_list_sync(bus,
                "org.freedesktop.portal.Documents", "/org/freedesktop/portal/documents",
                "org.freedesktop.portal.Documents", "AddFull",
                g_variant_new("(@ahus^as)", g_variant_builder_end(&handles),
                    1u | 8u, app.c_str(), permissions), // reuse existing, export directory
                G_VARIANT_TYPE("(asa{sv})"), G_DBUS_CALL_FLAGS_NONE, 5000,
                files, nullptr, nullptr, &failure);
        }
        g_object_unref(files);
    }
    if (fd >= 0) close(fd);
    if (bus) g_object_unref(bus);
    bool exported = false;
    if (reply) {
        gchar **ids = nullptr;
        GVariant *extra = nullptr;
        g_variant_get(reply, "(^as@a{sv})", &ids, &extra);
        GVariant *mount = g_variant_lookup_value(extra, "mountpoint", G_VARIANT_TYPE_BYTESTRING);
        if (ids[0] && *ids[0] && mount) {
            directory = std::filesystem::u8path(g_variant_get_bytestring(mount)) / ids[0] / directory.filename();
            exported = true;
        }
        if (mount) g_variant_unref(mount);
        g_strfreev(ids); g_variant_unref(extra); g_variant_unref(reply);
    }
    if (!exported && error) *error = std::string("Cannot share the help directory with the browser: ") +
        (failure ? failure->message : "document portal did not return a directory");
    if (failure) g_error_free(failure);
    return exported;
}
#endif
}
bool OpenDesktopFile(const std::string &path, std::string *error) {
    std::error_code failure;
    const auto file = std::filesystem::absolute(std::filesystem::u8path(path), failure);
    if (failure || !std::filesystem::exists(file, failure)) {
        if (error) *error = "File not found: " + path;
        return false;
    }
    const std::string name = file.generic_u8string();
    std::string url = "file://";
    if (name.empty() || name.front() != '/') url += '/';
    const char *hex = "0123456789ABCDEF";
    for (unsigned char c : name) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' || c == ':' || c == '-' || c == '_' || c == '.' || c == '~') url += char(c);
        else { url += '%'; url += hex[c >> 4]; url += hex[c & 15]; }
    }
    if (SDL_OpenURL(url.c_str())) return true;
    if (error) *error = SDL_GetError();
    return false;
}
bool OpenHelpTopic(const std::string &topic, std::string *error) {
    auto directory = std::filesystem::u8path(DataFilePath("help"));
#if defined(__linux__)
    if (!BrowserHelpDirectory(directory, error)) return false;
#endif
    return OpenDesktopFile((directory / std::filesystem::u8path(topic)).u8string(), error);
}
}
