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

#define targetLibName OBFUSCATE("libil2cpp.so")

static std::string g_lastIPFile = "/sdcard/Download/last_ip.txt";
static std::string g_networkLog = "/sdcard/Download/network_log.txt";
static std::string g_debugLog = "/sdcard/Download/mod_debug.txt";
static std::string g_targetIP = "";

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

// ======================== تزریق IP با کلاس مشخص ========================
static void inject_ip_with_class(JNIEnv *env, jobject obj, const std::string& ip, const char* className, const char* fieldName, const char* fieldType) {
    if (env == nullptr || obj == nullptr) return;

    jclass targetClass = env->FindClass(className);
    if (targetClass == nullptr) {
        show_toast(env, obj, std::string(className) + " not found!", 0);
        return;
    }

    jfieldID field = env->GetFieldID(targetClass, fieldName, fieldType);
    if (field == nullptr) {
        show_toast(env, obj, std::string(fieldName) + " not found in " + className, 0);
        return;
    }

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

    jobject instance = env->CallStaticObjectMethod(unityObjectClass, findObjectMethod, targetClass);
    if (instance == nullptr) {
        show_toast(env, obj, std::string(className) + " instance not found!", 0);
        return;
    }

    jobject inputField = env->GetObjectField(instance, field);
    if (inputField == nullptr) {
        show_toast(env, obj, std::string(fieldName) + " is null!", 0);
        return;
    }

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

    show_toast(env, obj, std::string("✅ ").append(className).append(": ").append(ip), 1);
}

// ======================== لیست ویژگی‌های منو ========================
jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    jobjectArray ret;
    const char *features[] = {
        OBFUSCATE("Category_🌐 Network Tools"),
        OBFUSCATE("InputText_Enter IP Address"),           // featNum: 0
        
        OBFUSCATE("Button_Apply: MenuControl"),            // featNum: 1
        OBFUSCATE("Button_Apply: GtaMenuControl"),         // featNum: 2
        OBFUSCATE("Button_Apply: ServerListAccess"),       // featNum: 3
        OBFUSCATE("Button_Apply: AutoRunLinuxServer"),     // featNum: 4
        
        OBFUSCATE("Button_Show Saved IP"),                 // featNum: 5
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
        case 0: // Input IP
            if (textStr != nullptr) {
                g_targetIP = textStr;
                save_ip_to_file(g_targetIP);
                show_toast(env, obj, "IP saved: " + g_targetIP, 0);
            }
            break;

        case 1: // Button_Apply: MenuControl
            if (g_targetIP.empty()) g_targetIP = load_ip_from_file();
            if (!g_targetIP.empty()) {
                inject_ip_with_class(env, obj, g_targetIP, "MenuControl", "ipInput", "LUnityEngine/UI/InputField;");
            } else {
                show_toast(env, obj, "No IP found!", 0);
            }
            break;

        case 2: // Button_Apply: GtaMenuControl
            if (g_targetIP.empty()) g_targetIP = load_ip_from_file();
            if (!g_targetIP.empty()) {
                inject_ip_with_class(env, obj, g_targetIP, "GtaMenuControl", "ipInput", "LUnityEngine/UI/InputField;");
            } else {
                show_toast(env, obj, "No IP found!", 0);
            }
            break;

        case 3: // Button_Apply: ServerListAccess
            if (g_targetIP.empty()) g_targetIP = load_ip_from_file();
            if (!g_targetIP.empty()) {
                inject_ip_with_class(env, obj, g_targetIP, "ServerListAccess", "ipAddressInput", "LUnityEngine/UI/InputField;");
            } else {
                show_toast(env, obj, "No IP found!", 0);
            }
            break;

        case 4: // Button_Apply: AutoRunLinuxServer (از طریق _menuControl)
            if (g_targetIP.empty()) g_targetIP = load_ip_from_file();
            if (!g_targetIP.empty()) {
                jclass targetClass = env->FindClass("AutoRunLinuxServer");
                if (targetClass == nullptr) {
                    show_toast(env, obj, "AutoRunLinuxServer not found!", 0);
                    break;
                }
                
                jfieldID menuControlField = env->GetFieldID(targetClass, "_menuControl", "LGtaMenuControl;");
                if (menuControlField == nullptr) {
                    show_toast(env, obj, "_menuControl not found!", 0);
                    break;
                }
                
                jclass unityObjectClass = env->FindClass("UnityEngine/Object");
                jmethodID findObjectMethod = env->GetStaticMethodID(unityObjectClass, "FindObjectOfType", "(Ljava/lang/Class;)Ljava/lang/Object;");
                jobject instance = env->CallStaticObjectMethod(unityObjectClass, findObjectMethod, targetClass);
                if (instance == nullptr) {
                    show_toast(env, obj, "AutoRunLinuxServer instance not found!", 0);
                    break;
                }
                
                jobject menuControl = env->GetObjectField(instance, menuControlField);
                if (menuControl == nullptr) {
                    show_toast(env, obj, "_menuControl is null!", 0);
                    break;
                }
                
                jclass gtaMenuClass = env->FindClass("GtaMenuControl");
                jfieldID ipField = env->GetFieldID(gtaMenuClass, "ipInput", "LUnityEngine/UI/InputField;");
                if (ipField == nullptr) {
                    show_toast(env, obj, "ipInput not found in GtaMenuControl!", 0);
                    break;
                }
                
                jobject inputField = env->GetObjectField(menuControl, ipField);
                if (inputField == nullptr) {
                    show_toast(env, obj, "ipInput is null!", 0);
                    break;
                }
                
                jclass inputFieldClass = env->FindClass("UnityEngine/UI/InputField");
                jmethodID setText = env->GetMethodID(inputFieldClass, "set_text", "(Ljava/lang/String;)V");
                if (setText == nullptr) {
                    show_toast(env, obj, "set_text not found!", 0);
                    break;
                }
                
                jstring jip = env->NewStringUTF(g_targetIP.c_str());
                env->CallVoidMethod(inputField, setText, jip);
                env->DeleteLocalRef(jip);
                show_toast(env, obj, "✅ AutoRunLinuxServer: " + g_targetIP, 1);
            } else {
                show_toast(env, obj, "No IP found!", 0);
            }
            break;

        case 5: // Show Saved IP
            {
                std::string savedIP = load_ip_from_file();
                if (!savedIP.empty()) {
                    show_toast(env, obj, "Saved IP: " + savedIP, 1);
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

void hack_thread() {
    const char* libName = "libil2cpp.so";
    int waitCount = 0;
    while (!isLibraryLoaded(targetLibName) && waitCount < 30) {
        sleep(1);
        waitCount++;
    }
    if (waitCount >= 30) return;
    std::string savedIP = load_ip_from_file();
    if (!savedIP.empty()) {
        g_targetIP = savedIP;
        LOGI("[Network] Loaded saved IP: %s", savedIP.c_str());
    }
}

__attribute__((constructor))
void lib_main() {
    std::ofstream f(g_debugLog);
    if (f.is_open()) {
        f << "========== MOD LOADED ==========\n";
        f << "Time: " << get_current_time() << "\n";
        f << "===============================\n\n";
        f.close();
    }
    std::thread(hack_thread).detach();
}