#include <string>
#include <jni.h>
#include <unistd.h>
#include <fstream>
#include <thread>
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

// ======================== آفست‌ها ========================
#define OFFSET_IP_INPUT  0xC0
#define OFFSET_IP_INPUT2 0xD8
#define OFFSET_IP_INPUT3 0x80

// ======================== مسیرها ========================
static std::string g_debugLog = "/sdcard/Download/mod_debug.txt";
static std::string g_lastIPFile = "/sdcard/Download/last_ip.txt";
static std::string g_captureLog = "/sdcard/Download/capture_log.txt";
static std::string g_ipResultLog = "/sdcard/Download/ip_result.txt";

// ======================== متغیرها ========================
static std::string g_targetIP = "";
static std::string g_lastAction = "";

// ======================== توابع کمکی ========================
static std::string get_time() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::string time_str = std::ctime(&time_t);
    time_str.pop_back();
    return time_str;
}

static void write_log(const std::string& file, const std::string& msg) {
    std::ofstream f(file, std::ios::app);
    if (f.is_open()) {
        f << "[" << get_time() << "] " << msg << "\n";
        f.close();
    }
}

static void write_debug(const std::string& msg) {
    write_log(g_debugLog, msg);
    LOGI("[Debug] %s", msg.c_str());
}

static void show_toast(JNIEnv *env, jobject obj, const std::string& msg, int length) {
    if (env == nullptr || obj == nullptr) return;
    
    jstring jmsg = env->NewStringUTF(msg.c_str());
    jclass toastClass = env->FindClass("android/widget/Toast");
    if (toastClass == nullptr) {
        env->DeleteLocalRef(jmsg);
        return;
    }
    
    jmethodID makeText = env->GetStaticMethodID(toastClass, "makeText", 
        "(Landroid/content/Context;Ljava/lang/CharSequence;I)Landroid/widget/Toast;");
    if (makeText == nullptr) {
        env->DeleteLocalRef(jmsg);
        return;
    }
    
    jobject toast = env->CallStaticObjectMethod(toastClass, makeText, obj, jmsg, length);
    if (toast == nullptr) {
        env->DeleteLocalRef(jmsg);
        return;
    }
    
    jmethodID show = env->GetMethodID(toastClass, "show", "()V");
    if (show != nullptr) {
        env->CallVoidMethod(toast, show);
    }
    
    env->DeleteLocalRef(jmsg);
    env->DeleteLocalRef(toast);
}

static void save_ip(const std::string& ip) {
    std::ofstream f(g_lastIPFile);
    if (f.is_open()) {
        f << ip << "\n";
        f.close();
        write_debug("IP saved: " + ip);
        write_log(g_ipResultLog, "✅ IP SAVED: " + ip);
    }
}

static std::string load_ip() {
    std::ifstream f(g_lastIPFile);
    if (f.is_open()) {
        std::string ip;
        std::getline(f, ip);
        f.close();
        return ip;
    }
    return "";
}

// ======================== کپچر کامل ========================
static void capture_everything(JNIEnv *env, jobject obj) {
    write_debug("========== CAPTURE START ==========");
    write_log(g_captureLog, "========== CAPTURE START ==========");
    
    // ۱. اطلاعات base address
    uintptr_t baseAddr = getLibraryAddress("libil2cpp.so");
    std::string baseInfo = "libil2cpp.so base: 0x" + std::to_string(baseAddr);
    write_debug(baseInfo);
    write_log(g_captureLog, baseInfo);
    
    if (baseAddr == 0) {
        write_log(g_captureLog, "❌ Failed to get base address!");
        return;
    }
    
    // ۲. چک کردن آفست‌ها
    uintptr_t offsets[] = {OFFSET_IP_INPUT, OFFSET_IP_INPUT2, OFFSET_IP_INPUT3};
    const char* offsetNames[] = {"GtaMenuControl.ipInput (0xC0)", "MenuControl.ipInput (0xD8)", "ServerListAccess.ipAddressInput (0x80)"};
    
    for (int i = 0; i < 3; i++) {
        uintptr_t addr = baseAddr + offsets[i];
        uintptr_t ptr = 0;
        memcpy(&ptr, (void*)addr, sizeof(uintptr_t));
        
        std::string msg = "Offset " + std::string(offsetNames[i]) + " at 0x" + std::to_string(addr) + " → pointer: 0x" + std::to_string(ptr);
        write_debug(msg);
        write_log(g_captureLog, msg);
        
        if (ptr != 0) {
            char buffer[256] = {0};
            memcpy(buffer, (void*)ptr, 50);
            std::string value = "   Value: \"" + std::string(buffer) + "\"";
            write_debug(value);
            write_log(g_captureLog, value);
        }
    }
    
    // ۳. اطلاعات IP
    std::string ipInfo = "Current IP: " + g_targetIP;
    write_debug(ipInfo);
    write_log(g_captureLog, ipInfo);
    
    // ۴. اطلاعات فایل‌ها
    std::string fileInfo = "Log files: " + g_debugLog + ", " + g_lastIPFile + ", " + g_captureLog;
    write_debug(fileInfo);
    write_log(g_captureLog, fileInfo);
    
    write_log(g_captureLog, "========== CAPTURE END ==========");
    write_debug("========== CAPTURE END ==========");
}

