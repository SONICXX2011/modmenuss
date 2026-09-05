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

// ======================== آفست‌های جدید از Dump5 ========================
#define OFFSET_GTA_GET_INSTANCE         0xF570C4
#define OFFSET_GTA_START                0xF571A4
#define OFFSET_OBJECT_GET_NAME          0x1E7CE6C
#define OFFSET_IL2CPP_STRING_NEW        0xE40BF0
#define OFFSET_GAMEOBJECT_NAME          0x20

// ====== آفست‌های هوک ======
#define OFFSET_ONPOINTERCLICK           0x1F14A4C   // OnPointerClick
#define OFFSET_EXECUTE_POINTER_CLICK    0x2098B60   // Execute<IPointerClickHandler>
#define OFFSET_EXECUTE_ISELECT          0x20994B0   // Execute<ISelectHandler>
#define OFFSET_UNITYEVENT_INVOKE        0x1E94354   // UnityEvent.Invoke

#define MAX_WAIT 30

// ======================== مسیرهای لاگ ========================
static std::string g_basePath = "/storage/emulated/0/Download/lac/";
static std::string g_clickLog = g_basePath + "click_log.txt";
static std::string g_crashLog = g_basePath + "crash_log.txt";
static std::string g_debugLog = g_basePath + "mod_debug.txt";

// ======================== متغیرهای سراسری ========================
static bool g_crashHandlerInstalled = false;
static bool g_gameReady = false;
static bool g_hooksInstalled = false;
static void* g_gtaInstance = nullptr;
static JNIEnv* g_env = nullptr;

// ====== وضعیت روش‌ها ======
static bool g_method_onpointer = false;
static bool g_method_execute_click = false;
static bool g_method_execute_select = false;
static bool g_method_unityevent = false;

// ====== شمارنده کلیک ======
static int g_clickCounter = 0;

