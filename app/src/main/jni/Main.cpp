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

// ======================== آفست‌ها (از دامپ) ========================
#define OFFSET_IP_INPUT       0xD8  // آفست ipInput در MenuControl/GtaMenuControl
#define OFFSET_HOST_BUTTON    0x70
#define OFFSET_JOIN_BUTTON    0x78
#define OFFSET_SERVER_LIST    0x80

// ======================== مسیرها ========================
static std::string g_captureLog = "/sdcard/Download/capture_log.txt";
static std::string g_debugLog = "/sdcard/Download/capture_debug.txt";
static std::string g_lastCapture = "";
static bool g_captureMode = false;

// ======================== توابع کمکی ========================
static std::string get_time() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::string time_str = std::ctime(&time_t);
    time_str.pop_back();
    return time_str;
}

static void write_capture(const std::string& msg) {
    std::ofstream f(g_captureLog, std::ios::app);
    if (f.is_open()) {
        f << "[" << get_time() << "] " << msg << "\n";
        f.close();
    }
}

static void write_debug(const std::string& msg) {
    std::ofstream f(g_debugLog, std::ios::app);
    if (f.is_open()) {
        f << "[" << get_time() << "] " << msg << "\n";
        f.close();
    }
    LOGI("[Capture] %s", msg.c_str());
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

// ======================== کپچر مستقیم با آفست (بدون کلاس) ========================
static void capture_by_offset(JNIEnv *env, jobject obj) {
    std::stringstream result;
    result << "========== CAPTURE BY OFFSET ==========\n";
    result << "Time: " << get_time() << "\n\n";
    result << "📡 Capturing offsets from libil2cpp.so\n";
    result << "   IP Input offset: 0x" << std::hex << OFFSET_IP_INPUT << "\n";
    result << "   Host Button offset: 0x" << std::hex << OFFSET_HOST_BUTTON << "\n";
    result << "   Join Button offset: 0x" << std::hex << OFFSET_JOIN_BUTTON << "\n\n";
    
    // ====== گرفتن آدرس base libil2cpp.so ======
    uintptr_t baseAddr = getLibraryAddress("libil2cpp.so");
    if (baseAddr == 0) {
        result << "❌ Failed to get libil2cpp.so base address!\n";
        write_capture(result.str());
        show_toast(env, obj, "libil2cpp.so not loaded!", 0);
        return;
    }
    result << "✅ libil2cpp.so base: 0x" << std::hex << baseAddr << "\n\n";
    
    // ====== خواندن حافظه از آفست‌ها ======
    uintptr_t ipInputAddr = baseAddr + OFFSET_IP_INPUT;
    uintptr_t hostButtonAddr = baseAddr + OFFSET_HOST_BUTTON;
    uintptr_t joinButtonAddr = baseAddr + OFFSET_JOIN_BUTTON;
    
    result << "🔍 Reading memory...\n";
    result << "   ipInput address: 0x" << std::hex << ipInputAddr << "\n";
    result << "   hostButton address: 0x" << std::hex << hostButtonAddr << "\n";
    result << "   joinButton address: 0x" << std::hex << joinButtonAddr << "\n\n";
    
    // ====== خواندن مقادیر ======
    void *ipInputPtr = (void*)ipInputAddr;
    void *hostButtonPtr = (void*)hostButtonAddr;
    void *joinButtonPtr = (void*)joinButtonAddr;
    
    result << "📊 Values:\n";
    
    // خواندن ipInput (pointer)
    uintptr_t ipInputValue = 0;
    memcpy(&ipInputValue, ipInputPtr, sizeof(uintptr_t));
    result << "   ipInput value: 0x" << std::hex << ipInputValue << "\n";
    
    // خواندن hostButton (pointer)
    uintptr_t hostButtonValue = 0;
    memcpy(&hostButtonValue, hostButtonPtr, sizeof(uintptr_t));
    result << "   hostButton value: 0x" << std::hex << hostButtonValue << "\n";
    
    // خواندن joinButton (pointer)
    uintptr_t joinButtonValue = 0;
    memcpy(&joinButtonValue, joinButtonPtr, sizeof(uintptr_t));
    result << "   joinButton value: 0x" << std::hex << joinButtonValue << "\n\n";
    
    // ====== خواندن رشته از ipInput (اگر pointer معتبر باشه) ======
    if (ipInputValue != 0) {
        // خواندن 50 بایت از آدرس ipInputValue
        char buffer[51] = {0};
        memcpy(buffer, (void*)ipInputValue, 50);
        result << "   🔤 IP Input string: \"" << buffer << "\"\n";
    } else {
        result << "   🔤 IP Input string: (null)\n";
    }
    
    result << "\n========================================\n";
    
    g_lastCapture = result.str();
    write_capture(result.str());
    write_debug("Capture by offset completed");
    show_toast(env, obj, "✅ Capture done!\nCheck: /sdcard/Download/capture_log.txt", 1);
}

// ======================== منو ========================
jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    jobjectArray ret;
    const char *features[] = {
        OBFUSCATE("Category_📡 Capture Tool"),
        OBFUSCATE("Toggle_Capture Mode"),              // featNum: 0
        OBFUSCATE("Button_📡 Capture Offsets"),        // featNum: 1
        OBFUSCATE("Button_Show Last Capture"),         // featNum: 2
        OBFUSCATE("Button_Clear Logs"),                // featNum: 3
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

    switch (featNum) {
        case 0: // Toggle Capture Mode
            g_captureMode = (bool)boolean;
            write_debug(g_captureMode ? "Capture Mode: ON" : "Capture Mode: OFF");
            show_toast(env, obj, g_captureMode ? "✅ Capture Mode: ON" : "❌ Capture Mode: OFF", 0);
            break;

        case 1: // Button_📡 Capture Offsets
            show_toast(env, obj, "🔄 Capturing offsets...", 0);
            capture_by_offset(env, obj);
            break;

        case 2: // Button_Show Last Capture
            if (!g_lastCapture.empty()) {
                std::string firstLine = g_lastCapture.substr(0, g_lastCapture.find('\n'));
                if (firstLine.length() > 80) firstLine = firstLine.substr(0, 80) + "...";
                show_toast(env, obj, "📄 " + firstLine, 1);
            } else {
                show_toast(env, obj, "No capture yet!", 0);
            }
            break;

        case 3: // Button_Clear Logs
            {
                std::ofstream f1(g_captureLog);
                if (f1.is_open()) {
                    f1 << "Logs cleared at " << get_time() << "\n";
                    f1.close();
                }
                std::ofstream f2(g_debugLog);
                if (f2.is_open()) {
                    f2 << "Debug logs cleared at " << get_time() << "\n";
                    f2.close();
                }
                g_lastCapture = "";
                show_toast(env, obj, "✅ Logs cleared!", 0);
                write_debug("Logs cleared");
            }
            break;

        default:
            write_debug("Unknown featNum: " + std::to_string(featNum));
            break;
    }
}

// ======================== ترد اصلی ========================
void hack_thread() {
    const char* libName = "libil2cpp.so";
    int waitCount = 0;
    while (!isLibraryLoaded(targetLibName) && waitCount < 30) {
        sleep(1);
        waitCount++;
    }
    if (waitCount >= 30) {
        write_debug("Timeout waiting for libil2cpp.so");
        return;
    }
    write_debug("libil2cpp.so loaded!");
}

// ======================== تابع ورودی ========================
__attribute__((constructor))
void lib_main() {
    std::ofstream f(g_captureLog);
    if (f.is_open()) {
        f << "========== CAPTURE LOG ==========\n";
        f << "Started: " << get_time() << "\n";
        f << "================================\n\n";
        f.close();
    }
    std::ofstream f2(g_debugLog);
    if (f2.is_open()) {
        f2 << "========== DEBUG LOG ==========\n";
        f2 << "Started: " << get_time() << "\n";
        f2 << "==============================\n\n";
        f2.close();
    }
    std::thread(hack_thread).detach();
}