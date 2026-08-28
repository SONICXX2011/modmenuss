#include <string>
#include <jni.h>
#include <unistd.h>
#include <fstream>
#include <thread>
#include <chrono>
#include <ctime>
#include <cstring>
#include <signal.h>
#include <sys/stat.h>
#include "Includes/Logger.h"
#include "Includes/obfuscate.h"
#include "Includes/Utils.hpp"
#include "Menu/Menu.hpp"
#include "Menu/Jni.hpp"
#include "Includes/Macros.h"
#include "dobby.h"

#define targetLibName OBFUSCATE("libil2cpp.so")

// ======================== آفست‌ها ========================
#define OFFSET_GET_INSTANCE     0xF570C4
#define OFFSET_GTA_START        0xF571A4
#define OFFSET_MENU_BUTTONS     0x30
#define OFFSET_INTERACTABLE     0xD8

// ======================== مسیرها ========================
static std::string g_basePath = "/storage/emulated/0/Download/lac/";
static std::string g_debugLog = g_basePath + "mod_debug.txt";
static std::string g_ipResultLog = g_basePath + "ip_result.txt";
static std::string g_crashLog = g_basePath + "crash_log.txt";
static std::string g_reportLog = g_basePath + "full_report.txt";

// ======================== متغیرها ========================
static bool g_crashHandlerInstalled = false;
static bool g_buttonsDisabled = false;
static void* g_gtaInstance = nullptr;

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

static void write_report(const std::string& msg) {
    write_log(g_reportLog, msg);
}

static void write_result(const std::string& msg) {
    write_log(g_ipResultLog, msg);
}

static void create_directory() {
    mkdir(g_basePath.c_str(), 0777);
}

// ======================== کرش‌گیر ========================
static void crash_handler(int sig, siginfo_t *info, void *context) {
    std::ofstream f(g_crashLog, std::ios::app);
    if (!f.is_open()) return;
    f << "\n========================================\n";
    f << "💥 CRASH at " << get_time() << "\n";
    f << "Signal: " << sig << " (" << strsignal(sig) << ")\n";
    f << "Fault address: " << info->si_addr << "\n";
    #if defined(__aarch64__)
    ucontext_t *uc = (ucontext_t *)context;
    struct sigcontext *sc = &uc->uc_mcontext;
    f << "  pc: 0x" << std::hex << sc->pc << "\n";
    f << "  lr: 0x" << std::hex << sc->regs[30] << "\n";
    f << "  sp: 0x" << std::hex << sc->sp << "\n";
    #endif
    f << "========================================\n\n";
    f.close();
    signal(sig, SIG_DFL);
    raise(sig);
}

static void install_crash_handler() {
    if (g_crashHandlerInstalled) return;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGFPE, &sa, nullptr);
    sigaction(SIGILL, &sa, nullptr);
    sigaction(SIGBUS, &sa, nullptr);
    g_crashHandlerInstalled = true;
    write_report("✅ Crash handler installed");
}

// ======================== روش 1: get_Instance ========================
static void* get_instance_method1() {
    typedef void* (*get_instance_t)();
    get_instance_t get_Instance = (get_instance_t)getAbsoluteAddress("libil2cpp.so", "GtaMenuControl.get_Instance");
    if (get_Instance == nullptr) {
        write_report("   ❌ get_Instance not found!");
        return nullptr;
    }
    void* instance = get_Instance();
    if (instance != nullptr) {
        write_report("   ✅ get_Instance: 0x" + std::to_string((uintptr_t)instance));
    }
    return instance;
}

// ======================== روش 2: هوک روی Start ========================
void (*orig_GtaMenuStart)(void *instance);
void hook_GtaMenuStart(void *instance) {
    if (instance != nullptr) {
        g_gtaInstance = instance;
        write_report("   ✅ Hook Start: 0x" + std::to_string((uintptr_t)instance));
    }
    if (orig_GtaMenuStart) {
        orig_GtaMenuStart(instance);
    }
}

static void* get_instance_method2() {
    for (int i = 0; i < 10 && g_gtaInstance == nullptr; i++) {
        sleep(1);
    }
    return g_gtaInstance;
}

