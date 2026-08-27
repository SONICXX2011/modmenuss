#include <string>
#include <jni.h>
#include <unistd.h>
#include <fstream>
#include <thread>
#include <chrono>
#include <ctime>
#include <sstream>
#include <vector>
#include <map>
#include "Includes/Logger.h"
#include "Includes/obfuscate.h"
#include "Includes/Utils.hpp"
#include "Menu/Menu.hpp"
#include "Menu/Jni.hpp"

#define targetLibName OBFUSCATE("libil2cpp.so")

// ======================== مسیرها ========================
static std::string g_captureLog = "/sdcard/Download/capture_log.txt";
static std::string g_debugLog = "/sdcard/Download/capture_debug.txt";
static std::string g_lastIPFile = "/sdcard/Download/last_ip.txt";

// ======================== متغیرها ========================
static std::string g_targetIP = "";
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

// ======================== کپچر رویداد (با هوک خودکار) ========================
static void capture_event(JNIEnv *env, const char* className, const char* methodName, const char* params) {
    if (!g_captureMode) return;
    
    std::stringstream ss;
    ss << "📡 EVENT CAPTURED\n";
    ss << "  Class: " << className << "\n";
    ss << "  Method: " << methodName << "\n";
    ss << "  Params: " << (params ? params : "()") << "\n";
    ss << "  Time: " << get_time() << "\n";
    ss << "----------------------------------------\n";
    
    std::string result = ss.str();
    g_lastCapture = result;
    write_capture(result);
    write_debug(("Event: " + std::string(className) + "::" + methodName).c_str());
}

// ======================== دکمه‌های منو ========================
jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    jobjectArray ret;
    const char *features[] = {
        OBFUSCATE("Category_🌐 Network Tools"),
        OBFUSCATE("InputText_Enter IP Address"),           // featNum: 0
        
        OBFUSCATE("Toggle_Capture Mode"),                  // featNum: 1
        OBFUSCATE("Button_📡 Capture Now"),                // featNum: 2
        OBFUSCATE("Button_Show Last Capture"),             // featNum: 3
        OBFUSCATE("Button_Clear Logs"),                    // featNum: 4
        
        OBFUSCATE("Button_Show Saved IP"),                 // featNum: 5
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
                std::ofstream f(g_lastIPFile);
                if (f.is_open()) {
                    f << g_targetIP << "\n";
                    f.close();
                }
                show_toast(env, obj, "IP saved: " + g_targetIP, 0);
                write_debug("IP saved: " + g_targetIP);
            }
            break;

        case 1: // Toggle Capture Mode
            g_captureMode = (bool)boolean;
            write_debug(g_captureMode ? "Capture Mode: ON" : "Capture Mode: OFF");
            show_toast(env, obj, g_captureMode ? "✅ Capture Mode: ON" : "❌ Capture Mode: OFF", 0);
            break;

        case 2: // Button_📡 Capture Now
            {
                show_toast(env, obj, "🔄 Capturing current event...", 0);
                
                // کپچر از کلاس‌های مختلف
                std::vector<std::string> classes = {"MenuControl", "GtaMenuControl", "ServerListAccess", "AutoRunLinuxServer"};
                std::string allResults = "========== CAPTURE ALL ==========\n";
                allResults += "Time: " + get_time() + "\n\n";
                
                for (const auto& className : classes) {
                    // تلاش برای پیدا کردن کلاس
                    jclass targetClass = env->FindClass(className.c_str());
                    if (targetClass != nullptr) {
                        allResults += "✅ Class found: " + className + "\n";
                        allResults += "   Instance: " + std::to_string((uintptr_t)targetClass) + "\n";
                        
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
                                    allResults += "   Instance found: " + std::to_string((uintptr_t)instance) + "\n";
                                } else {
                                    allResults += "   Instance: null (not created yet)\n";
                                }
                            }
                        }
                    } else {
                        allResults += "❌ Class not found: " + className + "\n";
                    }
                    allResults += "\n";
                }
                
                g_lastCapture = allResults;
                write_capture(allResults);
                show_toast(env, obj, "✅ Capture completed!\nCheck: /sdcard/Download/capture_log.txt", 1);
                write_debug("Manual capture completed");
            }
            break;

        case 3: // Show Last Capture
            if (!g_lastCapture.empty()) {
                std::string firstLine = g_lastCapture.substr(0, g_lastCapture.find('\n'));
                if (firstLine.length() > 80) firstLine = firstLine.substr(0, 80) + "...";
                show_toast(env, obj, "📄 " + firstLine, 1);
            } else {
                show_toast(env, obj, "No capture yet!", 0);
            }
            break;

        case 4: // Clear Logs
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

        case 5: // Show Saved IP
            {
                std::ifstream f(g_lastIPFile);
                if (f.is_open()) {
                    std::string ip;
                    std::getline(f, ip);
                    f.close();
                    if (!ip.empty()) {
                        show_toast(env, obj, "Saved IP: " + ip, 1);
                    } else {
                        show_toast(env, obj, "No saved IP!", 0);
                    }
                } else {
                    show_toast(env, obj, "No saved IP!", 0);
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

// ======================== هوک‌های خودکار برای کپچر رویدادها ========================
// این تابع توسط هوک‌ها صدا زده میشه
extern "C" void on_event_captured(JNIEnv *env, const char* className, const char* methodName, const char* params) {
    capture_event(env, className, methodName, params);
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
    
    std::ifstream f(g_lastIPFile);
    if (f.is_open()) {
        std::string ip;
        std::getline(f, ip);
        f.close();
        if (!ip.empty()) {
            g_targetIP = ip;
            write_debug("Loaded saved IP: " + ip);
        }
    }
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