#include <string>
#include <jni.h>
#include <unistd.h>
#include <fstream>
#include <thread>
#include <chrono>
#include <ctime>
#include <sstream>
#include <vector>
#include "Includes/Logger.h"
#include "Includes/obfuscate.h"
#include "Includes/Utils.hpp"
#include "Menu/Menu.hpp"
#include "Menu/Jni.hpp"

#define targetLibName OBFUSCATE("libil2cpp.so")

// ======================== مسیرها ========================
static std::string g_captureLog = "/sdcard/Download/capture_log.txt";
static std::string g_debugLog = "/sdcard/Download/capture_debug.txt";

// ======================== متغیرها ========================
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

// ======================== کپچر امن (بدون کرش) ========================
static void safe_capture_all(JNIEnv *env, jobject obj) {
    std::stringstream result;
    result << "========== CAPTURE REPORT ==========\n";
    result << "Time: " << get_time() << "\n\n";
    
    // لیست کلاس‌هایی که احتمال دارن توی بازی باشن
    std::vector<std::string> classNames = {
        "GtaMenuControl",
        "ServerListAccess", 
        "AutoRunLinuxServer",
        "CustomNetworkManager",
        "MenuControl",
        "PauseMenu",
        "CustomCode",
        "ModManager",
        "ServerGameManager"
    };
    
    int foundCount = 0;
    result << "🔍 Searching for classes...\n\n";
    
    for (const auto& className : classNames) {
        // با ExceptionClear امن شده
        jclass targetClass = env->FindClass(className.c_str());
        if (targetClass == nullptr) {
            env->ExceptionClear();
            result << "❌ " << className << " → NOT FOUND\n";
            continue;
        }
        
        foundCount++;
        result << "✅ " << className << " → FOUND\n";
        
        // گرفتن instance
        jclass unityObjectClass = env->FindClass("UnityEngine/Object");
        if (unityObjectClass != nullptr) {
            jmethodID findObjectMethod = env->GetStaticMethodID(
                unityObjectClass,
                "FindObjectOfType",
                "(Ljava/lang/Class;)Ljava/lang/Object;"
            );
            if (findObjectMethod != nullptr) {
                jobject instance = env->CallStaticObjectMethod(unityObjectClass, findObjectMethod, targetClass);
                if (instance != nullptr) {
                    result << "   ├─ Instance: " << instance << " (exists)\n";
                    
                    // گرفتن GameObject
                    jclass monoBehaviourClass = env->FindClass("UnityEngine/MonoBehaviour");
                    if (monoBehaviourClass != nullptr && env->IsInstanceOf(instance, monoBehaviourClass)) {
                        jmethodID getGameObject = env->GetMethodID(monoBehaviourClass, "get_gameObject", "()LUnityEngine/GameObject;");
                        if (getGameObject != nullptr) {
                            jobject gameObject = env->CallObjectMethod(instance, getGameObject);
                            if (gameObject != nullptr) {
                                jmethodID getObjectName = env->GetMethodID(env->FindClass("UnityEngine/GameObject"), "get_name", "()Ljava/lang/String;");
                                if (getObjectName != nullptr) {
                                    jstring nameObj = (jstring)env->CallObjectMethod(gameObject, getObjectName);
                                    const char* nameCStr = env->GetStringUTFChars(nameObj, nullptr);
                                    result << "   └─ GameObject: " << nameCStr << "\n";
                                    env->ReleaseStringUTFChars(nameObj, nameCStr);
                                }
                            }
                        }
                    }
                } else {
                    result << "   └─ Instance: null (not created yet)\n";
                }
            }
        }
        result << "\n";
    }
    
    result << "📊 Summary: " << foundCount << " classes found out of " << classNames.size() << "\n";
    result << "========================================\n";
    
    g_lastCapture = result.str();
    write_capture(result.str());
    write_debug("Capture completed, found " + std::to_string(foundCount) + " classes");
    
    std::string toastMsg = "✅ " + std::to_string(foundCount) + " classes found!\nCheck: /sdcard/Download/capture_log.txt";
    show_toast(env, obj, toastMsg, 1);
}

// ======================== منو (بدون آیپی) ========================
jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    jobjectArray ret;
    const char *features[] = {
        OBFUSCATE("Category_📡 Capture Tool"),
        OBFUSCATE("Toggle_Capture Mode"),              // featNum: 0
        OBFUSCATE("Button_📡 Capture All Classes"),    // featNum: 1
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

        case 1: // Button_📡 Capture All Classes
            show_toast(env, obj, "🔄 Capturing...", 0);
            safe_capture_all(env, obj);
            break;

        case 2: // Button_Show Last Capture
            if (!g_lastCapture.empty()) {
                // فقط خط اول رو توی Toast نشون بده
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