// ======================== روش 3: FindObjectOfType (JNI) ========================
static void* get_instance_method3(JNIEnv* env) {
    if (env == nullptr) return nullptr;
    jclass objClass = env->FindClass("UnityEngine.Object");
    if (objClass == nullptr) return nullptr;
    jmethodID findObj = env->GetStaticMethodID(objClass, "FindObjectOfType", "(Ljava/lang/Class;)Ljava/lang/Object;");
    if (findObj == nullptr) return nullptr;
    jclass gtaClass = env->FindClass("GtaMenuControl");
    if (gtaClass == nullptr) return nullptr;
    jobject instance = env->CallStaticObjectMethod(objClass, findObj, gtaClass);
    if (instance != nullptr) {
        write_report("   ✅ FindObjectOfType: 0x" + std::to_string((uintptr_t)instance));
        return instance;
    }
    return nullptr;
}

// ======================== روش 4: از طریق Name (Find) ========================
static void* get_instance_method4(JNIEnv* env) {
    if (env == nullptr) return nullptr;
    jclass gameObjClass = env->FindClass("UnityEngine.GameObject");
    if (gameObjClass == nullptr) return nullptr;
    jmethodID findMethod = env->GetStaticMethodID(gameObjClass, "Find", "(Ljava/lang/String;)LUnityEngine/GameObject;");
    if (findMethod == nullptr) return nullptr;
    jstring name = env->NewStringUTF("GtaMenuControl");
    jobject go = env->CallStaticObjectMethod(gameObjClass, findMethod, name);
    env->DeleteLocalRef(name);
    if (go != nullptr) {
        jmethodID getComp = env->GetMethodID(gameObjClass, "GetComponent", "(Ljava/lang/Class;)LUnityEngine/Component;");
        if (getComp != nullptr) {
            jclass gtaClass = env->FindClass("GtaMenuControl");
            if (gtaClass != nullptr) {
                jobject instance = env->CallObjectMethod(go, getComp, gtaClass);
                if (instance != nullptr) {
                    write_report("   ✅ GameObject.Find: 0x" + std::to_string((uintptr_t)instance));
                    return instance;
                }
            }
        }
    }
    return nullptr;
}

// ======================== روش 5: از طریق Resources ========================
static void* get_instance_method5(JNIEnv* env) {
    if (env == nullptr) return nullptr;
    jclass resourcesClass = env->FindClass("UnityEngine.Resources");
    if (resourcesClass == nullptr) return nullptr;
    jmethodID findObjOfTypeAll = env->GetStaticMethodID(resourcesClass, "FindObjectsOfTypeAll", "(Ljava/lang/Class;)[LUnityEngine/Object;");
    if (findObjOfTypeAll == nullptr) return nullptr;
    jclass gtaClass = env->FindClass("GtaMenuControl");
    if (gtaClass == nullptr) return nullptr;
    jobjectArray arr = (jobjectArray)env->CallStaticObjectMethod(resourcesClass, findObjOfTypeAll, gtaClass);
    if (arr != nullptr && env->GetArrayLength(arr) > 0) {
        jobject instance = env->GetObjectArrayElement(arr, 0);
        if (instance != nullptr) {
            write_report("   ✅ Resources.FindObjectsOfTypeAll: 0x" + std::to_string((uintptr_t)instance));
            return instance;
        }
    }
    return nullptr;
}

// ======================== گرفتن instance با همه روش‌ها ========================
static void* get_gta_instance(JNIEnv* env) {
    write_report("🔍 Getting GtaMenuControl instance...");
    
    void* instance = nullptr;
    
    // روش 1: get_Instance
    write_report("   Method 1: get_Instance()");
    instance = get_instance_method1();
    if (instance != nullptr) return instance;
    
    // روش 2: هوک
    write_report("   Method 2: Hook Start()");
    instance = get_instance_method2();
    if (instance != nullptr) return instance;
    
    // روش 3: FindObjectOfType
    write_report("   Method 3: FindObjectOfType");
    instance = get_instance_method3(env);
    if (instance != nullptr) return instance;
    
    // روش 4: GameObject.Find
    write_report("   Method 4: GameObject.Find");
    instance = get_instance_method4(env);
    if (instance != nullptr) return instance;
    
    // روش 5: Resources
    write_report("   Method 5: Resources.FindObjectsOfTypeAll");
    instance = get_instance_method5(env);
    if (instance != nullptr) return instance;
    
    write_report("❌ All methods failed!");
    return nullptr;
}

