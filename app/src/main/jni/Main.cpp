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
#include <vector>
#include <sstream>
#include "Includes/Logger.h"
#include "Includes/obfuscate.h"
#include "Includes/Utils.hpp"
#include "Menu/Menu.hpp"
#include "Menu/Jni.hpp"
#include "Includes/Macros.h"
#include "dobby.h"
#include "KittyMemory/KittyMemory.hpp"

#define targetLibName OBFUSCATE("libil2cpp.so")

// ======================== آفست‌ها ========================
#define OFFSET_GTA_GET_INSTANCE         0xF570C4
#define OFFSET_GTA_START                0xF571A4
#define OFFSET_OBJECT_GET_NAME          0x1E7CE6C
#define OFFSET_IL2CPP_STRING_NEW        0xE40BF0
#define OFFSET_GAMEOBJECT_NAME          0x20

// ====== روش‌های مختلف هوک ======
#define OFFSET_ONPOINTERCLICK           0x1F08A60   // روش 1: OnPointerClick
#define OFFSET_PRESS                    0x1F04A60   // روش 2: Press
#define OFFSET_UNITYACTION_INVOKE       0x1E8482C   // روش 3: UnityAction.Invoke
#define OFFSET_UNITYEVENT_INVOKE        0x1E84848   // روش 4: UnityEvent.Invoke

#define MAX_WAIT 30

static std::string g_basePath = "/storage/emulated/0/Download/lac/";
static std::string g_clickLog = g_basePath + "click_log.txt";
static std::string g_crashLog = g_basePath + "crash_log.txt";
static std::string g_debugLog = g_basePath + "mod_debug.txt";

static bool g_crashHandlerInstalled = false;
static bool g_gameReady = false;
static void* g_gtaInstance = nullptr;
static JNIEnv* g_env = nullptr;

// ====== وضعیت هر روش ======
static bool g_method1_on = false;  // OnPointerClick
static bool g_method2_on = false;  // Press
static bool g_method3_on = false;  // UnityAction.Invoke
static bool g_method4_on = false;  // UnityEvent.Invoke

// ====== پوینترهای توابع اصلی ======
void (*orig_OnPointerClick)(void* button, void* eventData);
void (*orig_Press)(void* button);
void (*orig_UnityAction_Invoke)(void* action);
void (*orig_UnityEvent_Invoke)(void* unityEvent);

// ====== متغیرهای کنترل هوک ======
static bool g_hook1_installed = false;
static bool g_hook2_installed = false;
static bool g_hook3_installed = false;
static bool g_hook4_installed = false;

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

// ======================== Toast ========================
static void show_toast(const std::string& msg) {
    if (!g_env) {
        write_debug("❌ JNIEnv not available for Toast");
        return;
    }
    
    JNIEnv* env = g_env;
    jclass toastClass = env->FindClass("android/widget/Toast");
    if (!toastClass) {
        env->ExceptionClear();
        return;
    }
    
    jmethodID makeTextMethod = env->GetStaticMethodID(toastClass, "makeText", "(Landroid/content/Context;Ljava/lang/CharSequence;I)Landroid/widget/Toast;");
    if (!makeTextMethod) {
        env->ExceptionClear();
        return;
    }
    
    jclass activityThreadClass = env->FindClass("android/app/ActivityThread");
    if (!activityThreadClass) {
        env->ExceptionClear();
        return;
    }
    
    jmethodID currentActivityThreadMethod = env->GetStaticMethodID(activityThreadClass, "currentActivityThread", "()Landroid/app/ActivityThread;");
    if (!currentActivityThreadMethod) {
        env->ExceptionClear();
        return;
    }
    
    jobject activityThread = env->CallStaticObjectMethod(activityThreadClass, currentActivityThreadMethod);
    if (!activityThread) {
        env->ExceptionClear();
        return;
    }
    
    jmethodID getApplicationMethod = env->GetMethodID(activityThreadClass, "getApplication", "()Landroid/app/Application;");
    if (!getApplicationMethod) {
        env->ExceptionClear();
        return;
    }
    
    jobject context = env->CallObjectMethod(activityThread, getApplicationMethod);
    if (!context) {
        env->ExceptionClear();
        return;
    }
    
    jstring jMsg = env->NewStringUTF(msg.c_str());
    if (!jMsg) {
        env->ExceptionClear();
        return;
    }
    
    jobject toast = env->CallStaticObjectMethod(toastClass, makeTextMethod, context, jMsg, 0);
    env->DeleteLocalRef(jMsg);
    
    if (!toast) {
        env->ExceptionClear();
        return;
    }
    
    jmethodID showMethod = env->GetMethodID(toastClass, "show", "()V");
    if (!showMethod) {
        env->ExceptionClear();
        return;
    }
    
    env->CallVoidMethod(toast, showMethod);
    env->DeleteLocalRef(toast);
    env->DeleteLocalRef(context);
    env->DeleteLocalRef(activityThread);
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
    write_debug("✅ Crash handler installed");
}

