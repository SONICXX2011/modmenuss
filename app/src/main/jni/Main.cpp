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
#define OFFSET_MENU_BUTTONS             0x30
#define OFFSET_BUTTON_ONCLICK           0x100
#define OFFSET_BUTTON_INTERACTABLE      0xD8
#define OFFSET_OBJECT_GET_NAME          0x1E7CE6C
#define OFFSET_UNITYACTION_INVOKE       0x1E8482C
#define OFFSET_IL2CPP_STRING_NEW        0xE40BF0

#define MAX_BUTTONS 30
#define MAX_WAIT 30

// ======================== مسیرها ========================
static std::string g_basePath = "/storage/emulated/0/Download/lac/";
static std::string g_buttonsLog = g_basePath + "buttons_list.txt";
static std::string g_clickLog = g_basePath + "click_log.txt";
static std::string g_crashLog = g_basePath + "crash_log.txt";
static std::string g_debugLog = g_basePath + "mod_debug.txt";

static bool g_crashHandlerInstalled = false;
static bool g_gameReady = false;
static void* g_gtaInstance = nullptr;
static bool g_hooksInstalled = false;

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

// ======================== توابع IL2CPP ========================
static void* create_mono_string(const char* str) {
    if (!str) return nullptr;
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

static bool is_valid_address(void* addr) {
    if (!addr) return false;
    uintptr_t ptr = (uintptr_t)addr;
    if (ptr < 0x1000) return false;
    if (ptr > 0x7FFFFFFFFFFFFF) return false;
    auto map = KittyMemory::getAddressMap(addr);
    return map.readable;
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
    if (!btn || !is_valid_address(btn)) return "(invalid)";
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
    
    // چک کن بازی آماده هست یا نه
    if (!g_gameReady || !g_gtaInstance || !is_valid_address(g_gtaInstance)) {
        write_log(g_buttonsLog, "⚠️ Game not ready yet! Wait for initialization.");
        write_log(g_buttonsLog, "========== DONE ==========\n");
        return;
    }
    
    try {
        void* instance = g_gtaInstance;
        write_log(g_buttonsLog, "✅ Instance: 0x" + std::to_string((uintptr_t)instance));
        
        void** menuButtons = *(void***)((uintptr_t)instance + OFFSET_MENU_BUTTONS);
        if (!menuButtons || !is_valid_address(menuButtons)) {
            write_log(g_buttonsLog, "❌ menuButtons is null or invalid!");
            write_log(g_buttonsLog, "========== DONE ==========\n");
            return;
        }
        write_log(g_buttonsLog, "✅ menuButtons array: 0x" + std::to_string((uintptr_t)menuButtons));
        
        int found = 0;
        for (int i = 0; i < MAX_BUTTONS; i++) {
            void* btn = menuButtons[i];
            if (!btn || !is_valid_address(btn)) continue;
            
            std::string name = get_button_name_safe(btn);
            if (name.empty() || name == "(invalid)" || name == "(error)") continue;
            
            bool* interactable = (bool*)((uintptr_t)btn + OFFSET_BUTTON_INTERACTABLE);
            bool isInteractable = (interactable && is_valid_address(interactable) && *interactable);
            
            void* onClick = *(void**)((uintptr_t)btn + OFFSET_BUTTON_ONCLICK);
            bool hasOnClick = (onClick && is_valid_address(onClick));
            
            write_log(g_buttonsLog, "  ─────────────────────────────");
            write_log(g_buttonsLog, "  📌 Button[" + std::to_string(i) + "]");
            write_log(g_buttonsLog, "     Name: " + name);
            write_log(g_buttonsLog, "     Address: 0x" + std::to_string((uintptr_t)btn));
            write_log(g_buttonsLog, "     Interactable: " + std::string(isInteractable ? "YES" : "NO"));
            write_log(g_buttonsLog, "     Has onClick: " + std::string(hasOnClick ? "YES" : "NO"));
            found++;
        }
        
        write_log(g_buttonsLog, "\n✅ Total buttons found: " + std::to_string(found));
        
    } catch (const std::exception& e) {
        write_log(g_buttonsLog, "❌ Exception: " + std::string(e.what()));
    } catch (...) {
        write_log(g_buttonsLog, "❌ Unknown exception!");
    }
    
    write_log(g_buttonsLog, "========== DONE ==========\n");
}

// ======================== کلیک روی دکمه CONNECT ========================
static void ClickConnectButton() {
    write_log(g_clickLog, "\n========== CLICK CONNECT ==========");
    write_log(g_clickLog, "Time: " + get_time());
    
    if (!g_gameReady || !g_gtaInstance || !is_valid_address(g_gtaInstance)) {
        write_log(g_clickLog, "⚠️ Game not ready yet! Wait for initialization.");
        write_log(g_clickLog, "========== DONE ==========\n");
        return;
    }
    
    try {
        void* instance = g_gtaInstance;
        void** menuButtons = *(void***)((uintptr_t)instance + OFFSET_MENU_BUTTONS);
        if (!menuButtons || !is_valid_address(menuButtons)) {
            write_log(g_clickLog, "❌ menuButtons is null!");
            write_log(g_clickLog, "========== DONE ==========\n");
            return;
        }
        
        // پیدا کردن دکمه CONNECT
        for (int i = 0; i < MAX_BUTTONS; i++) {
            void* btn = menuButtons[i];
            if (!btn || !is_valid_address(btn)) continue;
            
            std::string name = get_button_name_safe(btn);
            if (name != "CONNECT" && name != "Connect") continue;
            
            write_log(g_clickLog, "✅ Found CONNECT at index " + std::to_string(i));
            write_log(g_clickLog, "   Address: 0x" + std::to_string((uintptr_t)btn));
            
            // فعال کردن دکمه
            bool* interactable = (bool*)((uintptr_t)btn + OFFSET_BUTTON_INTERACTABLE);
            if (interactable && is_valid_address(interactable)) {
                *interactable = true;
                write_log(g_clickLog, "✅ Button enabled");
            }
            
            // گرفتن onClick
            void* onClick = *(void**)((uintptr_t)btn + OFFSET_BUTTON_ONCLICK);
            if (!onClick || !is_valid_address(onClick)) {
                write_log(g_clickLog, "❌ onClick is null or invalid!");
                write_log(g_clickLog, "========== DONE ==========\n");
                return;
            }
            write_log(g_clickLog, "✅ onClick: 0x" + std::to_string((uintptr_t)onClick));
            
            // صدا زدن Invoke
            typedef void (*invoke_t)(void*);
            invoke_t Invoke = (invoke_t)getAbsoluteAddress("libil2cpp.so", OBFUSCATE("0x1E8482C"));
            if (!Invoke) {
                write_log(g_clickLog, "❌ Invoke not found!");
                write_log(g_clickLog, "========== DONE ==========\n");
                return;
            }
            
            write_log(g_clickLog, "🔄 Calling Invoke() on CONNECT...");
            Invoke(onClick);
            write_log(g_clickLog, "✅ Invoke() called successfully!");
            write_log(g_clickLog, "========== DONE ==========\n");
            return;
        }
        
        write_log(g_clickLog, "❌ CONNECT button not found!");
        
    } catch (const std::exception& e) {
        write_log(g_clickLog, "❌ Exception: " + std::string(e.what()));
    } catch (...) {
        write_log(g_clickLog, "❌ Unknown exception!");
    }
    
    write_log(g_clickLog, "========== DONE ==========\n");
}

// ======================== هوک UnityAction.Invoke ========================
void (*orig_UnityAction_Invoke)(void* action);
void hook_UnityAction_Invoke(void* action) {
    if (!g_gameReady) {
        write_log(g_clickLog, "⚡ UnityAction called but game not ready!");
        return;
    }
    write_log(g_clickLog, "⚡ UnityAction.Invoke() called!");
    write_log(g_clickLog, "   Action: 0x" + std::to_string((uintptr_t)action));
    
    if (orig_UnityAction_Invoke) {
        orig_UnityAction_Invoke(action);
    }
}

// ======================== هوک GtaMenuControl.Start ========================
void (*orig_GtaMenuStart)(void *instance);
void hook_GtaMenuStart(void *instance) {
    if (instance && is_valid_address(instance)) {
        g_gtaInstance = instance;
        if (!g_gameReady) {
            g_gameReady = true;
            write_log(g_buttonsLog, "✅ Game ready! (GtaMenuControl.Start hooked)");
            write_debug("✅ Game ready!");
        }
    }
    if (orig_GtaMenuStart) {
        orig_GtaMenuStart(instance);
    }
}

// ======================== نصب هوک‌ها با تأخیر ========================
static void install_hooks_with_delay() {
    write_log(g_buttonsLog, "⏳ Waiting for game to load...");
    write_debug("⏳ Waiting for game to load...");
    
    // مرحله 1: صبر کن تا libil2cpp.so لود بشه
    int waitCount = 0;
    while (!isLibraryLoaded(targetLibName) && waitCount < MAX_WAIT) {
        sleep(1);
        waitCount++;
    }
    
    if (waitCount >= MAX_WAIT) {
        write_log(g_buttonsLog, "❌ Timeout! libil2cpp.so not loaded!");
        write_debug("❌ Timeout!");
        return;
    }
    write_log(g_buttonsLog, "✅ libil2cpp.so loaded");
    write_debug("✅ libil2cpp.so loaded");
    
    // مرحله 2: صبر کن تا GtaMenuControl instance ساخته بشه
    waitCount = 0;
    while (waitCount < MAX_WAIT) {
        void* instance = get_gta_instance_safe();
        if (instance && is_valid_address(instance)) {
            g_gtaInstance = instance;
            g_gameReady = true;
            write_log(g_buttonsLog, "✅ GtaMenuControl instance ready!");
            write_debug("✅ GtaMenuControl instance ready!");
            break;
        }
        sleep(1);
        waitCount++;
    }
    
    if (!g_gameReady) {
        write_log(g_buttonsLog, "⚠️ GtaMenuControl not ready, will try hook on Start");
    }
    
    // مرحله 3: نصب هوک‌ها
#if defined(__aarch64__)
    // هوک UnityAction.Invoke
    void* invokeAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0x1E8482C"));
    if (invokeAddr && is_valid_address(invokeAddr)) {
        int res = DobbyHook(invokeAddr, (dobby_dummy_func_t)hook_UnityAction_Invoke, (dobby_dummy_func_t*)&orig_UnityAction_Invoke);
        if (res == 0) {
            write_log(g_buttonsLog, "✅ UnityAction.Invoke() hooked");
            write_debug("✅ UnityAction.Invoke() hooked");
        } else {
            write_log(g_buttonsLog, "❌ UnityAction.Invoke() hook failed: " + std::to_string(res));
            write_debug("❌ UnityAction.Invoke() hook failed");
        }
    } else {
        write_log(g_buttonsLog, "❌ UnityAction.Invoke() address not found");
        write_debug("❌ UnityAction.Invoke() address not found");
    }
    
    // هوک GtaMenuControl.Start
    void* startAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0xF571A4"));
    if (startAddr && is_valid_address(startAddr)) {
        int res = DobbyHook(startAddr, (dobby_dummy_func_t)hook_GtaMenuStart, (dobby_dummy_func_t*)&orig_GtaMenuStart);
        if (res == 0) {
            write_log(g_buttonsLog, "✅ GtaMenuControl.Start() hooked");
            write_debug("✅ GtaMenuControl.Start() hooked");
        } else {
            write_log(g_buttonsLog, "❌ GtaMenuControl.Start() hook failed: " + std::to_string(res));
            write_debug("❌ GtaMenuControl.Start() hook failed");
        }
    } else {
        write_log(g_buttonsLog, "❌ GtaMenuControl.Start() address not found");
        write_debug("❌ GtaMenuControl.Start() address not found");
    }
#endif
    
    // مرحله 4: اگه هنوز instance نداریم، یه بار دیگه امتحان کن
    if (!g_gameReady) {
        write_log(g_buttonsLog, "⏳ Waiting additional 5 seconds for instance...");
        for (int i = 0; i < 5; i++) {
            void* instance = get_gta_instance_safe();
            if (instance && is_valid_address(instance)) {
                g_gtaInstance = instance;
                g_gameReady = true;
                write_log(g_buttonsLog, "✅ GtaMenuControl instance ready!");
                write_debug("✅ GtaMenuControl instance ready!");
                break;
            }
            sleep(1);
        }
    }
    
    if (g_gameReady) {
        write_log(g_buttonsLog, "✅ Mod ready! All hooks installed.");
        write_debug("✅ Mod ready!");
    } else {
        write_log(g_buttonsLog, "⚠️ Mod loaded but game not ready. Hooks installed for when game starts.");
        write_debug("⚠️ Mod loaded but game not ready");
    }
    
    g_hooksInstalled = true;
}

// ======================== منو ========================
jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    const char *features[] = {
        "Category_🔍 Button Tools",
        "Button_📋 Show All Buttons",
        "Button_🔄 Click CONNECT (via Invoke)",
        "Button_📊 Show Status",
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
        case 0:
            write_log(g_buttonsLog, "🔘 Show All Buttons pressed");
            ShowAllButtons();
            break;
        case 1:
            write_log(g_buttonsLog, "🔘 Click CONNECT pressed");
            ClickConnectButton();
            break;
        case 2:
            write_log(g_buttonsLog, "📊 Status");
            write_log(g_buttonsLog, "   Game ready: " + std::string(g_gameReady ? "YES" : "NO"));
            write_log(g_buttonsLog, "   Instance: 0x" + std::to_string((uintptr_t)g_gtaInstance));
            write_log(g_buttonsLog, "   Hooks installed: " + std::string(g_hooksInstalled ? "YES" : "NO"));
            break;
    }
}

// ======================== ترد اصلی ========================
void hack_thread() {
    // صبر کن تا بازی کامل لود بشه
    install_hooks_with_delay();
    
    // اگر هنوز بازی آماده نیست، هوک‌ها نصب شدن و وقتی Start صدا زده شد، کار میکنن
    write_log(g_buttonsLog, "✅ hack_thread finished");
}

// ======================== تابع ورودی ========================
__attribute__((constructor))
void lib_main() {
    // ایجاد پوشه و نصب کرش‌گیر
    mkdir(g_basePath.c_str(), 0777);
    install_crash_handler();
    
    write_log(g_buttonsLog, "🚀 Mod loaded");
    write_log(g_buttonsLog, "⏳ Starting initialization...");
    write_debug("🚀 lib_main called");
    
    // اجرای ترد اصلی
    std::thread(hack_thread).detach();
}