#include <list>
#include <vector>
#include <cstring>
#include <pthread.h>
#include <thread>
#include <string>
#include <jni.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include "Includes/Logger.h"
#include "Includes/obfuscate.h"
#include "Includes/Utils.hpp"
#include "Menu/Menu.hpp"
#include "Menu/Jni.hpp"
#include "Includes/Macros.h"
#include "dobby.h"

#define targetLibName OBFUSCATE("libil2cpp.so")

#define OFFSET_IEnumReadObj    0x10D8EC0
#define OFFSET_RpcSyncObj      0x10D90B0
#define OFFSET_objectLoadCount 0x330

static bool g_captureEnabled = false;
static int  g_capturedCount  = 0;
static std::string g_outputPath = "/sdcard/Download/lac_objdata.txt";
static std::string g_jsonPath   = "/sdcard/Download/lac_objdata.json";
static std::vector<std::string> g_rawEntries;

static void flush_json() {
    std::ofstream f(g_jsonPath);
    if (!f.is_open()) return;
    f << "{\n  \"count\": " << g_rawEntries.size() << ",\n  \"objects\": [\n";
    for (size_t i = 0; i < g_rawEntries.size(); i++) {
        std::string e = g_rawEntries[i];
        size_t pos = 0;
        while ((pos = e.find('"', pos)) != std::string::npos) { e.replace(pos, 1, "\\\""); pos += 2; }
        f << "    {\"idx\":" << i << ",\"raw\":\"" << e << "\"}";
        if (i + 1 < g_rawEntries.size()) f << ",";
        f << "\n";
    }
    f << "  ]\n}\n";
    f.close();
}

static void append_raw_log(int idx, const std::string &entry) {
    std::ofstream f(g_outputPath, std::ios::app);
    if (!f.is_open()) return;
    char buf[32];
    snprintf(buf, sizeof(buf), "[%04d] ", idx);
    f << buf << entry << "\n";
    f.close();
}

static std::string il2cpp_str(void *strObj) {
    if (!strObj) return "";
    int len = *(int *)((uintptr_t)strObj + 0x10);
    if (len <= 0 || len > 65536) return "";
    uint16_t *chars = (uint16_t *)((uintptr_t)strObj + 0x14);
    std::string result;
    result.reserve(len);
    for (int i = 0; i < len; i++) result += (char)(chars[i] & 0xFF);
    return result;
}

void (*orig_IEnumReadObj)(void *instance, void *_data);
void hook_IEnumReadObj(void *instance, void *_data) {
    if (g_captureEnabled && _data) {
        std::string data = il2cpp_str(_data);
        g_rawEntries.clear();
        g_capturedCount = 0;
        { std::ofstream f(g_outputPath); f << "session: " << time(nullptr) << "\n\n"; }
        std::stringstream ss(data);
        std::string line;
        while (std::getline(ss, line)) {
            if (!line.empty()) {
                g_rawEntries.push_back(line);
                append_raw_log(++g_capturedCount, line);
            }
        }
        flush_json();
        LOGI("[objData] captured %d entries", g_capturedCount);
    }
    return orig_IEnumReadObj(instance, _data);
}

void (*orig_RpcSyncObj)(void *instance, void *_action, void *_data, void *_player);
void hook_RpcSyncObj(void *instance, void *_action, void *_data, void *_player) {
    if (g_captureEnabled && _action && _data) {
        std::string action = il2cpp_str(_action);
        if (action == "spawn") {
            std::string entry = il2cpp_str(_data);
            if (!entry.empty()) {
                g_rawEntries.push_back(entry);
                append_raw_log(++g_capturedCount, entry);
                flush_json();
                LOGI("[objData] live [%d]: %.60s", g_capturedCount, entry.c_str());
            }
        }
    }
    return orig_RpcSyncObj(instance, _action, _data, _player);
}

jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    jobjectArray ret;
    const char *features[] = {
        OBFUSCATE("Category_ObjData Capture"),
        OBFUSCATE("Toggle_Enable Capture"),
        OBFUSCATE("Button_Dump Now"),
        OBFUSCATE("Button_Clear Log"),
        OBFUSCATE("RichTextView_Output: /sdcard/Download/<br/>lac_objdata.txt<br/>lac_objdata.json"),
    };
    int total = sizeof features / sizeof features[0];
    ret = (jobjectArray)env->NewObjectArray(total, env->FindClass(OBFUSCATE("java/lang/String")), env->NewStringUTF(""));
    for (int i = 0; i < total; i++)
        env->SetObjectArrayElement(ret, i, env->NewStringUTF(features[i]));
    return ret;
}

void Changes(JNIEnv *env, jclass clazz, jobject obj, jint featNum, jstring featName, jint value, jlong Lvalue, jboolean boolean, jstring text) {
    switch (featNum) {
        case 0:
            g_captureEnabled = (bool)boolean;
            LOGI("[objData] capture %s", g_captureEnabled ? "ON" : "OFF");
            break;
        case 1:
            flush_json();
            LOGI("[objData] manual flush, count=%d", g_capturedCount);
            break;
        case 2:
            g_rawEntries.clear();
            g_capturedCount = 0;
            { std::ofstream f(g_outputPath); f << "cleared\n"; }
            { std::ofstream f(g_jsonPath);   f << "{\"count\":0,\"objects\":[]}\n"; }
            LOGI("[objData] cleared");
            break;
        default: break;
    }
}

void hack_thread() {
    while (!isLibraryLoaded(targetLibName)) sleep(1);
#if defined(__aarch64__)
   HOOK(targetLibName, "0x10D8EC0", hook_IEnumReadObj, orig_IEnumReadObj);
   HOOK(targetLibName, "0x10D90B0", hook_RpcSyncObj,   orig_RpcSyncObj);
    LOGI(OBFUSCATE("[objData] hooks installed"));
#endif
    LOGI(OBFUSCATE("Done"));
}

__attribute__((constructor))
void lib_main() {
    std::thread(hack_thread).detach();
}