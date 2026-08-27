#include <string>
#include <jni.h>
#include <unistd.h>
#include <fstream>
#include <thread>
#include <chrono>
#include <ctime>
#include <vector>
#include <sstream>
#include "Includes/Logger.h"
#include "Includes/obfuscate.h"
#include "Includes/Utils.hpp"
#include "Menu/Menu.hpp"
#include "Menu/Jni.hpp"

#define targetLibName OBFUSCATE("libil2cpp.so")

// ======================== مسیرها ========================
static std::string g_logPath = "/sdcard/Download/capture_log.txt";
static std::string g_debugPath = "/sdcard/Download/capture_debug.txt";
static std::string g_lastIPFile = "/sdcard/Download/last_ip.txt";

// ======================== متغیرها ========================
static std::string g_targetIP = "";
static std::string g_lastCapture = "";

// ======================== توابع کمکی ========================
static std::string get_time() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::string time_str = std::ctime(&time_t);
    time_str.pop_back();
    return time_str;
}

static void write_log(const std::string& msg) {
    std::ofstream f(g_logPath, std::ios::app);
    if (f.is_open()) {
        f << "[" << get_time() << "] " << msg << "\n";
        f.close();
    }
}

static void write_debug(const std::string& msg) {
    std::ofstream f(g_debugPath, std::ios::app);
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

// ======================== تابع اصلی Capture ========================
static std::string capture_class_info(JNIEnv *env, const char* className) {
    std::stringstream result;
    result << "========== CAPTURE: " << className << " ==========\n";
    result << "Time: " << get_time() << "\n\n";

    // پیدا کردن کلاس
    jclass targetClass = env->FindClass(className);
    if (targetClass == nullptr) {
        result << "❌ Class not found: " << className << "\n";
        write_debug("Class not found: " + std::string(className));
        return result.str();
    }
    result << "✅ Class found: " << className << "\n";

    // پیدا کردن instance با FindObjectOfType
    jclass unityObjectClass = env->FindClass("UnityEngine/Object");
    if (unityObjectClass == nullptr) {
        result << "❌ UnityEngine.Object class not found!\n";
        return result.str();
    }

    jmethodID findObjectMethod = env->GetStaticMethodID(
        unityObjectClass,
        "FindObjectOfType",
        "(Ljava/lang/Class;)Ljava/lang/Object;"
    );
    if (findObjectMethod == nullptr) {
        result << "❌ FindObjectOfType method not found!\n";
        return result.str();
    }

    jobject instance = env->CallStaticObjectMethod(unityObjectClass, findObjectMethod, targetClass);
    if (instance == nullptr) {
        result << "⚠️ No instance of " << className << " found (maybe not created yet)\n";
        result << "========================================\n";
        return result.str();
    }
    result << "✅ Instance found at: " << instance << "\n\n";

    // گرفتن اطلاعات کلاس
    jclass instanceClass = env->GetObjectClass(instance);
    
    // 1. اسم کلاس کامل
    jmethodID getClassMethod = env->GetMethodID(instanceClass, "getClass", "()Ljava/lang/Class;");
    jobject classObj = env->CallObjectMethod(instance, getClassMethod);
    jmethodID getNameMethod = env->GetMethodID(env->FindClass("java/lang/Class"), "getName", "()Ljava/lang/String;");
    jstring nameStr = (jstring)env->CallObjectMethod(classObj, getNameMethod);
    const char* nameCStr = env->GetStringUTFChars(nameStr, nullptr);
    result << "Full Class Name: " << nameCStr << "\n";
    env->ReleaseStringUTFChars(nameStr, nameCStr);

    // 2. گرفتن فیلدها
    jmethodID getDeclaredFields = env->GetMethodID(
        env->FindClass("java/lang/Class"),
        "getDeclaredFields",
        "()[Ljava/lang/reflect/Field;"
    );
    jobjectArray fields = (jobjectArray)env->CallObjectMethod(classObj, getDeclaredFields);
    jsize fieldCount = env->GetArrayLength(fields);
    result << "\n📋 Fields (" << fieldCount << "):\n";
    
    for (int i = 0; i < fieldCount && i < 30; i++) {
        jobject field = env->GetObjectArrayElement(fields, i);
        jclass fieldClass = env->GetObjectClass(field);
        
        jmethodID getFieldName = env->GetMethodID(fieldClass, "getName", "()Ljava/lang/String;");
        jstring fieldNameStr = (jstring)env->CallObjectMethod(field, getFieldName);
        const char* fieldNameCStr = env->GetStringUTFChars(fieldNameStr, nullptr);
        
        jmethodID getFieldType = env->GetMethodID(fieldClass, "getType", "()Ljava/lang/Class;");
        jobject fieldTypeObj = env->CallObjectMethod(field, getFieldType);
        jmethodID getTypeName = env->GetMethodID(env->FindClass("java/lang/Class"), "getName", "()Ljava/lang/String;");
        jstring fieldTypeStr = (jstring)env->CallObjectMethod(fieldTypeObj, getTypeName);
        const char* fieldTypeCStr = env->GetStringUTFChars(fieldTypeStr, nullptr);
        
        // تلاش برای خواندن مقدار فیلد
        jmethodID setAccessible = env->GetMethodID(fieldClass, "setAccessible", "(Z)V");
        env->CallVoidMethod(field, setAccessible, JNI_TRUE);
        
        std::string valueStr = "[cannot read]";
        try {
            jmethodID getValue = env->GetMethodID(fieldClass, "get", "(Ljava/lang/Object;)Ljava/lang/Object;");
            if (getValue != nullptr) {
                jobject value = env->CallObjectMethod(field, getValue, instance);
                if (value != nullptr) {
                    jclass valueClass = env->GetObjectClass(value);
                    jmethodID valueToString = env->GetMethodID(valueClass, "toString", "()Ljava/lang/String;");
                    if (valueToString != nullptr) {
                        jstring valueStrObj = (jstring)env->CallObjectMethod(value, valueToString);
                        const char* valueCStr = env->GetStringUTFChars(valueStrObj, nullptr);
                        valueStr = valueCStr;
                        env->ReleaseStringUTFChars(valueStrObj, valueCStr);
                    }
                } else {
                    valueStr = "null";
                }
            }
        } catch (...) {
            valueStr = "[access denied]";
        }
        
        result << "  " << i << ". " << fieldNameCStr << " : " << fieldTypeCStr << " = " << valueStr << "\n";
        
        env->ReleaseStringUTFChars(fieldNameStr, fieldNameCStr);
        env->ReleaseStringUTFChars(fieldTypeStr, fieldTypeCStr);
        env->DeleteLocalRef(field);
    }

    // 3. گرفتن متدها
    jmethodID getDeclaredMethods = env->GetMethodID(
        env->FindClass("java/lang/Class"),
        "getDeclaredMethods",
        "()[Ljava/lang/reflect/Method;"
    );
    jobjectArray methods = (jobjectArray)env->CallObjectMethod(classObj, getDeclaredMethods);
    jsize methodCount = env->GetArrayLength(methods);
    result << "\n🔧 Methods (" << methodCount << "):\n";
    
    for (int i = 0; i < methodCount && i < 20; i++) {
        jobject method = env->GetObjectArrayElement(methods, i);
        jclass methodClass = env->GetObjectClass(method);
        
        jmethodID getMethodName = env->GetMethodID(methodClass, "getName", "()Ljava/lang/String;");
        jstring methodNameStr = (jstring)env->CallObjectMethod(method, getMethodName);
        const char* methodNameCStr = env->GetStringUTFChars(methodNameStr, nullptr);
        
        jmethodID getReturnType = env->GetMethodID(methodClass, "getReturnType", "()Ljava/lang/Class;");
        jobject returnTypeObj = env->CallObjectMethod(method, getReturnType);
        jmethodID getTypeName = env->GetMethodID(env->FindClass("java/lang/Class"), "getName", "()Ljava/lang/String;");
        jstring returnTypeStr = (jstring)env->CallObjectMethod(returnTypeObj, getTypeName);
        const char* returnTypeCStr = env->GetStringUTFChars(returnTypeStr, nullptr);
        
        // گرفتن پارامترها
        jmethodID getParameterTypes = env->GetMethodID(methodClass, "getParameterTypes", "()[Ljava/lang/Class;");
        jobjectArray paramTypes = (jobjectArray)env->CallObjectMethod(method, getParameterTypes);
        jsize paramCount = env->GetArrayLength(paramTypes);
        
        std::string params = "(";
        for (int j = 0; j < paramCount; j++) {
            jobject paramType = env->GetObjectArrayElement(paramTypes, j);
            jmethodID getParamName = env->GetMethodID(env->FindClass("java/lang/Class"), "getName", "()Ljava/lang/String;");
            jstring paramNameStr = (jstring)env->CallObjectMethod(paramType, getParamName);
            const char* paramNameCStr = env->GetStringUTFChars(paramNameStr, nullptr);
            params += paramNameCStr;
            if (j < paramCount - 1) params += ", ";
            env->ReleaseStringUTFChars(paramNameStr, paramNameCStr);
            env->DeleteLocalRef(paramType);
        }
        params += ")";
        
        result << "  " << i << ". " << returnTypeCStr << " " << methodNameCStr << params << "\n";
        
        env->ReleaseStringUTFChars(methodNameStr, methodNameCStr);
        env->ReleaseStringUTFChars(returnTypeStr, returnTypeCStr);
        env->DeleteLocalRef(method);
        env->DeleteLocalRef(paramTypes);
    }

    // 4. اطلاعات اضافی
    result << "\n📊 Instance Info:\n";
    result << "  Address: " << instance << "\n";
    
    // تلاش برای دریافت GameObject (اگر MonoBehaviour باشد)
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
                    result << "  GameObject Name: " << nameCStr << "\n";
                    env->ReleaseStringUTFChars(nameObj, nameCStr);
                }
            }
        }
    }

    result << "========================================\n";
    return result.str();
}

