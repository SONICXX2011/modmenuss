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
#include <iomanip>
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

// ======================== مسیرهای ذخیره‌سازی ========================
static std::string g_outputPath = "/sdcard/Download/lac_objdata.txt";
static std::string g_jsonPath   = "/sdcard/Download/lac_objdata.json";
static std::string g_sessionFile = "/sdcard/Download/lac_session.txt";
static std::string g_networkLog  = "/sdcard/Download/network_log.txt";
static std::string g_lastIPFile  = "/sdcard/Download/last_ip.txt";
static std::string g_debugLog    = "/sdcard/Download/mod_debug.txt";
static std::string g_ipResultLog = "/sdcard/Download/ip_result.txt";

// ======================== متغیرهای سراسری ========================
static bool g_captureEnabled = false;
static int  g_capturedCount  = 0;
static std::vector<std::string> g_rawEntries;

// ======================== متغیرهای شبکه ========================
static std::string g_targetIP = "";
static bool g_ipApplied = false;
static bool g_connectPressed = false;

// ======================== توابع کمکی ========================
static std::string get_current_time() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::string time_str = std::ctime(&time_t);
    time_str.pop_back();
    return time_str;
}

static void write_debug(const char* tag, const char* msg) {
    std::ofstream f(g_debugLog, std::ios::app);
    if (f.is_open()) {
        f << "[" << get_current_time() << "] " << tag << ": " << msg << "\n";
        f.close();
    }
}

static void log_network(const char* msg) {
    std::ofstream f(g_networkLog, std::ios::app);
    if (f.is_open()) {
        f << "[" << get_current_time() << "] " << msg << "\n";
        f.close();
    }
    LOGI("[Network] %s", msg);
    write_debug("Network", msg);
}

static void save_ip_to_file(const std::string& ip) {
    std::ofstream f(g_lastIPFile);
    if (f.is_open()) {
        f << ip << "\n";
        f.close();
        log_network(("IP saved to file: " + ip).c_str());
    }
}

static std::string load_ip_from_file() {
    std::ifstream f(g_lastIPFile);
    if (f.is_open()) {
        std::string ip;
        std::getline(f, ip);
        f.close();
        return ip;
    }
    return "";
}

// ======================== توابع دامپ ========================
static void flush_json() {
    std::ofstream f(g_jsonPath);
    if (!f.is_open()) return;
    f << "{\n";
    f << "  \"timestamp\": \"" << get_current_time() << "\",\n";
    f << "  \"count\": " << g_rawEntries.size() << ",\n";
    f << "  \"objects\": [\n";
    for (size_t i = 0; i < g_rawEntries.size(); i++) {
        std::string e = g_rawEntries[i];
        size_t pos = 0;
        while ((pos = e.find('"', pos)) != std::string::npos) {
            e.replace(pos, 1, "\\\"");
            pos += 2;
        }
        f << "    {\"idx\":" << i << ",\"raw\":\"" << e << "\"}";
        if (i + 1 < g_rawEntries.size()) f << ",";
        f << "\n";
    }
    f << "  ]\n}\n";
    f.close();
}

static void append_raw_log(int idx, const std::string &entry) {
    g_rawEntries.push_back(entry);
    std::ofstream f(g_outputPath, std::ios::app);
    if (f.is_open()) {
        char buf[32];
        snprintf(buf, sizeof(buf), "[%04d] ", idx);
        f << buf << entry << "\n";
        f.close();
    }
}

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

// ======================== توابع Toast با string ========================
static void show_toast(JNIEnv *env, jobject obj, const std::string& msg, int length) {
    // با NewStringUTF مستقیم می‌فرستیم، چون OBFUSCATE روی متغیر runtime کار نمیکنه
    jstring jmsg = env->NewStringUTF(msg.c_str());
    jclass toastClass = env->FindClass("android/widget/Toast");
    jmethodID makeText = env->GetStaticMethodID(toastClass, "makeText", 
        "(Landroid/content/Context;Ljava/lang/CharSequence;I)Landroid/widget/Toast;");
    jobject toast = env->CallStaticObjectMethod(toastClass, makeText, obj, jmsg, length);
    jmethodID show = env->GetMethodID(toastClass, "show", "()V");
    env->CallVoidMethod(toast, show);
    env->DeleteLocalRef(jmsg);
}

