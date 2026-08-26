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
#include <chrono>
#include <ctime>
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

// ======================== تنظیمات مسیرها ========================
static std::string g_outputPath = "/sdcard/Download/lac_objdata.txt";
static std::string g_jsonPath   = "/sdcard/Download/lac_objdata.json";
static std::string g_sessionFile = "/sdcard/Download/lac_session.txt";

// ======================== متغیرهای سراسری ========================
static bool g_captureEnabled = false;
static int  g_capturedCount  = 0;
static std::vector<std::string> g_rawEntries;
static std::vector<std::string> g_timestampedEntries; // برای ذخیره با زمان

// ======================== توابع کمکی ========================
static std::string get_current_time() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::string time_str = std::ctime(&time_t);
    time_str.pop_back(); // حذف \n
    return time_str;
}

// ======================== ذخیره‌سازی کامل (JSON + TXT) ========================
static void flush_all() {
    // ----- ذخیره JSON -----
    std::ofstream f_json(g_jsonPath);
    if (f_json.is_open()) {
        f_json << "{\n";
        f_json << "  \"timestamp\": \"" << get_current_time() << "\",\n";
        f_json << "  \"count\": " << g_rawEntries.size() << ",\n";
        f_json << "  \"objects\": [\n";
        for (size_t i = 0; i < g_rawEntries.size(); i++) {
            std::string e = g_rawEntries[i];
            // فرار از کاراکترهای خاص JSON
            size_t pos = 0;
            while ((pos = e.find('"', pos)) != std::string::npos) {
                e.replace(pos, 1, "\\\"");
                pos += 2;
            }
            f_json << "    {\"idx\":" << i << ",\"raw\":\"" << e << "\"}";
            if (i + 1 < g_rawEntries.size()) f_json << ",";
            f_json << "\n";
        }
        f_json << "  ]\n}\n";
        f_json.close();
        LOGI("[objData] JSON saved: %s", g_jsonPath.c_str());
    } else {
        LOGE("[objData] Failed to open JSON file for writing");
    }

    // ----- ذخیره TXT (همراه با شماره و زمان) -----
    std::ofstream f_txt(g_outputPath);
    if (f_txt.is_open()) {
        f_txt << "========================================\n";
        f_txt << "  OBJDATA DUMP - " << get_current_time() << "\n";
        f_txt << "  Total entries: " << g_rawEntries.size() << "\n";
        f_txt << "========================================\n\n";
        
        if (g_rawEntries.empty()) {
            f_txt << "(No data captured yet)\n";
        } else {
            for (size_t i = 0; i < g_rawEntries.size(); i++) {
                f_txt << "[" << std::setw(4) << std::setfill('0') << (i+1) << "] " 
                      << g_rawEntries[i] << "\n";
            }
        }
        f_txt << "\n========================================\n";
        f_txt << "  End of dump\n";
        f_txt << "========================================\n";
        f_txt.close();
        LOGI("[objData] TXT saved: %s", g_outputPath.c_str());
    } else {
        LOGE("[objData] Failed to open TXT file for writing");
    }

    // ----- ذخیره فایل جلسه (خلاصه) -----
    std::ofstream f_session(g_sessionFile);
    if (f_session.is_open()) {
        f_session << "Session started: " << get_current_time() << "\n";
        f_session << "Total captures: " << g_rawEntries.size() << "\n";
        f_session.close();
    }
}

// ======================== اضافه کردن لاگ جدید ========================
static void append_raw_log(int idx, const std::string &entry) {
    // اضافه کردن به لیست
    g_rawEntries.push_back(entry);
    
    // نوشتن توی فایل TXT به صورت لحظه‌ای (اختیاری)
    std::ofstream f(g_outputPath, std::ios::app);
    if (f.is_open()) {
        char buf[32];
        snprintf(buf, sizeof(buf), "[%04d] ", idx);
        f << buf << entry << "\n";
        f.close();
    }
}

// ======================== تبدیل رشته از IL2CPP ========================
static std::string il2cpp_str(void *strObj) {
    if (!strObj) return "";
    int len = *(int *)((uintptr_t)strObj + 0x10);
    if (len <= 0 || len > 65536) return "";
    uint16_t *chars = (uint16_t *)((uintptr_t)strObj + 0x14);
    std::string result;
    result.reserve(len);
    for (int i = 0; i < len; i++) {
        result += (char)(chars[i] & 0xFF);
    }
    return result;
}