// ======================== امنیت حافظه ========================
static bool is_valid_address(void* addr) {
    if (!addr) return false;
    uintptr_t ptr = (uintptr_t)addr;
    if (ptr < 0x1000) return false;
    if (ptr > 0x7FFFFFFFFFFFFF) return false;
    auto map = KittyMemory::getAddressMap(addr);
    return map.readable;
}

// ======================== توابع IL2CPP ========================
static void* create_mono_string(const char* str) {
    if (!str) return nullptr;
    typedef void* (*il2cpp_string_new_t)(const char*);
    il2cpp_string_new_t il2cpp_string_new = 
        (il2cpp_string_new_t)getAbsoluteAddress("libil2cpp.so", OBFUSCATE("0xE40BF0"));
    if (!il2cpp_string_new) return nullptr;
    return il2cpp_string_new(str);
}

static std::string mono_to_string_safe(void* monoStr) {
    if (!monoStr || !is_valid_address(monoStr)) return "";
    typedef int32_t (*len_t)(void*);
    typedef uint16_t* (*chars_t)(void*);
    len_t get_len = (len_t)getAbsoluteAddress("libil2cpp.so", "il2cpp_string_length");
    chars_t get_chars = (chars_t)getAbsoluteAddress("libil2cpp.so", "il2cpp_string_chars");
    if (!get_len || !get_chars) return "";
    int len = get_len(monoStr);
    if (len <= 0 || len > 65536) return "";
    uint16_t* chars = get_chars(monoStr);
    if (!chars || !is_valid_address(chars)) return "";
    std::string result;
    result.reserve(len);
    for (int i = 0; i < len; i++) {
        result += (char)(chars[i] & 0xFF);
    }
    return result;
}

static void* get_gta_instance_safe() {
    if (g_gtaInstance && is_valid_address(g_gtaInstance)) {
        return g_gtaInstance;
    }
    typedef void* (*get_instance_t)();
    get_instance_t get_Instance = (get_instance_t)getAbsoluteAddress("libil2cpp.so", OBFUSCATE("0xF570C4"));
    if (!get_Instance) return nullptr;
    void* instance = get_Instance();
    if (instance && is_valid_address(instance)) {
        g_gtaInstance = instance;
        return instance;
    }
    return nullptr;
}

static std::string get_button_name_safe(void* btn) {
    if (!btn || !is_valid_address(btn)) return "";
    void* namePtr = *(void**)((uintptr_t)btn + OFFSET_GAMEOBJECT_NAME);
    if (namePtr && is_valid_address(namePtr)) {
        std::string name = mono_to_string_safe(namePtr);
        if (!name.empty()) return name;
    }
    typedef void* (*get_name_t)(void*);
    get_name_t get_name = (get_name_t)getAbsoluteAddress("libil2cpp.so", OBFUSCATE("0x1E7CE6C"));
    if (get_name) {
        void* monoStr = get_name(btn);
        if (monoStr && is_valid_address(monoStr)) {
            return mono_to_string_safe(monoStr);
        }
    }
    return "";
}

// ======================== هوک‌ها ========================

// ----- روش 1: OnPointerClick -----
void hook_OnPointerClick(void* button, void* eventData) {
    if (g_method1_on && g_gameReady && button && is_valid_address(button)) {
        std::string name = get_button_name_safe(button);
        if (!name.empty()) {
            show_toast("🔵 " + name);
            write_log(g_clickLog, "[Method1-OnPointerClick] " + name + " | 0x" + std::to_string((uintptr_t)button));
        }
    }
    if (orig_OnPointerClick) orig_OnPointerClick(button, eventData);
}

// ----- روش 2: Press -----
void hook_Press(void* button) {
    if (g_method2_on && g_gameReady && button && is_valid_address(button)) {
        std::string name = get_button_name_safe(button);
        if (!name.empty()) {
            show_toast("🟢 " + name);
            write_log(g_clickLog, "[Method2-Press] " + name + " | 0x" + std::to_string((uintptr_t)button));
        }
    }
    if (orig_Press) orig_Press(button);
}

// ----- روش 3: UnityAction.Invoke -----
void hook_UnityAction_Invoke(void* action) {
    if (g_method3_on && g_gameReady && action && is_valid_address(action)) {
        write_log(g_clickLog, "[Method3-UnityAction] Action: 0x" + std::to_string((uintptr_t)action));
        show_toast("🟡 UnityAction");
    }
    if (orig_UnityAction_Invoke) orig_UnityAction_Invoke(action);
}

