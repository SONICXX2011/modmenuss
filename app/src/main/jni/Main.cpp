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
#include <cstdlib>
#include "Includes/Logger.h"
#include "Includes/obfuscate.h"
#include "Includes/Utils.hpp"
#include "Menu/Menu.hpp"
#include "Menu/Jni.hpp"
#include "Includes/Macros.h"
#include "dobby.h"
#include "KittyMemory/KittyMemory.hpp"

#define targetLibName OBFUSCATE("libil2cpp.so")
#define antiCheatLibName OBFUSCATE("libunity.so")

// ======================== آفست‌های آنتی‌چیت ========================
#define OFFSET_PTRACE_FUNC              0x10c3efc
#define OFFSET_STATUS_STRING            0x001733e2
#define OFFSET_TRACERPID_STRING         0x001affcd
#define OFFSET_RO_DEBUGGABLE            0x001a6be4
#define OFFSET_MAPS_STRING              0x00217f59
#define OFFSET_ROOTED_1                 0x00168ea6
#define OFFSET_ROOTED_2                 0x0019cbf8
#define OFFSET_CHECKSUM                 0x0180da63
#define OFFSET_IL2CPP_DEBUG             0x001fbfe7
#define OFFSET_WAIT_DEBUG               0x00192c78
#define OFFSET_PORT_DEBUG               0x001fbc4f

// ======================== آفست‌های بازی ========================
#define OFFSET_GTA_GET_INSTANCE         0xF570C4
#define OFFSET_GTA_START                0xF571A4
#define OFFSET_OBJECT_GET_NAME          0x1E7CE6C
#define OFFSET_IL2CPP_STRING_NEW        0xE40BF0
#define OFFSET_GAMEOBJECT_NAME          0x20
#define OFFSET_BUTTON_PRESS             0x1F14A14

#define MAX_WAIT 30

// ======================== مسیرهای لاگ ========================
static std::string g_basePath = "/storage/emulated/0/Download/lac/";
static std::string g_clickLog = g_basePath + "click_log.txt";
static std::string g_antiCheatLog = g_basePath + "anti_cheat_log.txt";
static std::string g_crashLog = g_basePath + "crash_log.txt";
static std::string g_debugLog = g_basePath + "mod_debug.txt";

static bool g_crashHandlerInstalled = false;
static bool g_gameReady = false;
static bool g_hooksInstalled = false;
static void* g_gtaInstance = nullptr;
static JNIEnv* g_env = nullptr;
static bool g_captureEnabled = false;
static int g_clickCounter = 0;

void (*orig_ButtonPress)(void* button);
void (*orig_GtaMenuStart)(void *instance);

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