// ====== پوینترهای توابع اصلی ======
void (*orig_OnPointerClick)(void* button, void* eventData);
void (*orig_ExecutePointerClick)(void* handler, void* eventData);
void (*orig_ExecuteISelect)(void* handler, void* eventData);
void (*orig_UnityEvent_Invoke)(void* unityEvent);

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
        write_debug("⚠️ JNIEnv not available for Toast");
        return;
    }
    
    JNIEnv* env = g_env;
    
    jclass toastClass = env->FindClass("android/widget/Toast");
    if (!toastClass) {
        env->ExceptionClear();
        write_debug("❌ Toast class not found");
        return;
    }
    
    jmethodID makeTextMethod = env->GetStaticMethodID(toastClass, "makeText", "(Landroid/content/Context;Ljava/lang/CharSequence;I)Landroid/widget/Toast;");
    if (!makeTextMethod) {
        env->ExceptionClear();
        write_debug("❌ Toast.makeText not found");
        return;
    }
    
    jclass activityThreadClass = env->FindClass("android/app/ActivityThread");
    if (!activityThreadClass) {
        env->ExceptionClear();
        write_debug("❌ ActivityThread class not found");
        return;
    }
    
    jmethodID currentActivityThreadMethod = env->GetStaticMethodID(activityThreadClass, "currentActivityThread", "()Landroid/app/ActivityThread;");
    if (!currentActivityThreadMethod) {
        env->ExceptionClear();
        write_debug("❌ currentActivityThread not found");
        return;
    }
    
    jobject activityThread = env->CallStaticObjectMethod(activityThreadClass, currentActivityThreadMethod);
    if (!activityThread) {
        env->ExceptionClear();
        write_debug("❌ ActivityThread is null");
        return;
    }
    
    jmethodID getApplicationMethod = env->GetMethodID(activityThreadClass, "getApplication", "()Landroid/app/Application;");
    if (!getApplicationMethod) {
        env->ExceptionClear();
        write_debug("❌ getApplication not found");
        return;
    }
    
    jobject context = env->CallObjectMethod(activityThread, getApplicationMethod);
    if (!context) {
        env->ExceptionClear();
        write_debug("❌ Application context is null");
        return;
    }
    
    jstring jMsg = env->NewStringUTF(msg.c_str());
    if (!jMsg) {
        env->ExceptionClear();
        write_debug("❌ Failed to create jstring");
        return;
    }
    
    jobject toast = env->CallStaticObjectMethod(toastClass, makeTextMethod, context, jMsg, 0);
    env->DeleteLocalRef(jMsg);
    
    if (!toast) {
        env->ExceptionClear();
        write_debug("❌ Toast.makeText returned null");
        return;
    }
    
    jmethodID showMethod = env->GetMethodID(toastClass, "show", "()V");
    if (!showMethod) {
        env->ExceptionClear();
        write_debug("❌ Toast.show not found");
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

static std::string get_button_name_safe(void* obj) {
    if (!obj || !is_valid_address(obj)) return "";
    
    // روش 1: از GameObject.m_Name (آفست 0x20)
    void* namePtr = *(void**)((uintptr_t)obj + OFFSET_GAMEOBJECT_NAME);
    if (namePtr && is_valid_address(namePtr)) {
        std::string name = mono_to_string_safe(namePtr);
        if (!name.empty()) return name;
    }
    
    // روش 2: از UnityEngine.Object.get_name (آفست 0x1E7CE6C)
    typedef void* (*get_name_t)(void*);
    get_name_t get_name = (get_name_t)getAbsoluteAddress("libil2cpp.so", OBFUSCATE("0x1E7CE6C"));
    if (get_name) {
        void* monoStr = get_name(obj);
        if (monoStr && is_valid_address(monoStr)) {
            return mono_to_string_safe(monoStr);
        }
    }
    
    return "";
}

// ======================== هوک‌ها ========================

// ----- روش 1: OnPointerClick -----
void hook_OnPointerClick(void* button, void* eventData) {
    if (g_method_onpointer && g_gameReady && button && is_valid_address(button)) {
        g_clickCounter++;
        std::string name = get_button_name_safe(button);
        if (!name.empty()) {
            show_toast("🔵 [" + std::to_string(g_clickCounter) + "] " + name);
            write_log(g_clickLog, "[OnPointerClick] #" + std::to_string(g_clickCounter) + " | " + name);
            write_log(g_clickLog, "   Button: 0x" + std::to_string((uintptr_t)button));
        } else {
            show_toast("🔵 [" + std::to_string(g_clickCounter) + "] Unknown");
            write_log(g_clickLog, "[OnPointerClick] #" + std::to_string(g_clickCounter) + " | Unknown");
            write_log(g_clickLog, "   Button: 0x" + std::to_string((uintptr_t)button));
        }
    }
    if (orig_OnPointerClick) orig_OnPointerClick(button, eventData);
}

// ----- روش 2: Execute<IPointerClickHandler> -----
void hook_ExecutePointerClick(void* handler, void* eventData) {
    if (g_method_execute_click && g_gameReady && handler && is_valid_address(handler)) {
        g_clickCounter++;
        std::string name = get_button_name_safe(handler);
        if (!name.empty()) {
            show_toast("🟢 [" + std::to_string(g_clickCounter) + "] " + name);
            write_log(g_clickLog, "[Execute-Click] #" + std::to_string(g_clickCounter) + " | " + name);
            write_log(g_clickLog, "   Handler: 0x" + std::to_string((uintptr_t)handler));
        } else {
            show_toast("🟢 [" + std::to_string(g_clickCounter) + "] Unknown");
            write_log(g_clickLog, "[Execute-Click] #" + std::to_string(g_clickCounter) + " | Unknown");
            write_log(g_clickLog, "   Handler: 0x" + std::to_string((uintptr_t)handler));
        }
    }
    if (orig_ExecutePointerClick) orig_ExecutePointerClick(handler, eventData);
}

// ----- روش 3: Execute<ISelectHandler> -----
void hook_ExecuteISelect(void* handler, void* eventData) {
    if (g_method_execute_select && g_gameReady && handler && is_valid_address(handler)) {
        g_clickCounter++;
        std::string name = get_button_name_safe(handler);
        if (!name.empty()) {
            show_toast("🟣 [" + std::to_string(g_clickCounter) + "] " + name);
            write_log(g_clickLog, "[Execute-Select] #" + std::to_string(g_clickCounter) + " | " + name);
            write_log(g_clickLog, "   Handler: 0x" + std::to_string((uintptr_t)handler));
        } else {
            show_toast("🟣 [" + std::to_string(g_clickCounter) + "] Unknown");
            write_log(g_clickLog, "[Execute-Select] #" + std::to_string(g_clickCounter) + " | Unknown");
            write_log(g_clickLog, "   Handler: 0x" + std::to_string((uintptr_t)handler));
        }
    }
    if (orig_ExecuteISelect) orig_ExecuteISelect(handler, eventData);
}

// ----- روش 4: UnityEvent.Invoke -----
void hook_UnityEvent_Invoke(void* unityEvent) {
    if (g_method_unityevent && g_gameReady && unityEvent && is_valid_address(unityEvent)) {
        g_clickCounter++;
        write_log(g_clickLog, "[UnityEvent.Invoke] #" + std::to_string(g_clickCounter) + " | Event: 0x" + std::to_string((uintptr_t)unityEvent));
        show_toast("🟠 [" + std::to_string(g_clickCounter) + "] Event");
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

// ======================== نصب هوک‌ها ========================
static void install_hooks_with_delay() {
    write_debug("⏳ Waiting for game to load...");
    
    int waitCount = 0;
    while (!isLibraryLoaded(targetLibName) && waitCount < MAX_WAIT) {
        sleep(1);
        waitCount++;
    }
    
    if (waitCount >= MAX_WAIT) {
        write_debug("❌ Timeout! libil2cpp.so not loaded!");
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
    
    if (!g_gameReady) {
        write_debug("⚠️ No instance yet, will use hook");
    }
    
#if defined(__aarch64__)
    // روش 1: OnPointerClick (0x1F14A4C)
    void* addr1 = getAbsoluteAddress(targetLibName, OBFUSCATE("0x1F14A4C"));
    if (addr1 && is_valid_address(addr1)) {
        if (DobbyHook(addr1, (dobby_dummy_func_t)hook_OnPointerClick, (dobby_dummy_func_t*)&orig_OnPointerClick) == 0) {
            write_debug("✅ OnPointerClick hooked (0x1F14A4C)");
        }
    }
    
    // روش 2: Execute<IPointerClickHandler> (0x2098B60)
    void* addr2 = getAbsoluteAddress(targetLibName, OBFUSCATE("0x2098B60"));
    if (addr2 && is_valid_address(addr2)) {
        if (DobbyHook(addr2, (dobby_dummy_func_t)hook_ExecutePointerClick, (dobby_dummy_func_t*)&orig_ExecutePointerClick) == 0) {
            write_debug("✅ Execute<IPointerClickHandler> hooked (0x2098B60)");
        }
    }
    
    // روش 3: Execute<ISelectHandler> (0x20994B0)
    void* addr3 = getAbsoluteAddress(targetLibName, OBFUSCATE("0x20994B0"));
    if (addr3 && is_valid_address(addr3)) {
        if (DobbyHook(addr3, (dobby_dummy_func_t)hook_ExecuteISelect, (dobby_dummy_func_t*)&orig_ExecuteISelect) == 0) {
            write_debug("✅ Execute<ISelectHandler> hooked (0x20994B0)");
        }
    }
    
    // روش 4: UnityEvent.Invoke (0x1E94354)
    void* addr4 = getAbsoluteAddress(targetLibName, OBFUSCATE("0x1E94354"));
    if (addr4 && is_valid_address(addr4)) {
        if (DobbyHook(addr4, (dobby_dummy_func_t)hook_UnityEvent_Invoke, (dobby_dummy_func_t*)&orig_UnityEvent_Invoke) == 0) {
            write_debug("✅ UnityEvent.Invoke hooked (0x1E94354)");
        }
    }
    
    // هوک GtaMenuControl.Start
    void* startAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0xF571A4"));
    if (startAddr && is_valid_address(startAddr)) {
        DobbyHook(startAddr, (dobby_dummy_func_t)hook_GtaMenuStart, (dobby_dummy_func_t*)&orig_GtaMenuStart);
    }
#endif
    
    g_hooksInstalled = true;
    write_debug("✅ Mod ready!");
}

// ======================== منو ========================
jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    g_env = env;
    
    const char *features[] = {
        "Category_🎯 Capture Methods",
        "Toggle_Method1: OnPointerClick",
        "Toggle_Method2: Execute<IPointerClick>",
        "Toggle_Method3: Execute<ISelect>",
        "Toggle_Method4: UnityEvent.Invoke",
        "Button_📊 Reset Counter",
        "Button_📁 Show Logs Path",
        "RichTextView_📁 /sdcard/Download/lac/",
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
    
    switch (featNum) {
        case 0:
            g_method_onpointer = boolean;
            write_log(g_clickLog, "Method1 (OnPointerClick): " + std::string(boolean ? "ON ✅" : "OFF ❌"));
            if (boolean) { g_clickCounter = 0; show_toast("🔵 Method1 ON"); }
            else { show_toast("🔵 Method1 OFF"); }
            break;
        case 1:
            g_method_execute_click = boolean;
            write_log(g_clickLog, "Method2 (Execute<IPointerClick>): " + std::string(boolean ? "ON ✅" : "OFF ❌"));
            if (boolean) { g_clickCounter = 0; show_toast("🟢 Method2 ON"); }
            else { show_toast("🟢 Method2 OFF"); }
            break;
        case 2:
            g_method_execute_select = boolean;
            write_log(g_clickLog, "Method3 (Execute<ISelect>): " + std::string(boolean ? "ON ✅" : "OFF ❌"));
            if (boolean) { g_clickCounter = 0; show_toast("🟣 Method3 ON"); }
            else { show_toast("🟣 Method3 OFF"); }
            break;
        case 3:
            g_method_unityevent = boolean;
            write_log(g_clickLog, "Method4 (UnityEvent.Invoke): " + std::string(boolean ? "ON ✅" : "OFF ❌"));
            if (boolean) { g_clickCounter = 0; show_toast("🟠 Method4 ON"); }
            else { show_toast("🟠 Method4 OFF"); }
            break;
        case 4:
            g_clickCounter = 0;
            write_log(g_clickLog, "📊 Counter reset to 0");
            show_toast("🔄 Counter reset: 0");
            break;
        case 5:
            show_toast("📁 /sdcard/Download/lac/");
            write_log(g_clickLog, "📁 Logs path: " + g_basePath);
            break;
    }
}

// ======================== ترد اصلی ========================
void hack_thread() {
    install_hooks_with_delay();
    write_debug("✅ hack_thread finished");
}

// ======================== تابع ورودی ========================
__attribute__((constructor))
void lib_main() {
    mkdir(g_basePath.c_str(), 0777);
    install_crash_handler();
    write_debug("🚀 Mod loaded");
    std::thread(hack_thread).detach();
}