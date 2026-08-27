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

// ======================== آفست‌ها ========================
#define OFFSET_MENU_START     0xF6E0E8  // MenuControl.Start()
#define OFFSET_IP_INPUT       0xD8      // ipInput

// ======================== متغیرها ========================
static void* g_menuInstance = nullptr;
static std::string g_targetIP = "";
static std::string g_captureLog = "/sdcard/Download/capture_log.txt";
static std::string g_debugLog = "/sdcard/Download/capture_debug.txt";
static std::string g_lastIPFile = "/sdcard/Download/last_ip.txt";
static bool g_hasInstance = false;

// ======================== توابع کمکی ========================
static std::string get_time() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::string time_str = std::ctime(&time_t);
    time_str.pop_back();
    return time_str;
}

static void write_debug(const std::string& msg) {
    std::ofstream f(g_debugLog, std::ios::app);
    if (f.is_open()) {
        f << "[" << get_time() << "] " << msg << "\n";
        f.close();
    }
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

// ======================== تزریق IP از طریق JNI (با instance ذخیره شده) ========================
static void inject_ip_via_instance(JNIEnv *env, jobject obj) {
    if (g_menuInstance == nullptr) {
        write_debug("ERROR: menu instance is null!");
        show_toast(env, obj, "❌ Menu not loaded yet!", 0);
        return;
    }
    
    if (g_targetIP.empty()) {
        g_targetIP = load_ip();
        if (g_targetIP.empty()) {
            show_toast(env, obj, "❌ No IP found!", 0);
            return;
        }
    }
    
    write_debug("Injecting IP: " + g_targetIP + " into instance: " + std::to_string((uintptr_t)g_menuInstance));
    
    // ====== گرفتن کلاس از instance (بدون FindClass) ======
    jclass menuClass = env->GetObjectClass((jobject)g_menuInstance);
    if (menuClass == nullptr) {
        write_debug("ERROR: failed to get menu class from instance!");
        show_toast(env, obj, "❌ Menu class error!", 0);
        return;
    }
    
    // ====== پیدا کردن فیلد ipInput ======
    jfieldID ipField = env->GetFieldID(menuClass, "ipInput", "LUnityEngine/UI/InputField;");
    if (ipField == nullptr) {
        env->ExceptionClear();
        write_debug("ERROR: ipInput field not found!");
        show_toast(env, obj, "❌ ipInput not found!", 0);
        return;
    }
    
    // ====== گرفتن آبجکت ipInput ======
    jobject inputField = env->GetObjectField((jobject)g_menuInstance, ipField);
    if (inputField == nullptr) {
        write_debug("ERROR: ipInput object is null!");
        show_toast(env, obj, "❌ ipInput is null!", 0);
        return;
    }
    
    // ====== ست کردن IP ======
    jclass inputFieldClass = env->GetObjectClass(inputField);
    if (inputFieldClass == nullptr) {
        write_debug("ERROR: failed to get InputField class!");
        show_toast(env, obj, "❌ InputField error!", 0);
        return;
    }
    
    jmethodID setText = env->GetMethodID(inputFieldClass, "set_text", "(Ljava/lang/String;)V");
    if (setText == nullptr) {
        env->ExceptionClear();
        write_debug("ERROR: set_text method not found!");
        show_toast(env, obj, "❌ set_text not found!", 0);
        return;
    }
    
    jstring jip = env->NewStringUTF(g_targetIP.c_str());
    env->CallVoidMethod(inputField, setText, jip);
    env->DeleteLocalRef(jip);
    
    write_debug("✅ IP injected successfully!");
    show_toast(env, obj, "✅ IP Injected: " + g_targetIP, 1);
}

// ======================== هوک روی Menu.Start() ========================
void (*orig_MenuStart)(void *instance);
void hook_MenuStart(void *instance) {
    write_debug("========================================");
    write_debug("🎯 Menu.Start() CALLED!");
    write_debug("   instance: " + std::to_string((uintptr_t)instance));
    
    // ذخیره instance
    g_menuInstance = instance;
    g_hasInstance = true;
    
    // بارگذاری IP
    g_targetIP = load_ip();
    if (!g_targetIP.empty()) {
        write_debug("   loaded IP: " + g_targetIP);
    }
    
    write_debug("========================================");
    
    // صدا زدن تابع اصلی
    if (orig_MenuStart) {
        orig_MenuStart(instance);
    }
}

// ======================== منو ========================
jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    jobjectArray ret;
    const char *features[] = {
        OBFUSCATE("Category_🌐 Network Tools"),
        OBFUSCATE("InputText_Enter IP"),           // featNum: 0
        OBFUSCATE("Button_Inject IP"),             // featNum: 1
        OBFUSCATE("Button_Status"),                // featNum: 2
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
                show_toast(env, obj, "✅ IP saved: " + g_targetIP, 0);
                write_debug("IP saved: " + g_targetIP);
            }
            break;

        case 1: // Button_Inject IP
            write_debug("Inject button pressed");
            if (g_hasInstance && g_menuInstance != nullptr) {
                inject_ip_via_instance(env, obj);
            } else {
                show_toast(env, obj, "⏳ Wait for menu to load...", 0);
                write_debug("WARNING: Menu not loaded yet!");
            }
            break;

        case 2: // Button_Status
            {
                std::string status = "📊 Status:\n";
                status += "  Menu: " + std::string(g_hasInstance ? "✅" : "❌") + "\n";
                status += "  IP: " + (g_targetIP.empty() ? "(empty)" : g_targetIP);
                show_toast(env, obj, status, 1);
                write_debug("Status shown: hasInstance=" + std::to_string(g_hasInstance));
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
    const char* libName = "libil2cpp.so";
    int waitCount = 0;
    
    write_debug("Waiting for libil2cpp.so...");
    while (!isLibraryLoaded(targetLibName) && waitCount < 30) {
        sleep(1);
        waitCount++;
    }
    
    if (waitCount >= 30) {
        write_debug("Timeout waiting for libil2cpp.so");
        return;
    }
    
    write_debug("libil2cpp.so loaded!");
    
    // بارگذاری IP
    g_targetIP = load_ip();
    if (!g_targetIP.empty()) {
        write_debug("Loaded IP: " + g_targetIP);
    }
    
#if defined(__aarch64__)
    // ====== هوک روی Menu.Start() ======
    void *menuStartAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0xF6E0E8"));
    if (menuStartAddr != nullptr) {
        int res = DobbyHook(menuStartAddr, (dobby_dummy_func_t)hook_MenuStart, (dobby_dummy_func_t*)&orig_MenuStart);
        if (res == 0) {
            write_debug("✅ Menu.Start() hooked at: " + std::to_string((uintptr_t)menuStartAddr));
        } else {
            write_debug("❌ Failed to hook Menu.Start()! error: " + std::to_string(res));
        }
    } else {
        write_debug("❌ Menu.Start() address not found!");
    }
#endif
    
    write_debug("Hack thread finished");
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
    write_debug("lib_main called");
    std::thread(hack_thread).detach();
}