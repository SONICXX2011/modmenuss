#include <string>
#include <jni.h>
#include <unistd.h>
#include <fstream>
#include <thread>
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

// ======================== متغیرهای سراسری ========================
static std::string g_targetIP = "";
static std::string g_debugLog = "/sdcard/Download/mod_debug.txt";
static std::string g_lastIPFile = "/sdcard/Download/last_ip.txt";
static bool g_menuReady = false;

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
        write_debug("IP saved: " + ip);
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

// ======================== تزریق IP (امن‌ترین روش) ========================
static void inject_ip_to_game(JNIEnv *env, jobject obj) {
    // چک اولیه
    if (env == nullptr || obj == nullptr) {
        write_debug("❌ inject_ip_to_game: env or obj is null");
        return;
    }
    
    // بارگذاری IP
    if (g_targetIP.empty()) {
        g_targetIP = load_ip();
        if (g_targetIP.empty()) {
            show_toast(env, obj, "❌ No IP found!", 0);
            return;
        }
    }
    
    write_debug("🔄 Injecting IP: " + g_targetIP);
    
    // ====== ۱. پیدا کردن کلاس (با ۲ روش) ======
    jclass menuClass = nullptr;
    const char* classNames[] = {"GtaMenuControl", "MenuControl"};
    
    for (const char* className : classNames) {
        menuClass = env->FindClass(className);
        if (menuClass != nullptr) {
            env->ExceptionClear();
            write_debug("✅ Found class: " + std::string(className));
            break;
        }
        env->ExceptionClear();
    }
    
    if (menuClass == nullptr) {
        show_toast(env, obj, "❌ Menu class not found!", 0);
        write_debug("❌ No menu class found!");
        return;
    }
    
    // ====== ۲. پیدا کردن فیلد ipInput (با ۲ اسم) ======
    jfieldID ipField = env->GetFieldID(menuClass, "ipInput", "LUnityEngine/UI/InputField;");
    if (ipField == nullptr) {
        env->ExceptionClear();
        ipField = env->GetFieldID(menuClass, "ipAddressInput", "LUnityEngine/UI/InputField;");
        if (ipField == nullptr) {
            env->ExceptionClear();
            show_toast(env, obj, "❌ ipInput field not found!", 0);
            write_debug("❌ ipInput field not found!");
            return;
        }
    }
    
    // ====== ۳. پیدا کردن instance ======
    jclass unityObjectClass = env->FindClass("UnityEngine/Object");
    if (unityObjectClass == nullptr) {
        env->ExceptionClear();
        show_toast(env, obj, "❌ UnityEngine.Object not found!", 0);
        return;
    }
    
    jmethodID findObjectMethod = env->GetStaticMethodID(
        unityObjectClass,
        "FindObjectOfType",
        "(Ljava/lang/Class;)Ljava/lang/Object;"
    );
    if (findObjectMethod == nullptr) {
        env->ExceptionClear();
        show_toast(env, obj, "❌ FindObjectOfType not found!", 0);
        return;
    }
    
    jobject menuInstance = env->CallStaticObjectMethod(unityObjectClass, findObjectMethod, menuClass);
    if (menuInstance == nullptr) {
        show_toast(env, obj, "⏳ Menu not loaded yet!", 0);
        write_debug("⏳ Menu instance is null (not ready)");
        return;
    }
    
    // ====== ۴. گرفتن ipInput ======
    jobject inputField = env->GetObjectField(menuInstance, ipField);
    if (inputField == nullptr) {
        show_toast(env, obj, "❌ ipInput is null!", 0);
        write_debug("❌ ipInput object is null");
        return;
    }
    
    // ====== ۵. ست کردن IP ======
    jclass inputFieldClass = env->GetObjectClass(inputField);
    if (inputFieldClass == nullptr) {
        show_toast(env, obj, "❌ InputField class error!", 0);
        return;
    }
    
    jmethodID setText = env->GetMethodID(inputFieldClass, "set_text", "(Ljava/lang/String;)V");
    if (setText == nullptr) {
        env->ExceptionClear();
        show_toast(env, obj, "❌ set_text not found!", 0);
        return;
    }
    
    jstring jip = env->NewStringUTF(g_targetIP.c_str());
    env->CallVoidMethod(inputField, setText, jip);
    env->DeleteLocalRef(jip);
    
    g_menuReady = true;
    write_debug("✅ IP injected successfully: " + g_targetIP);
    show_toast(env, obj, "✅ IP Injected: " + g_targetIP, 1);
}

// ======================== هوک روی GtaMenuControl.Start ========================
void (*orig_GtaMenuStart)(void *instance);
void hook_GtaMenuStart(void *instance) {
    write_debug("🎯 GtaMenuControl.Start() called!");
    
    if (instance != nullptr) {
        g_menuReady = true;
        write_debug("✅ Menu instance saved: " + std::to_string((uintptr_t)instance));
        
        // بارگذاری IP
        g_targetIP = load_ip();
        if (!g_targetIP.empty()) {
            write_debug("📌 Loaded IP: " + g_targetIP);
        }
    }
    
    // صدا زدن تابع اصلی
    if (orig_GtaMenuStart) {
        orig_GtaMenuStart(instance);
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
            inject_ip_to_game(env, obj);
            break;

        case 2: // Button_Status
            {
                std::string status = "📊 Status:\n";
                status += "  Menu ready: " + std::string(g_menuReady ? "✅" : "❌") + "\n";
                status += "  IP: " + (g_targetIP.empty() ? "(empty)" : g_targetIP);
                show_toast(env, obj, status, 1);
                write_debug("Status shown");
            }
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
        write_debug("⏰ Timeout waiting for " + std::string(libName) + " (menu still works)");
        return;
    }
    
    write_debug("✅ " + std::string(libName) + " loaded!");
    
    // بارگذاری IP
    g_targetIP = load_ip();
    if (!g_targetIP.empty()) {
        write_debug("📌 Loaded IP from file: " + g_targetIP);
    }
    
#if defined(__aarch64__)
    // ====== هوک روی GtaMenuControl.Start ======
    void *startAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0xF7D060"));
    if (startAddr != nullptr) {
        int res = DobbyHook(startAddr, (dobby_dummy_func_t)hook_GtaMenuStart, (dobby_dummy_func_t*)&orig_GtaMenuStart);
        if (res == 0) {
            write_debug("✅ GtaMenuControl.Start() hooked at: 0x" + std::to_string((uintptr_t)startAddr));
        } else {
            write_debug("❌ Failed to hook GtaMenuControl.Start()! error: " + std::to_string(res));
        }
    } else {
        write_debug("❌ GtaMenuControl.Start() address not found!");
    }
#endif
    
    write_debug("✅ hack_thread finished");
}

// ======================== تابع ورودی ========================
__attribute__((constructor))
void lib_main() {
    // ایجاد فایل دیباگ
    std::ofstream f(g_debugLog);
    if (f.is_open()) {
        f << "========== MOD LOADED ==========\n";
        f << "Time: " << get_time() << "\n";
        f << "===============================\n\n";
        f.close();
    }
    
    write_debug("🚀 lib_main called");
    std::thread(hack_thread).detach();
}