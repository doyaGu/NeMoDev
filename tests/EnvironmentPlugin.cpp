#include "CKAll.h"
#if defined(_WIN32)
#define ENVIRONMENT_PLUGIN_EXPORT __declspec(dllexport)
#else
#define ENVIRONMENT_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif
extern "C" ENVIRONMENT_PLUGIN_EXPORT int CKGetPluginInfoCount() { return 1; }
extern "C" ENVIRONMENT_PLUGIN_EXPORT CKPluginInfo *CKGetPluginInfo(int index) {
    if (index != 0) return nullptr;
    static CKPluginInfo info;
    info.m_GUID = CKGUID(0x6e76656e, 0x00000501);
    info.m_Summary = "Environment reload fixture";
    info.m_Description = "Plugin lifecycle regression fixture";
    info.m_Author = "NeMoDev tests";
    info.m_Version = 1;
    info.m_Type = CKPLUGIN_EXTENSION_DLL;
    return &info;
}