// ======================== دیسیبل کردن menuButtons ========================
static void disable_menu_buttons(JNIEnv* env) {
    if (g_buttonsDisabled) {
        write_report("⚠️ Already disabled!");
        return;
    }
    
    write_report("\n========== DISABLE MENU BUTTONS ==========");
    write_report("Time: " + get_time());
    
    for (int attempt = 0; attempt < 5; attempt++) {
        if (attempt > 0) {
            write_report("🔄 Retry " + std::to_string(attempt) + "...");
            sleep(1);
        }
        
        void* instance = get_gta_instance(env);
        if (instance == nullptr) {
            write_report("   ⚠️ Instance null, retrying...");
            continue;
        }
        
        g_gtaInstance = instance;
        write_report("✅ Instance: 0x" + std::to_string((uintptr_t)instance));
        
        // گرفتن آرایه menuButtons
        void** menuButtons = *(void***)((uintptr_t)instance + OFFSET_MENU_BUTTONS);
        if (menuButtons == nullptr) {
            write_report("   ⚠️ menuButtons null, retrying...");
            continue;
        }
        
        // شمارش دکمه‌ها
        int count = 0;
        for (int i = 0; i < 20; i++) {
            if (menuButtons[i] != nullptr) count++;
        }
        write_report("✅ Found " + std::to_string(count) + " buttons");
        
        // اگه کمتر از 5 دکمه بود، دوباره تلاش کن
        if (count < 5) {
            write_report("   ⚠️ Only " + std::to_string(count) + " buttons, retrying...");
            continue;
        }
        
        // دیسیبل کردن (به جز LAN=0,1 و SETTINGS=4)
        int disabled = 0;
        for (int i = 0; i < 20; i++) {
            void* btn = menuButtons[i];
            if (btn == nullptr) continue;
            
            // LAN و SETTINGS فعال بمونن
            if (i == 0 || i == 1 || i == 4) {
                write_report("   🔵 Keeping: index " + std::to_string(i));
                continue;
            }
            
            bool* interactable = (bool*)((uintptr_t)btn + OFFSET_INTERACTABLE);
            if (interactable != nullptr) {
                *interactable = false;
                disabled++;
                write_report("   ❌ Disabled: index " + std::to_string(i));
            }
        }
        
        g_buttonsDisabled = true;
        write_report("✅ Disabled " + std::to_string(disabled) + " buttons!");
        write_result("✅ Disabled " + std::to_string(disabled) + " buttons!");
        write_report("========== COMPLETE ==========\n");
        return;
    }
    
    write_report("❌ Failed after 5 attempts!");
}

// ======================== منو ========================
jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    jobjectArray ret;
    const char *features[] = {
        OBFUSCATE("Category_🔧 Tools"),
        OBFUSCATE("Button_Disable Menu Buttons"),
        OBFUSCATE("Button_Enable Menu Buttons"),
        OBFUSCATE("RichTextView_📁 /sdcard/Download/lac/"),
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
        case 0:
            write_report("🔘 Disable button pressed");
            g_buttonsDisabled = false;
            disable_menu_buttons(env);
            break;
        case 1:
            write_report("🔘 Enable button pressed");
            if (g_gtaInstance != nullptr) {
                void** menuButtons = *(void***)((uintptr_t)g_gtaInstance + OFFSET_MENU_BUTTONS);
                if (menuButtons != nullptr) {
                    for (int i = 0; i < 20; i++) {
                        if (menuButtons[i] == nullptr) continue;
                        bool* interactable = (bool*)((uintptr_t)menuButtons[i] + OFFSET_INTERACTABLE);
                        if (interactable != nullptr) *interactable = true;
                    }
                    g_buttonsDisabled = false;
                    write_report("✅ All buttons enabled!");
                }
            }
            break;
        default:
            break;
    }
}

// ======================== ترد اصلی ========================
void hack_thread() {
    while (!isLibraryLoaded(targetLibName)) sleep(1);
    write_report("✅ libil2cpp.so loaded");
    
#if defined(__aarch64__)
    // هوک Start
    void* startAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0xF571A4"));
    if (startAddr != nullptr) {
        DobbyHook(startAddr, (dobby_dummy_func_t)hook_GtaMenuStart, (dobby_dummy_func_t*)&orig_GtaMenuStart);
        write_report("✅ Hook installed");
    }
#endif
    
    write_report("✅ hack_thread done");
}

// ======================== تابع ورودی ========================
__attribute__((constructor))
void lib_main() {
    create_directory();
    install_crash_handler();
    
    std::ofstream f(g_debugLog);
    if (f.is_open()) {
        f << "========== MOD LOADED ==========\n";
        f << "Time: " << get_time() << "\n";
        f << "===============================\n\n";
        f.close();
    }
    
    std::ofstream report(g_reportLog);
    if (report.is_open()) {
        report << "========== FULL REPORT ==========\n";
        report << "Started at: " << get_time() << "\n";
        report << "==================================\n\n";
        report.close();
    }
    
    write_report("🚀 lib_main called");
    std::thread(hack_thread).detach();
}