// ====== Toast خالی (هیچ کاری نمیکنه) ======
static void show_toast(const std::string& msg) {
    // Toast حذف شده - فقط لاگ گرفته میشه
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

static bool is_valid_address(void* addr) {
    if (!addr) return false;
    uintptr_t ptr = (uintptr_t)addr;
    if (ptr < 0x1000) return false;
    if (ptr > 0x7FFFFFFFFFFFFF) return false;
    auto map = KittyMemory::getAddressMap(addr);
    return map.readable;
}

static bool safe_mem_write(uintptr_t addr, const void* buffer, size_t len) {
    if (!is_valid_address((void*)addr)) return false;
    return KittyMemory::memWrite((void*)addr, buffer, len);
}

static void* get_abs_addr(const char* lib, uintptr_t offset) {
    return getAbsoluteAddress(lib, std::to_string(offset).c_str());
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
    if (g_gtaInstance && is_valid_address(g_gtaInstance)) return g_gtaInstance;
    typedef void* (*get_instance_t)();
    get_instance_t get_Instance = (get_instance_t)getAbsoluteAddress("libil2cpp.so", OBFUSCATE("0xF570C4"));
    if (!get_Instance) return nullptr;
    g_gtaInstance = get_Instance();
    return g_gtaInstance;
}

static std::string get_button_name_safe(void* obj) {
    if (!obj || !is_valid_address(obj)) return "";
    void* namePtr = *(void**)((uintptr_t)obj + OFFSET_GAMEOBJECT_NAME);
    if (namePtr && is_valid_address(namePtr)) {
        std::string name = mono_to_string_safe(namePtr);
        if (!name.empty()) return name;
    }
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

// ======================== آنتی‌چیت خودکار ========================
static void PatchAntiCheat() {
    write_log(g_antiCheatLog, "\n========== AUTO-PATCHING ANTI-CHEAT ==========");
    write_log(g_antiCheatLog, "Time: " + get_time());
    write_debug("🛡️ Auto-patching Anti-Cheat...");
    
    int success = 0;
    int total = 11;
    
    {
        void* addr = get_abs_addr(antiCheatLibName, OFFSET_PTRACE_FUNC);
        uint32_t patch[] = {0xd2800000, 0xc0035fd6};
        if (addr && safe_mem_write((uintptr_t)addr, patch, 8)) {
            write_log(g_antiCheatLog, "✅ ptrace function patched");
            success++;
        } else {
            write_log(g_antiCheatLog, "❌ ptrace function patch failed");
        }
    }
    
    struct PatchInfo {
        uintptr_t offset;
        size_t len;
        const char* name;
    } patches[] = {
        {OFFSET_STATUS_STRING, 24, "/proc/self/status"},
        {OFFSET_TRACERPID_STRING, 12, "TracerPid:"},
        {OFFSET_RO_DEBUGGABLE, 16, "ro.debuggable"},
        {OFFSET_MAPS_STRING, 24, "/proc/self/maps"},
        {OFFSET_ROOTED_1, 28, "rooted_or_jailbroken"},
        {OFFSET_ROOTED_2, 24, "rooted_jailbroken"},
        {OFFSET_CHECKSUM, 64, "has-checksums"},
        {OFFSET_IL2CPP_DEBUG, 32, "il2cpp_is_debugger_attached"},
        {OFFSET_WAIT_DEBUG, 32, "wait-for-native-debugger"},
        {OFFSET_PORT_DEBUG, 32, "managed-debugger-fixed-port"}
    };
    
    for (auto& p : patches) {
        void* addr = get_abs_addr(antiCheatLibName, p.offset);
        std::vector<uint8_t> zeros(p.len, 0);
        if (addr && safe_mem_write((uintptr_t)addr, zeros.data(), zeros.size())) {
            write_log(g_antiCheatLog, "✅ " + std::string(p.name) + " cleared");
            success++;
        } else {
            write_log(g_antiCheatLog, "❌ " + std::string(p.name) + " clear failed");
        }
    }
    
    write_log(g_antiCheatLog, "✅ Anti-Cheat Patched: " + std::to_string(success) + "/" + std::to_string(total));
    write_log(g_antiCheatLog, "========== PATCH COMPLETE ==========\n");
    write_debug("🛡️ Anti-Cheat patched (" + std::to_string(success) + "/11)");
}

// ======================== هوک Button.Press ========================
void hook_ButtonPress(void* button) {
    if (g_captureEnabled && g_gameReady && button && is_valid_address(button)) {
        g_clickCounter++;
        std::string name = get_button_name_safe(button);
        if (!name.empty()) {
            write_log(g_clickLog, "[Button.Press] #" + std::to_string(g_clickCounter) + " | " + name);
            write_log(g_clickLog, "   Button: 0x" + std::to_string((uintptr_t)button));
        } else {
            write_log(g_clickLog, "[Button.Press] #" + std::to_string(g_clickCounter) + " | Unknown");
            write_log(g_clickLog, "   Button: 0x" + std::to_string((uintptr_t)button));
        }
    }
    if (orig_ButtonPress) orig_ButtonPress(button);
}

// ======================== هوک GtaMenuControl.Start ========================
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

// ======================== نصب هوک‌ها (فقط با دکمه) ========================
static void InstallHooks() {
    if (g_hooksInstalled) {
        write_debug("⏭️ Hooks already installed");
        return;
    }
    
    write_debug("🔧 Installing hooks...");
    
#if defined(__aarch64__)
    void* pressAddr = getAbsoluteAddress("libil2cpp.so", OBFUSCATE("0x1F14A14"));
    if (pressAddr && is_valid_address(pressAddr)) {
        if (DobbyHook(pressAddr, (dobby_dummy_func_t)hook_ButtonPress, (dobby_dummy_func_t*)&orig_ButtonPress) == 0) {
            g_hooksInstalled = true;
            write_debug("✅ Button.Press hooked (0x1F14A14)");
        } else {
            write_debug("❌ Button.Press hook failed");
        }
    } else {
        write_debug("❌ Button.Press address not found");
    }
#endif
    
    if (g_hooksInstalled) {
        write_debug("🔧 Hooks installed");
    } else {
        write_debug("❌ Hook installation failed");
    }
}

// ======================== منو ========================
jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    g_env = env;
    const char *features[] = {
        "Category_🎯 Button Capture",
        "Toggle_🔴 Enable Capture (Install Hooks)",
        "Button_📊 Reset Counter",
        "RichTextView_📁 Logs: /sdcard/Download/lac/",
        "RichTextView_📌 click_log.txt",
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
            g_captureEnabled = boolean;
            if (boolean) {
                if (!g_hooksInstalled) {
                    InstallHooks();
                }
                g_clickCounter = 0;
                write_debug("🔴 Capture mode enabled");
                write_log(g_clickLog, "📌 Capture: ON");
            } else {
                write_debug("⚫ Capture mode disabled");
                write_log(g_clickLog, "📌 Capture: OFF");
            }
            break;
        case 1:
            g_clickCounter = 0;
            write_log(g_clickLog, "📌 Counter reset to 0");
            break;
    }
}

// ======================== ترد اصلی ========================
void hack_thread() {
    write_debug("⏳ Waiting for libil2cpp.so...");
    while (!isLibraryLoaded("libil2cpp.so")) sleep(1);
    write_debug("✅ libil2cpp.so loaded");
    
    PatchAntiCheat();
    
    void* startAddr = getAbsoluteAddress("libil2cpp.so", "0xF571A4");
    if (startAddr) {
        DobbyHook(startAddr, (dobby_dummy_func_t)hook_GtaMenuStart, (dobby_dummy_func_t*)&orig_GtaMenuStart);
        write_debug("✅ GtaMenuControl.Start() hooked (passive)");
    }
    
    for (int i = 0; i < 15; i++) {
        if (g_gameReady) break;
        sleep(1);
    }
    
    write_debug("✅ Mod ready - Anti-Cheat disabled, hooks waiting for user");
}

// ======================== تابع ورودی ========================
__attribute__((constructor))
void lib_main() {
    mkdir(g_basePath.c_str(), 0777);
    install_crash_handler();
    write_debug("🚀 Mod loaded");
    std::thread(hack_thread).detach();
}