// ======================== هوک‌ها ========================
void (*orig_IEnumReadObj)(void *instance, void *_data);
void hook_IEnumReadObj(void *instance, void *_data) {
    if (g_captureEnabled && _data) {
        std::string data = il2cpp_str(_data);
        if (!data.empty()) {
            g_rawEntries.clear();
            g_capturedCount = 0;
            std::stringstream ss(data);
            std::string line;
            while (std::getline(ss, line)) {
                if (!line.empty()) {
                    append_raw_log(++g_capturedCount, line);
                }
            }
            flush_json();
            LOGI("[objData] Captured %d entries", g_capturedCount);
        }
    }
    if (orig_IEnumReadObj) orig_IEnumReadObj(instance, _data);
}

void (*orig_RpcSyncObj)(void *instance, void *_action, void *_data, void *_player);
void hook_RpcSyncObj(void *instance, void *_action, void *_data, void *_player) {
    if (g_captureEnabled && _action && _data) {
        std::string action = il2cpp_str(_action);
        if (action == "spawn") {
            std::string entry = il2cpp_str(_data);
            if (!entry.empty()) {
                append_raw_log(++g_capturedCount, entry);
                flush_json();
                LOGI("[objData] Live spawn [%d]: %.60s", g_capturedCount, entry.c_str());
            }
        }
    }
    if (orig_RpcSyncObj) orig_RpcSyncObj(instance, _action, _data, _player);
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

        OBFUSCATE("Category_🌐 Network Tools"),
        OBFUSCATE("InputText_Enter IP Address"),
        OBFUSCATE("Button_Apply IP"),
        OBFUSCATE("Button_Connect"),
        OBFUSCATE("Button_Show Saved IP"),
        OBFUSCATE("RichTextView_IP Status: <font color='yellow'>Not Set</font>"),
    };
    int total = sizeof features / sizeof features[0];
    ret = (jobjectArray)env->NewObjectArray(total, env->FindClass(OBFUSCATE("java/lang/String")), env->NewStringUTF(""));
    for (int i = 0; i < total; i++) {
        env->SetObjectArrayElement(ret, i, env->NewStringUTF(features[i]));
    }
    return ret;
}