// ======================== دکمه‌های منو ========================
jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    jobjectArray ret;
    const char *features[] = {
        OBFUSCATE("Category_🌐 Network Tools"),
        OBFUSCATE("InputText_Enter IP Address"),           // featNum: 0
        
        OBFUSCATE("Button_📡 Capture: MenuControl"),        // featNum: 1
        OBFUSCATE("Button_📡 Capture: GtaMenuControl"),     // featNum: 2
        OBFUSCATE("Button_📡 Capture: ServerListAccess"),   // featNum: 3
        OBFUSCATE("Button_📡 Capture: AutoRunLinuxServer"), // featNum: 4
        OBFUSCATE("Button_📡 Capture: ALL Classes"),        // featNum: 5
        
        OBFUSCATE("Button_Show Saved IP"),                 // featNum: 6
        OBFUSCATE("Button_Show Last Capture"),             // featNum: 7
        OBFUSCATE("Button_Clear Logs"),                    // featNum: 8
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

    const char* classNames[] = {
        "MenuControl",
        "GtaMenuControl",
        "ServerListAccess",
        "AutoRunLinuxServer"
    };
    const char* classLabels[] = {
        "MenuControl",
        "GtaMenuControl",
        "ServerListAccess",
        "AutoRunLinuxServer"
    };

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

        case 1: // Capture MenuControl
        case 2: // Capture GtaMenuControl
        case 3: // Capture ServerListAccess
        case 4: // Capture AutoRunLinuxServer
            {
                int idx = featNum - 1;
                const char* className = classNames[idx];
                const char* classLabel = classLabels[idx];
                
                show_toast(env, obj, "🔄 Capturing " + std::string(classLabel) + "...", 0);
                write_debug("Starting capture for " + std::string(classLabel));
                
                std::string result = capture_class_info(env, className);
                g_lastCapture = result;
                
                // ذخیره در فایل
                write_log(result);
                
                // نمایش خلاصه در Toast
                std::string summary = "✅ " + std::string(classLabel) + " captured!\n";
                summary += "Check: /sdcard/Download/capture_log.txt";
                show_toast(env, obj, summary, 1);
                
                write_debug("Capture completed for " + std::string(classLabel));
            }
            break;

        case 5: // Capture ALL Classes
            {
                show_toast(env, obj, "🔄 Capturing ALL classes...", 0);
                write_debug("Starting capture for ALL classes");
                
                std::string allResults = "========== CAPTURE ALL ==========\n";
                allResults += "Time: " + get_time() + "\n\n";
                
                for (int i = 0; i < 4; i++) {
                    allResults += capture_class_info(env, classNames[i]);
                    allResults += "\n";
                }
                
                g_lastCapture = allResults;
                write_log(allResults);
                
                show_toast(env, obj, "✅ All classes captured!\nCheck: /sdcard/Download/capture_log.txt", 1);
                write_debug("All classes captured");
            }
            break;

        case 6: // Show Saved IP
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

        case 7: // Show Last Capture
            if (!g_lastCapture.empty()) {
                // نمایش فقط خط اول در Toast
                std::string firstLine = g_lastCapture.substr(0, g_lastCapture.find('\n'));
                if (firstLine.length() > 80) firstLine = firstLine.substr(0, 80) + "...";
                show_toast(env, obj, "📄 " + firstLine, 1);
                write_debug("Last capture shown");
            } else {
                show_toast(env, obj, "No capture yet! Run a capture first.", 0);
            }
            break;

        case 8: // Clear Logs
            {
                std::ofstream f1(g_logPath);
                if (f1.is_open()) {
                    f1 << "Logs cleared at " << get_time() << "\n";
                    f1.close();
                }
                std::ofstream f2(g_debugPath);
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

    if (textStr != nullptr) {
        env->ReleaseStringUTFChars(text, textStr);
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
    
    // بارگذاری IP ذخیره شده
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
    std::ofstream f(g_logPath);
    if (f.is_open()) {
        f << "========== CAPTURE LOG ==========\n";
        f << "Started: " << get_time() << "\n";
        f << "================================\n\n";
        f.close();
    }
    std::ofstream f2(g_debugPath);
    if (f2.is_open()) {
        f2 << "========== DEBUG LOG ==========\n";
        f2 << "Started: " << get_time() << "\n";
        f2 << "==============================\n\n";
        f2.close();
    }
    std::thread(hack_thread).detach();
}