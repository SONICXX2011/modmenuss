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

// ======================== تعریف لایب با OBFUSCATE (برای هوک) ========================
#define targetLibName OBFUSCATE("libil2cpp.so")

// ======================== مسیرهای ذخیره‌سازی ========================
static std::string g_lastIPFile = "/sdcard/Download/last_ip.txt";
static std::string g_networkLog = "/sdcard/Download/network_log.txt";
static std::string g_debugLog = "/sdcard/Download/mod_debug.txt";

// ======================== متغیرهای سراسری ========================
static std::string g_targetIP = "";

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
        log_network(("IP saved: " + ip).c_str());
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

// ======================== نمایش Toast با JNI ========================
static void show_toast(JNIEnv *env, jobject obj, const std::string& msg, int length) {
    if (env == nullptr || obj == nullptr) {
        return;
    }
    
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

// ======================== تزریق IP به بازی (با JNI) ========================
static void inject_ip_to_game(JNIEnv *env, jobject obj, const std::string& ip) {
    if (env == nullptr || obj == nullptr) {
        return;
    }

    // پیدا کردن کلاس MenuControl
    jclass menuClass = env->FindClass("MenuControl");
    if (menuClass == nullptr) {
        show_toast(env, obj, "MenuControl not found!", 0);
        return;
    }

    // پیدا کردن فیلد ipInput
    jfieldID ipField = env->GetFieldID(menuClass, "ipInput", "LUnityEngine/UI/InputField;");
    if (ipField == nullptr) {
        show_toast(env, obj, "ipInput not found!", 0);
        return;
    }

    // پیدا کردن instance MenuControl با FindObjectOfType
    jclass unityObjectClass = env->FindClass("UnityEngine/Object");
    if (unityObjectClass == nullptr) {
        show_toast(env, obj, "UnityEngine.Object not found!", 0);
        return;
    }

    jmethodID findObjectMethod = env->GetStaticMethodID(
        unityObjectClass,
        "FindObjectOfType",
        "(Ljava/lang/Class;)Ljava/lang/Object;"
    );
    if (findObjectMethod == nullptr) {
        show_toast(env, obj, "FindObjectOfType not found!", 0);
        return;
    }

    jobject menuInstance = env->CallStaticObjectMethod(unityObjectClass, findObjectMethod, menuClass);
    if (menuInstance == nullptr) {
        show_toast(env, obj, "MenuControl instance not found!", 0);
        return;
    }

    // گرفتن آبجکت ipInput
    jobject inputField = env->GetObjectField(menuInstance, ipField);
    if (inputField == nullptr) {
        show_toast(env, obj, "ipInput is null!", 0);
        return;
    }

    // ست کردن IP با متد set_text
    jclass inputFieldClass = env->FindClass("UnityEngine/UI/InputField");
    if (inputFieldClass == nullptr) {
        show_toast(env, obj, "InputField class not found!", 0);
        return;
    }

    jmethodID setText = env->GetMethodID(inputFieldClass, "set_text", "(Ljava/lang/String;)V");
    if (setText == nullptr) {
        show_toast(env, obj, "set_text not found!", 0);
        return;
    }

    jstring jip = env->NewStringUTF(ip.c_str());
    env->CallVoidMethod(inputField, setText, jip);
    env->DeleteLocalRef(jip);

    show_toast(env, obj, "IP Injected: " + ip, 1);
}

// ======================== لیست ویژگی‌های منو ========================
jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    jobjectArray ret;
    const char *features[] = {
        OBFUSCATE("Category_🌐 Network Tools"),
        OBFUSCATE("InputText_Enter IP Address"),    // featNum: 0
        OBFUSCATE("Button_Apply IP"),               // featNum: 1
        OBFUSCATE("Button_Show Saved IP"),          // featNum: 2
        OBFUSCATE("RichTextView_IP Status: <font color='yellow'>Ready</font>"),
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
        case 0: // InputText_Enter IP Address
            if (textStr != nullptr) {
                g_targetIP = textStr;
                save_ip_to_file(g_targetIP);
                show_toast(env, obj, "IP saved: " + g_targetIP, 0);
            } else {
                show_toast(env, obj, "No IP entered!", 0);
            }
            break;

        case 1: // Button_Apply IP
            if (g_targetIP.empty()) {
                g_targetIP = load_ip_from_file();
            }
            if (!g_targetIP.empty()) {
                inject_ip_to_game(env, obj, g_targetIP);
            } else {
                show_toast(env, obj, "No IP found! Enter IP first.", 0);
            }
            break;

        case 2: // Button_Show Saved IP
            {
                std::string savedIP = load_ip_from_file();
                if (!savedIP.empty()) {
                    show_toast(env, obj, "Saved IP: " + savedIP, 1);
                } else {
                    show_toast(env, obj, "No saved IP found!", 0);
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

// ======================== ترد اصلی (منتظر لود شدن لایب) ========================
void hack_thread() {
    // استفاده از متغیر معمولی برای لاگ (نه OBFUSCATE)
    const char* libName = "libil2cpp.so";
    
    write_debug("Init", "hack_thread started");

    int waitCount = 0;
    LOGI("[Network] Waiting for %s ...", libName);
    while (!isLibraryLoaded(targetLibName) && waitCount < 30) {
        sleep(1);
        waitCount++;
    }

    if (waitCount >= 30) {
        LOGI("[Network] Timeout: %s not loaded", libName);
        write_debug("ERROR", "Timeout waiting for libil2cpp.so");
        return;
    }

    LOGI("[Network] %s loaded!", libName);
    write_debug("Init", "libil2cpp.so loaded");

    // بارگذاری IP ذخیره‌شده از فایل
    std::string savedIP = load_ip_from_file();
    if (!savedIP.empty()) {
        g_targetIP = savedIP;
        LOGI("[Network] Loaded saved IP: %s", savedIP.c_str());
        write_debug("IP", ("Loaded saved IP: " + savedIP).c_str());
    }

    LOGI("[Network] hack_thread finished");
    write_debug("Init", "hack_thread finished");
}

// ======================== تابع ورودی (constructor) ========================
__attribute__((constructor))
void lib_main() {
    // ایجاد فایل دیباگ
    std::ofstream f(g_debugLog);
    if (f.is_open()) {
        f << "========== MOD LOADED ==========\n";
        f << "Time: " << get_current_time() << "\n";
        f << "===============================\n\n";
        f.close();
    }

    // ایجاد فایل لاگ شبکه
    std::ofstream f2(g_networkLog);
    if (f2.is_open()) {
        f2 << "========== NETWORK LOG ==========\n";
        f2 << "Time: " << get_current_time() << "\n";
        f2 << "================================\n\n";
        f2.close();
    }

    write_debug("Init", "lib_main called");
    std::thread(hack_thread).detach();
}