// ======================== تغییرات منو ========================
void Changes(JNIEnv *env, jclass clazz, jobject obj, jint featNum, jstring featName, 
             jint value, jlong Lvalue, jboolean boolean, jstring text) {

    const char *textStr = nullptr;
    if (text != nullptr) {
        textStr = env->GetStringUTFChars(text, nullptr);
    }

    switch (featNum) {
        case 0:
            g_captureEnabled = (bool)boolean;
            LOGI("[objData] capture %s", g_captureEnabled ? "ON" : "OFF");
            write_debug("Toggle", g_captureEnabled ? "Capture ON" : "Capture OFF");
            break;

        case 1:
            flush_json();
            LOGI("[objData] manual flush, count=%d", g_capturedCount);
            write_debug("Button", "Manual dump");
            break;

        case 2:
            g_rawEntries.clear();
            g_capturedCount = 0;
            { std::ofstream f(g_outputPath); f << "cleared at " << get_current_time() << "\n"; }
            { std::ofstream f(g_jsonPath);   f << "{\"count\":0,\"objects\":[]}\n"; }
            LOGI("[objData] cleared");
            write_debug("Button", "Logs cleared");
            break;

        // ========== بخش شبکه ==========
        case 10: // InputText_Enter IP Address
            if (textStr != nullptr) {
                g_targetIP = textStr;
                LOGI("[Network] IP entered: %s", g_targetIP.c_str());
                log_network(("IP entered: " + g_targetIP).c_str());
                save_ip_to_file(g_targetIP);

                std::ofstream f(g_ipResultLog);
                if (f.is_open()) {
                    f << "IP Entered: " << g_targetIP << "\n";
                    f << "Time: " << get_current_time() << "\n";
                    f.close();
                }

                // ✅ استفاده از show_toast با string
                show_toast(env, obj, "IP saved: " + g_targetIP, ToastLength::LENGTH_SHORT);
            }
            break;

        case 11: // Button_Apply IP
            if (!g_targetIP.empty()) {
                LOGI("[Network] Applying IP: %s", g_targetIP.c_str());
                log_network(("Applying IP: " + g_targetIP).c_str());
                g_ipApplied = true;

                std::ofstream f(g_ipResultLog, std::ios::app);
                if (f.is_open()) {
                    f << "IP Applied: " << g_targetIP << "\n";
                    f << "Applied Time: " << get_current_time() << "\n";
                    f.close();
                }
                show_toast(env, obj, "IP Applied!", ToastLength::LENGTH_SHORT);
            } else {
                std::string savedIP = load_ip_from_file();
                if (!savedIP.empty()) {
                    g_targetIP = savedIP;
                    LOGI("[Network] Loaded IP from file: %s", savedIP.c_str());
                    log_network(("Loaded IP from file: " + savedIP).c_str());
                    show_toast(env, obj, "IP loaded: " + savedIP, ToastLength::LENGTH_SHORT);
                } else {
                    show_toast(env, obj, "No IP found! Enter IP first.", ToastLength::LENGTH_SHORT);
                }
            }
            break;

        case 12: // Button_Connect
            g_connectPressed = true;
            LOGI("[Network] Connect button pressed");
            log_network("Connect button pressed");

            if (!g_targetIP.empty()) {
                LOGI("[Network] Attempting connection to: %s", g_targetIP.c_str());
                std::ofstream f(g_ipResultLog, std::ios::app);
                if (f.is_open()) {
                    f << "Connect Attempt: " << g_targetIP << "\n";
                    f << "Time: " << get_current_time() << "\n";
                    f.close();
                }
                show_toast(env, obj, "Connecting to: " + g_targetIP, ToastLength::LENGTH_LONG);
            } else {
                show_toast(env, obj, "No IP set! Enter IP first.", ToastLength::LENGTH_SHORT);
            }
            break;

        case 13: // Button_Show Saved IP
            {
                std::string savedIP = load_ip_from_file();
                if (!savedIP.empty()) {
                    LOGI("[Network] Saved IP: %s", savedIP.c_str());
                    show_toast(env, obj, "Saved IP: " + savedIP, ToastLength::LENGTH_LONG);
                } else {
                    show_toast(env, obj, "No saved IP found!", ToastLength::LENGTH_SHORT);
                }
            }
            break;

        default:
            break;
    }

    if (textStr != nullptr) {
        env->ReleaseStringUTFChars(text, textStr);
    }
}

// ======================== ترد اصلی ========================
void hack_thread() {
    write_debug("Init", "hack_thread started");

    int waitCount = 0;
    while (!isLibraryLoaded(targetLibName) && waitCount < 30) {
        sleep(1);
        waitCount++;
    }

    if (waitCount >= 30) {
        LOGI("[objData] Timeout: libil2cpp.so not loaded");
        write_debug("ERROR", "Timeout waiting for libil2cpp.so");
        return;
    }

    LOGI("[objData] libil2cpp.so loaded!");
    write_debug("Init", "libil2cpp.so loaded");

#if defined(__aarch64__)
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

    std::string savedIP = load_ip_from_file();
    if (!savedIP.empty()) {
        g_targetIP = savedIP;
        LOGI("[Network] Loaded saved IP: %s", savedIP.c_str());
    }

    LOGI(OBFUSCATE("[objData] Done"));
    write_debug("Init", "hack_thread finished");
}

// ======================== تابع ورودی ========================
__attribute__((constructor))
void lib_main() {
    std::ofstream f(g_debugLog);
    if (f.is_open()) {
        f << "========== MOD LOADED ==========\n";
        f << "Time: " << get_current_time() << "\n";
        f << "===============================\n\n";
        f.close();
    }

    std::ofstream f2(g_networkLog);
    if (f2.is_open()) {
        f2 << "========== NETWORK LOG ==========\n";
        f2 << "Time: " << get_current_time() << "\n";
        f2 << "================================\n\n";
        f2.close();
    }

    std::thread(hack_thread).detach();
}