// ----- روش 4: UnityEvent.Invoke -----
void hook_UnityEvent_Invoke(void* unityEvent) {
    if (g_method4_on && g_gameReady && unityEvent && is_valid_address(unityEvent)) {
        write_log(g_clickLog, "[Method4-UnityEvent] Event: 0x" + std::to_string((uintptr_t)unityEvent));
        show_toast("🟠 UnityEvent");
    }
    if (orig_UnityEvent_Invoke) orig_UnityEvent_Invoke(unityEvent);
}

// ======================== هوک GtaMenuControl.Start ========================
void (*orig_GtaMenuStart)(void *instance);
void hook_GtaMenuStart(void *instance) {
    if (instance && is_valid_address(instance)) {
        g_gtaInstance = instance;
        if (!g_gameReady) {
            g_gameReady = true;
            write_debug("✅ Game ready!");
        }
    }
    if (orig_GtaMenuStart) orig_GtaMenuStart(instance);
}

// ======================== نصب/برداشتن هوک‌ها ========================
static void install_all_hooks() {
#if defined(__aarch64__)
    // روش 1: OnPointerClick
    if (!g_hook1_installed) {
        void* addr = getAbsoluteAddress(targetLibName, OBFUSCATE("0x1F08A60"));
        if (addr && is_valid_address(addr)) {
            if (DobbyHook(addr, (dobby_dummy_func_t)hook_OnPointerClick, (dobby_dummy_func_t*)&orig_OnPointerClick) == 0) {
                g_hook1_installed = true;
                write_debug("✅ Method1 (OnPointerClick) hooked");
            }
        }
    }
    
    // روش 2: Press
    if (!g_hook2_installed) {
        void* addr = getAbsoluteAddress(targetLibName, OBFUSCATE("0x1F04A60"));
        if (addr && is_valid_address(addr)) {
            if (DobbyHook(addr, (dobby_dummy_func_t)hook_Press, (dobby_dummy_func_t*)&orig_Press) == 0) {
                g_hook2_installed = true;
                write_debug("✅ Method2 (Press) hooked");
            }
        }
    }
    
    // روش 3: UnityAction.Invoke (با احتیاط)
    if (!g_hook3_installed) {
        void* addr = getAbsoluteAddress(targetLibName, OBFUSCATE("0x1E8482C"));
        if (addr && is_valid_address(addr)) {
            if (DobbyHook(addr, (dobby_dummy_func_t)hook_UnityAction_Invoke, (dobby_dummy_func_t*)&orig_UnityAction_Invoke) == 0) {
                g_hook3_installed = true;
                write_debug("✅ Method3 (UnityAction.Invoke) hooked");
            }
        }
    }
    
    // روش 4: UnityEvent.Invoke
    if (!g_hook4_installed) {
        void* addr = getAbsoluteAddress(targetLibName, OBFUSCATE("0x1E84848"));
        if (addr && is_valid_address(addr)) {
            if (DobbyHook(addr, (dobby_dummy_func_t)hook_UnityEvent_Invoke, (dobby_dummy_func_t*)&orig_UnityEvent_Invoke) == 0) {
                g_hook4_installed = true;
                write_debug("✅ Method4 (UnityEvent.Invoke) hooked");
            }
        }
    }
#endif
    
    // هوک GtaMenuControl.Start
    void* startAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0xF571A4"));
    if (startAddr && is_valid_address(startAddr)) {
        DobbyHook(startAddr, (dobby_dummy_func_t)hook_GtaMenuStart, (dobby_dummy_func_t*)&orig_GtaMenuStart);
    }
}

