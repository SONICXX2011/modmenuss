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

// ======================== آفست‌های تأیید شده ========================
#define OFFSET_GTA_GET_INSTANCE         0xF570C4
#define OFFSET_GTA_START                0xF571A4
#define OFFSET_MENU_BUTTONS             0x30          // GtaMenuControl.menuButtons
#define OFFSET_BUTTON_ONCLICK           0x100         // Button.m_OnClick
#define OFFSET_BUTTON_INTERACTABLE      0xD8          // Selectable.m_Interactable
#define OFFSET_OBJECT_GET_NAME          0x1E7CE6C     // UnityEngine.Object.get_name()
#define OFFSET_UNITYACTION_INVOKE       0x1E8482C     // UnityAction.Invoke()
#define OFFSET_IL2CPP_STRING_NEW        0xE40BF0

#define MAX_BUTTONS 30

// ======================== مسیرها ========================
static std::string g_basePath = "/storage/emulated/0/Download/lac/";
static std::string g_buttonsLog = g_basePath + "buttons_list.txt";
static std::string g_clickLog = g_basePath + "click_log.txt";
static std::string g_crashLog = g_basePath + "crash_log.txt";

static bool g_crashHandlerInstalled = false;
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
}

// ======================== توابع IL2CPP ========================
static void* create_mono_string(const char* str) {
    typedef void* (*il2cpp_string_new_t)(const char*);
    il2cpp_string_new_t il2cpp_string_new = 
        (il2cpp_string_new_t)getAbsoluteAddress("libil2cpp.so", OBFUSCATE("0xE40BF0"));
    if (!il2cpp_string_new) return nullptr;
    return il2cpp_string_new(str);
}

static std::string mono_to_string(void* monoStr) {
    if (!monoStr) return "";
    typedef int32_t (*len_t)(void*);
    typedef uint16_t* (*chars_t)(void*);
    len_t get_len = (len_t)getAbsoluteAddress("libil2cpp.so", "il2cpp_string_length");
    chars_t get_chars = (chars_t)getAbsoluteAddress("libil2cpp.so", "il2cpp_string_chars");
    if (!get_len || !get_chars) return "";
    int len = get_len(monoStr);
    if (len <= 0 || len > 65536) return "";
    uint16_t* chars = get_chars(monoStr);
    if (!chars) return "";
    std::string result;
    result.reserve(len);
    for (int i = 0; i < len; i++) result += (char)(chars[i] & 0xFF);
    return result;
}

static void* get_gta_instance() {
    if (g_gtaInstance) return g_gtaInstance;
    typedef void* (*get_instance_t)();
    get_instance_t get_Instance = (get_instance_t)getAbsoluteAddress("libil2cpp.so", OBFUSCATE("0xF570C4"));
    if (!get_Instance) return nullptr;
    g_gtaInstance = get_Instance();
    return g_gtaInstance;
}

static std::string get_button_name(void* btn) {
    if (!btn) return "(null)";
    typedef void* (*get_name_t)(void*);
    get_name_t get_name = (get_name_t)getAbsoluteAddress("libil2cpp.so", OBFUSCATE("0x1E7CE6C"));
    if (!get_name) return "(error)";
    void* monoStr = get_name(btn);
    return mono_to_string(monoStr);
}

// ======================== نمایش همه دکمه‌ها ========================
static void ShowAllButtons() {
    write_log(g_buttonsLog, "\n========== ALL BUTTONS ==========");
    write_log(g_buttonsLog, "Time: " + get_time());
    write_log(g_buttonsLog, "Searching in GtaMenuControl.menuButtons...");
    
    try {
        void* instance = get_gta_instance();
        if (!instance) {
            write_log(g_buttonsLog, "❌ GtaMenuControl instance not found!");
            return;
        }
        write_log(g_buttonsLog, "✅ Instance: 0x" + std::to_string((uintptr_t)instance));
        
        void** menuButtons = *(void***)((uintptr_t)instance + OFFSET_MENU_BUTTONS);
        if (!menuButtons) {
            write_log(g_buttonsLog, "❌ menuButtons is null!");
            return;
        }
        write_log(g_buttonsLog, "✅ menuButtons array: 0x" + std::to_string((uintptr_t)menuButtons));
        
        int found = 0;
        for (int i = 0; i < MAX_BUTTONS; i++) {
            void* btn = menuButtons[i];
            if (!btn) continue;
            
            // گرفتن اسم دکمه
            std::string name = get_button_name(btn);
            if (name.empty() || name == "(null)" || name == "(error)") continue;
            
            // گرفتن وضعیت interactable
            bool* interactable = (bool*)((uintptr_t)btn + OFFSET_BUTTON_INTERACTABLE);
            bool isInteractable = (interactable && *interactable);
            
            // گرفتن onClick
            void* onClick = *(void**)((uintptr_t)btn + OFFSET_BUTTON_ONCLICK);
            bool hasOnClick = (onClick != nullptr);
            
            write_log(g_buttonsLog, "  ─────────────────────────────");
            write_log(g_buttonsLog, "  📌 Button[" + std::to_string(i) + "]");
            write_log(g_buttonsLog, "     Name: " + name);
            write_log(g_buttonsLog, "     Address: 0x" + std::to_string((uintptr_t)btn));
            write_log(g_buttonsLog, "     Interactable: " + std::string(isInteractable ? "YES" : "NO"));
            write_log(g_buttonsLog, "     Has onClick: " + std::string(hasOnClick ? "YES" : "NO"));
            found++;
        }
        
        write_log(g_buttonsLog, "\n✅ Total buttons found: " + std::to_string(found));
        
    } catch (...) {
        write_log(g_buttonsLog, "❌ Exception occurred!");
    }
    
    write_log(g_buttonsLog, "========== DONE ==========\n");
}