// ======================== تزریق IP با آفست ========================
static void inject_ip_by_offset(JNIEnv *env, jobject obj) {
    if (env == nullptr || obj == nullptr) {
        write_debug("❌ env or obj is null");
        return;
    }
    
    if (g_targetIP.empty()) {
        g_targetIP = load_ip();
        if (g_targetIP.empty()) {
            write_debug("❌ No IP found!");
            write_log(g_ipResultLog, "❌ No IP found!");
            return;
        }
    }
    
    write_debug("🔄 Injecting IP: " + g_targetIP);
    write_log(g_ipResultLog, "🔄 Injecting IP: " + g_targetIP);
    
    uintptr_t baseAddr = getLibraryAddress("libil2cpp.so");
    if (baseAddr == 0) {
        write_debug("❌ libil2cpp.so not loaded!");
        write_log(g_ipResultLog, "❌ libil2cpp.so not loaded!");
        return;
    }
    
    bool injected = false;
    uintptr_t offsets[] = {OFFSET_IP_INPUT, OFFSET_IP_INPUT2, OFFSET_IP_INPUT3};
    const char* offsetNames[] = {"0xC0", "0xD8", "0x80"};
    
    for (int i = 0; i < 3 && !injected; i++) {
        uintptr_t addr = baseAddr + offsets[i];
        uintptr_t ptr = 0;
        memcpy(&ptr, (void*)addr, sizeof(uintptr_t));
        
        if (ptr != 0) {
            // نوشتن IP
            KittyMemory::memWrite((void*)ptr, g_targetIP.c_str(), g_targetIP.length());
            write_debug("✅ IP written at offset " + std::string(offsetNames[i]) + " (0x" + std::to_string(addr) + ")");
            write_log(g_ipResultLog, "✅ IP written at offset " + std::string(offsetNames[i]) + " (0x" + std::to_string(addr) + ")");
            injected = true;
        }
    }
    
    if (!injected) {
        write_debug("❌ All offsets failed!");
        write_log(g_ipResultLog, "❌ All offsets failed!");
    }
}

// ======================== منو ========================
jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    jobjectArray ret;
    const char *features[] = {
        OBFUSCATE("Category_🌐 Network Tools"),
        OBFUSCATE("InputText_Enter IP"),           // featNum: 0
        OBFUSCATE("Button_Inject IP"),             // featNum: 1
        OBFUSCATE("Button_Show IP"),               // featNum: 2
        OBFUSCATE("Button_📡 Capture All"),        // featNum: 3
        OBFUSCATE("RichTextView_Status: <font color='yellow'>Ready</font>"),
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
        case 0: // Input IP
            if (textStr != nullptr) {
                g_targetIP = textStr;
                save_ip(g_targetIP);
                write_log(g_ipResultLog, "📝 IP entered: " + g_targetIP);
            }
            break;

        case 1: // Button_Inject IP
            write_log(g_ipResultLog, "🔘 Inject button pressed");
            inject_ip_by_offset(env, obj);
            break;

        case 2: // Button_Show IP
            {
                std::string savedIP = load_ip();
                if (!savedIP.empty()) {
                    write_log(g_ipResultLog, "📌 Show IP: " + savedIP);
                    show_toast(env, obj, "📌 IP: " + savedIP, 1);
                } else {
                    write_log(g_ipResultLog, "❌ No saved IP!");
                }
            }
            break;

        case 3: // Button_📡 Capture All
            write_log(g_captureLog, "🔘 Capture button pressed");
            capture_everything(env, obj);
            show_toast(env, obj, "✅ Capture saved to capture_log.txt", 1);
            break;

        default:
            write_debug("Unknown featNum: " + std::to_string(featNum));
            break;
    }

    if (textStr != nullptr) {
        env->ReleaseStringUTFChars(text, textStr);
    }
}

// ======================== ترد اصلی ========================
void hack_thread() {
    const char* libName = "libil2cpp.so";
    int waitCount = 0;
    
    write_debug("⏳ Waiting for " + std::string(libName) + "...");
    while (!isLibraryLoaded(targetLibName) && waitCount < 30) {
        sleep(1);
        waitCount++;
    }
    
    if (waitCount >= 30) {
        write_debug("⏰ Timeout waiting for " + std::string(libName));
        return;
    }
    
    write_debug("✅ " + std::string(libName) + " loaded!");
    
    g_targetIP = load_ip();
    if (!g_targetIP.empty()) {
        write_debug("📌 Loaded IP: " + g_targetIP);
    }
    
    write_debug("✅ hack_thread finished");
}

// ======================== تابع ورودی ========================
__attribute__((constructor))
void lib_main() {
    std::ofstream f(g_debugLog);
    if (f.is_open()) {
        f << "========== MOD LOADED ==========\n";
        f << "Time: " << get_time() << "\n";
        f << "===============================\n\n";
        f.close();
    }
    
    std::ofstream f2(g_ipResultLog);
    if (f2.is_open()) {
        f2 << "========== IP RESULT LOG ==========\n";
        f2 << "Time: " << get_time() << "\n";
        f2 << "==================================\n\n";
        f2.close();
    }
    
    std::ofstream f3(g_captureLog);
    if (f3.is_open()) {
        f3 << "========== CAPTURE LOG ==========\n";
        f3 << "Time: " << get_time() << "\n";
        f3 << "================================\n\n";
        f3.close();
    }
    
    write_debug("🚀 lib_main called");
    std::thread(hack_thread).detach();
}