// ======================== تابع تست دکمه‌ها (با JNI) ========================
static void TestFindButtons() {
    if (!g_env) {
        write_log(g_clickLog, "❌ JNIEnv not available");
        return;
    }
    
    JNIEnv* env = g_env;
    jclass buttonClass = env->FindClass("UnityEngine.UI.Button");
    if (!buttonClass) {
        env->ExceptionClear();
        write_log(g_clickLog, "❌ Button class not found");
        return;
    }
    
    jclass resourcesClass = env->FindClass("UnityEngine.Resources");
    if (!resourcesClass) {
        env->ExceptionClear();
        write_log(g_clickLog, "❌ Resources class not found");
        return;
    }
    
    jmethodID findObjectsMethod = env->GetStaticMethodID(resourcesClass, "FindObjectsOfTypeAll", "(Ljava/lang/Class;)[Ljava/lang/Object;");
    if (!findObjectsMethod) {
        env->ExceptionClear();
        write_log(g_clickLog, "❌ FindObjectsOfTypeAll not found");
        return;
    }
    
    jobjectArray buttons = (jobjectArray)env->CallStaticObjectMethod(resourcesClass, findObjectsMethod, buttonClass);
    if (!buttons) {
        env->ExceptionClear();
        write_log(g_clickLog, "❌ No buttons found");
        return;
    }
    
    jsize count = env->GetArrayLength(buttons);
    write_log(g_clickLog, "📊 Total buttons: " + std::to_string(count));
    
    for (int i = 0; i < count && i < 50; i++) {
        jobject btn = env->GetObjectArrayElement(buttons, i);
        if (!btn) continue;
        
        jclass btnClass = env->GetObjectClass(btn);
        jmethodID getName = env->GetMethodID(btnClass, "get_name", "()Ljava/lang/String;");
        if (!getName) {
            env->ExceptionClear();
            env->DeleteLocalRef(btn);
            continue;
        }
        
        jstring nameStr = (jstring)env->CallObjectMethod(btn, getName);
        if (!nameStr) {
            env->ExceptionClear();
            env->DeleteLocalRef(btn);
            continue;
        }
        
        const char* nameC = env->GetStringUTFChars(nameStr, nullptr);
        if (nameC) {
            write_log(g_clickLog, "  Button[" + std::to_string(i) + "] = " + std::string(nameC));
            env->ReleaseStringUTFChars(nameStr, nameC);
        }
        env->DeleteLocalRef(nameStr);
        env->DeleteLocalRef(btn);
    }
    
    env->DeleteLocalRef(buttons);
    write_log(g_clickLog, "✅ Test complete");
}

// ======================== منو ========================
jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    g_env = env;
    
    const char *features[] = {
        "Category_🎯 Capture Methods",
        "Toggle_Method1: OnPointerClick",
        "Toggle_Method2: Press",
        "Toggle_Method3: UnityAction.Invoke",
        "Toggle_Method4: UnityEvent.Invoke",
        "Button_🔍 Test Find All Buttons",
        "RichTextView_📁 Logs: /sdcard/Download/lac/",
        "RichTextView_📌 click_log.txt | mod_debug.txt",
    };
    int total = sizeof(features) / sizeof(features[0]);
    jobjectArray ret = (jobjectArray)env->NewObjectArray(
        total, env->FindClass("java/lang/String"), env->NewStringUTF("")
    );
    for (int i = 0; i < total; i++) {
        env->SetObjectArrayElement(ret, i, env->NewStringUTF(features[i]));
    }
    return ret;
}

void Changes(JNIEnv *env, jclass clazz, jobject obj, jint featNum,
             jstring featName, jint value, jlong Lvalue, jboolean boolean, jstring text) {
    
    g_env = env;
    std::string status = boolean ? "ON ✅" : "OFF ❌";
    
    switch (featNum) {
        case 0:
            g_method1_on = boolean;
            write_log(g_clickLog, "Method1 (OnPointerClick): " + status);
            show_toast("Method1 " + status);
            break;
        case 1:
            g_method2_on = boolean;
            write_log(g_clickLog, "Method2 (Press): " + status);
            show_toast("Method2 " + status);
            break;
        case 2:
            g_method3_on = boolean;
            write_log(g_clickLog, "Method3 (UnityAction.Invoke): " + status);
            show_toast("Method3 " + status);
            break;
        case 3:
            g_method4_on = boolean;
            write_log(g_clickLog, "Method4 (UnityEvent.Invoke): " + status);
            show_toast("Method4 " + status);
            break;
        case 4:
            write_log(g_clickLog, "🔍 Test Find All Buttons");
            TestFindButtons();
            break;
    }
}

// ======================== ترد اصلی ========================
void hack_thread() {
    write_debug("⏳ Waiting for game to load...");
    
    int waitCount = 0;
    while (!isLibraryLoaded(targetLibName) && waitCount < MAX_WAIT) {
        sleep(1);
        waitCount++;
    }
    
    if (waitCount >= MAX_WAIT) {
        write_debug("❌ Timeout!");
        return;
    }
    write_debug("✅ libil2cpp.so loaded");
    
    waitCount = 0;
    while (waitCount < MAX_WAIT) {
        void* instance = get_gta_instance_safe();
        if (instance && is_valid_address(instance)) {
            g_gtaInstance = instance;
            g_gameReady = true;
            write_debug("✅ GtaMenuControl instance ready!");
            break;
        }
        sleep(1);
        waitCount++;
    }
    
    install_all_hooks();
    write_debug("✅ Mod ready!");
}

__attribute__((constructor))
void lib_main() {
    mkdir(g_basePath.c_str(), 0777);
    install_crash_handler();
    write_debug("🚀 Mod loaded");
    std::thread(hack_thread).detach();
}