// ======================== هوک UnityAction.Invoke ========================
void (*orig_UnityAction_Invoke)(void* action);
void hook_UnityAction_Invoke(void* action) {
    write_log(g_clickLog, "⚡ UnityAction.Invoke() called!");
    write_log(g_clickLog, "   Action address: 0x" + std::to_string((uintptr_t)action));
    write_log(g_clickLog, "   Time: " + get_time());
    
    if (orig_UnityAction_Invoke) {
        orig_UnityAction_Invoke(action);
    }
}

// ======================== هوک GtaMenuControl.Start ========================
void (*orig_GtaMenuStart)(void *instance);
void hook_GtaMenuStart(void *instance) {
    if (instance) {
        g_gtaInstance = instance;
        write_log(g_buttonsLog, "✅ GtaMenuControl instance captured via hook");
    }
    if (orig_GtaMenuStart) {
        orig_GtaMenuStart(instance);
    }
}

// ======================== منو ========================
jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    const char *features[] = {
        "Category_🔍 Button Tools",
        "Button_📋 Show All Buttons",
        "Button_🔄 Click CONNECT (via Invoke)",
        "Button_📊 Show Click Log",
        "RichTextView_📁 Logs: /sdcard/Download/lac/",
        "RichTextView_📌 buttons_list.txt | click_log.txt",
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

// ======================== تغییرات منو ========================
void Changes(JNIEnv *env, jclass clazz, jobject obj, jint featNum,
             jstring featName, jint value, jlong Lvalue, jboolean boolean, jstring text) {
    
    switch (featNum) {
        case 0: {
            write_log(g_buttonsLog, "🔘 Show All Buttons pressed");
            ShowAllButtons();
            break;
        }
        case 1: {
            write_log(g_buttonsLog, "🔘 Click CONNECT pressed");
            // پیدا کردن دکمه CONNECT از menuButtons
            void* instance = get_gta_instance();
            if (!instance) {
                write_log(g_buttonsLog, "❌ No instance!");
                return;
            }
            void** menuButtons = *(void***)((uintptr_t)instance + OFFSET_MENU_BUTTONS);
            if (!menuButtons) {
                write_log(g_buttonsLog, "❌ menuButtons is null!");
                return;
            }
            
            // حلقه برای پیدا کردن CONNECT
            for (int i = 0; i < MAX_BUTTONS; i++) {
                void* btn = menuButtons[i];
                if (!btn) continue;
                std::string name = get_button_name(btn);
                if (name == "CONNECT" || name == "Connect") {
                    write_log(g_buttonsLog, "✅ Found CONNECT at index " + std::to_string(i));
                    
                    // فعال کردن دکمه
                    bool* interactable = (bool*)((uintptr_t)btn + OFFSET_BUTTON_INTERACTABLE);
                    if (interactable) *interactable = true;
                    
                    // گرفتن onClick
                    void* onClick = *(void**)((uintptr_t)btn + OFFSET_BUTTON_ONCLICK);
                    if (!onClick) {
                        write_log(g_buttonsLog, "❌ onClick is null!");
                        return;
                    }
                    
                    // صدا زدن Invoke
                    typedef void (*invoke_t)(void*);
                    invoke_t Invoke = (invoke_t)getAbsoluteAddress("libil2cpp.so", OBFUSCATE("0x1E8482C"));
                    if (!Invoke) {
                        write_log(g_buttonsLog, "❌ Invoke not found!");
                        return;
                    }
                    
                    write_log(g_buttonsLog, "🔄 Calling Invoke() on CONNECT...");
                    Invoke(onClick);
                    write_log(g_buttonsLog, "✅ Invoke() called!");
                    return;
                }
            }
            write_log(g_buttonsLog, "❌ CONNECT button not found!");
            break;
        }
        case 2: {
            write_log(g_buttonsLog, "📊 Show Click Log");
            // نمایش مسیر لاگ
            write_log(g_buttonsLog, "📁 Click log: " + g_clickLog);
            break;
        }
    }
}

// ======================== ترد اصلی ========================
void hack_thread() {
    while (!isLibraryLoaded(targetLibName)) sleep(1);
    
#if defined(__aarch64__)
    // هوک UnityAction.Invoke
    void* invokeAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0x1E8482C"));
    if (invokeAddr) {
        DobbyHook(invokeAddr, (dobby_dummy_func_t)hook_UnityAction_Invoke, (dobby_dummy_func_t*)&orig_UnityAction_Invoke);
        write_log(g_buttonsLog, "✅ UnityAction.Invoke() hooked");
    }
    
    // هوک GtaMenuControl.Start
    void* startAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0xF571A4"));
    if (startAddr) {
        DobbyHook(startAddr, (dobby_dummy_func_t)hook_GtaMenuStart, (dobby_dummy_func_t*)&orig_GtaMenuStart);
        write_log(g_buttonsLog, "✅ GtaMenuControl.Start() hooked");
    }
#endif
    
    get_gta_instance();
}

// ======================== تابع ورودی ========================
__attribute__((constructor))
void lib_main() {
    mkdir(g_basePath.c_str(), 0777);
    install_crash_handler();
    write_log(g_buttonsLog, "🚀 Mod loaded");
    std::thread(hack_thread).detach();
}