// ======================== هوک‌ها ========================
void (*orig_IEnumReadObj)(void *instance, void *_data);
void hook_IEnumReadObj(void *instance, void *_data) {
    if (g_captureEnabled && _data) {
        std::string data = il2cpp_str(_data);
        if (!data.empty()) {
            // پاک کردن داده‌های قبلی (برای دامپ کامل)
            g_rawEntries.clear();
            g_capturedCount = 0;
            
            // تجزیه داده
            std::stringstream ss(data);
            std::string line;
            while (std::getline(ss, line)) {
                if (!line.empty()) {
                    append_raw_log(++g_capturedCount, line);
                }
            }
            
            // ذخیره خودکار بعد از دریافت
            flush_all();
            LOGI("[objData] Captured %d entries from IEnumReadObj", g_capturedCount);
        }
    }
    if (orig_IEnumReadObj) {
        orig_IEnumReadObj(instance, _data);
    }
}

void (*orig_RpcSyncObj)(void *instance, void *_action, void *_data, void *_player);
void hook_RpcSyncObj(void *instance, void *_action, void *_data, void *_player) {
    if (g_captureEnabled && _action && _data) {
        std::string action = il2cpp_str(_action);
        if (action == "spawn") {
            std::string entry = il2cpp_str(_data);
            if (!entry.empty()) {
                append_raw_log(++g_capturedCount, entry);
                // ذخیره خودکار بعد از هر ورودی جدید
                flush_all();
                LOGI("[objData] Live spawn [%d]: %.60s", g_capturedCount, entry.c_str());
            }
        }
    }
    if (orig_RpcSyncObj) {
        orig_RpcSyncObj(instance, _action, _data, _player);
    }
}

// ======================== لیست ویژگی‌های منو ========================
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
    for (int i = 0; i < total; i++) {
        env->SetObjectArrayElement(ret, i, env->NewStringUTF(features[i]));
    }
    return ret;
}

// ======================== تغییرات منو ========================
void Changes(JNIEnv *env, jclass clazz, jobject obj, jint featNum, jstring featName, jint value, jlong Lvalue, jboolean boolean, jstring text) {
    switch (featNum) {
        case 0: // Toggle Enable Capture
            g_captureEnabled = (bool)boolean;
            LOGI("[objData] Capture %s", g_captureEnabled ? "ON" : "OFF");
            if (g_captureEnabled) {
                // پاک کردن داده‌های قبلی وقتی فعال می‌شه
                g_rawEntries.clear();
                g_capturedCount = 0;
                LOGI("[objData] Cleared old data on capture start");
            }
            break;
            
        case 1: // Button Dump Now
            LOGI("[objData] Manual dump requested, entries: %d", (int)g_rawEntries.size());
            flush_all();
            // نمایش پیام موفقیت (از طریق Toast در جاوا)
            Toast(env, obj, OBFUSCATE("Dump saved to Download/"), ToastLength::LENGTH_LONG);
            break;
            
        case 2: // Button Clear Log
            g_rawEntries.clear();
            g_capturedCount = 0;
            // پاک کردن فایل‌ها
            { std::ofstream f(g_outputPath); f << "cleared at " << get_current_time() << "\n"; }
            { std::ofstream f(g_jsonPath);   f << "{\"count\":0,\"objects\":[]}\n"; }
            { std::ofstream f(g_sessionFile); f << "Cleared at " << get_current_time() << "\n"; }
            LOGI("[objData] Logs cleared");
            Toast(env, obj, OBFUSCATE("Logs cleared"), ToastLength::LENGTH_SHORT);
            break;
            
        default:
            break;
    }
}

// ======================== ترد اصلی ========================
void hack_thread() {
    // منتظر لود شدن libil2cpp.so با تایم‌اوت
    int waitCount = 0;
    LOGI("[objData] Waiting for libil2cpp.so...");
    while (!isLibraryLoaded(targetLibName) && waitCount < 30) {
        sleep(1);
        waitCount++;
    }
    
    if (waitCount >= 30) {
        LOGI("[objData] Timeout: libil2cpp.so not loaded, skipping hooks");
        return;
    }
    
    LOGI("[objData] libil2cpp.so loaded, installing hooks...");
    
#if defined(__aarch64__)
    // هوک‌ها با چک کردن آدرس
    void *addr1 = getAbsoluteAddress(targetLibName, "0x10D8EC0");
    void *addr2 = getAbsoluteAddress(targetLibName, "0x10D90B0");
    
    if (addr1 != nullptr) {
        HOOK(targetLibName, "0x10D8EC0", hook_IEnumReadObj, orig_IEnumReadObj);
        LOGI("[objData] Hook IEnumReadObj installed at %p", addr1);
    } else {
        LOGE("[objData] Failed to find IEnumReadObj address");
    }
    
    if (addr2 != nullptr) {
        HOOK(targetLibName, "0x10D90B0", hook_RpcSyncObj, orig_RpcSyncObj);
        LOGI("[objData] Hook RpcSyncObj installed at %p", addr2);
    } else {
        LOGE("[objData] Failed to find RpcSyncObj address");
    }
#endif

    LOGI(OBFUSCATE("[objData] Done"));
}

// ======================== تابع ورودی ========================
__attribute__((constructor))
void lib_main() {
    std::thread(hack_thread).detach();
}