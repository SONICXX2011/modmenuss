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
#include "KittyMemory/KittyMemory.hpp"

#define targetLibName OBFUSCATE("libil2cpp.so")
#define antiCheatLibName OBFUSCATE("libunity.so")

// ======================== آفست‌های آنتی‌چیت (از r2) ========================
#define OFFSET_PTRACE_FUNC          0x10c3efc   // fcn.010c3efc (ptrace check)
#define OFFSET_STATUS_STRING        0x001733e2  // "/proc/self/status"
#define OFFSET_TRACERPID_STRING     0x001affcd  // "TracerPid:"
#define OFFSET_RO_DEBUGGABLE        0x001a6be4  // "ro.debuggable"
#define OFFSET_MAPS_STRING          0x00217f59  // "/proc/self/maps"
#define OFFSET_ROOTED_1             0x00168ea6  // "rooted_or_jailbroken"
#define OFFSET_ROOTED_2             0x0019cbf8  // "rooted_jailbroken"
#define OFFSET_CHECKSUM             0x0180da63  // "has-checksums"
#define OFFSET_IL2CPP_DEBUG         0x001fbfe7  // "il2cpp_is_debugger_attached"
#define OFFSET_WAIT_DEBUG           0x00192c78  // "wait-for-native-debugger"
#define OFFSET_PORT_DEBUG           0x001fbc4f  // "managed-debugger-fixed-port"

// ======================== مسیرهای لاگ ========================
static std::string g_basePath = "/storage/emulated/0/Download/lac/";
static std::string g_antiCheatLog = g_basePath + "anti_cheat_log.txt";
static std::string g_crashLog = g_basePath + "crash_log.txt";
static std::string g_debugLog = g_basePath + "mod_debug.txt";

static bool g_crashHandlerInstalled = false;
static bool g_gameReady = false;
static void* g_gtaInstance = nullptr;
static JNIEnv* g_env = nullptr;
static bool g_antiCheatDisabled = false;

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
    if (!toastClass) { env->ExceptionClear(); return; }
    jmethodID makeTextMethod = env->GetStaticMethodID(toastClass, "makeText", "(Landroid/content/Context;Ljava/lang/CharSequence;I)Landroid/widget/Toast;");
    if (!makeTextMethod) { env->ExceptionClear(); return; }
    jclass activityThreadClass = env->FindClass("android/app/ActivityThread");
    if (!activityThreadClass) { env->ExceptionClear(); return; }
    jmethodID currentActivityThreadMethod = env->GetStaticMethodID(activityThreadClass, "currentActivityThread", "()Landroid/app/ActivityThread;");
    if (!currentActivityThreadMethod) { env->ExceptionClear(); return; }
    jobject activityThread = env->CallStaticObjectMethod(activityThreadClass, currentActivityThreadMethod);
    if (!activityThread) { env->ExceptionClear(); return; }
    jmethodID getApplicationMethod = env->GetMethodID(activityThreadClass, "getApplication", "()Landroid/app/Application;");
    if (!getApplicationMethod) { env->ExceptionClear(); return; }
    jobject context = env->CallObjectMethod(activityThread, getApplicationMethod);
    if (!context) { env->ExceptionClear(); return; }
    jstring jMsg = env->NewStringUTF(msg.c_str());
    if (!jMsg) { env->ExceptionClear(); return; }
    jobject toast = env->CallStaticObjectMethod(toastClass, makeTextMethod, context, jMsg, 0);
    env->DeleteLocalRef(jMsg);
    if (!toast) { env->ExceptionClear(); return; }
    jmethodID showMethod = env->GetMethodID(toastClass, "show", "()V");
    if (!showMethod) { env->ExceptionClear(); return; }
    env->CallVoidMethod(toast, showMethod);
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

// ======================== توابع IL2CPP پایه ========================
static void* get_gta_instance() {
    if (g_gtaInstance && is_valid_address(g_gtaInstance)) return g_gtaInstance;
    typedef void* (*get_instance_t)();
    get_instance_t get_Instance = (get_instance_t)getAbsoluteAddress("libil2cpp.so", OBFUSCATE("0xF570C4"));
    if (!get_Instance) return nullptr;
    g_gtaInstance = get_Instance();
    return g_gtaInstance;
}

// ======================== ====== آنتی‌چیت ====== ========================

// ساختار وضعیت
struct AntiCheatStatus {
    bool ptrace_patched;
    bool status_cleared;
    bool tracerpid_cleared;
    bool ro_debuggable_cleared;
    bool maps_cleared;
    bool rooted1_cleared;
    bool rooted2_cleared;
    bool checksum_cleared;
    bool il2cpp_debug_cleared;
    bool wait_debug_cleared;
    bool port_debug_cleared;
    std::string summary;
};

// چک کردن وضعیت یک آفست
static bool check_patch_status(uintptr_t offset, size_t len, const std::vector<uint8_t>& expected, const std::string& name) {
    void* addr = get_abs_addr(antiCheatLibName, offset);
    if (!addr || !is_valid_address(addr)) {
        write_log(g_antiCheatLog, "❌ " + name + " address invalid");
        return false;
    }
    std::vector<uint8_t> buffer(len);
    if (!KittyMemory::memRead(addr, buffer.data(), len)) {
        write_log(g_antiCheatLog, "❌ " + name + " cannot read");
        return false;
    }
    bool patched = (memcmp(buffer.data(), expected.data(), expected.size()) == 0);
    write_log(g_antiCheatLog, (patched ? "✅ " : "❌ ") + name + (patched ? " PATCHED" : " ACTIVE"));
    return patched;
}

// تابع اصلی چک کردن وضعیت
static AntiCheatStatus CheckAntiCheatStatus() {
    write_log(g_antiCheatLog, "\n========== ANTI-CHEAT STATUS ==========");
    write_log(g_antiCheatLog, "Time: " + get_time());
    
    AntiCheatStatus status;
    status.ptrace_patched = false;
    status.status_cleared = false;
    status.tracerpid_cleared = false;
    status.ro_debuggable_cleared = false;
    status.maps_cleared = false;
    status.rooted1_cleared = false;
    status.rooted2_cleared = false;
    status.checksum_cleared = false;
    status.il2cpp_debug_cleared = false;
    status.wait_debug_cleared = false;
    status.port_debug_cleared = false;
    
    // 1. ptrace function
    {
        std::vector<uint8_t> expected = {0x00, 0x00, 0x80, 0xD2, 0xD6, 0x5F, 0x03, 0xC0};
        status.ptrace_patched = check_patch_status(OFFSET_PTRACE_FUNC, 8, expected, "ptrace function");
    }
    
    // 2. رشته‌ها (همه صفر)
    std::vector<uint8_t> zeros24(24, 0);
    std::vector<uint8_t> zeros16(16, 0);
    std::vector<uint8_t> zeros12(12, 0);
    std::vector<uint8_t> zeros64(64, 0);
    
    status.status_cleared = check_patch_status(OFFSET_STATUS_STRING, 24, zeros24, "/proc/self/status");
    status.tracerpid_cleared = check_patch_status(OFFSET_TRACERPID_STRING, 12, zeros12, "TracerPid:");
    status.ro_debuggable_cleared = check_patch_status(OFFSET_RO_DEBUGGABLE, 16, zeros16, "ro.debuggable");
    status.maps_cleared = check_patch_status(OFFSET_MAPS_STRING, 24, zeros24, "/proc/self/maps");
    status.rooted1_cleared = check_patch_status(OFFSET_ROOTED_1, 28, zeros24, "rooted_or_jailbroken");
    status.rooted2_cleared = check_patch_status(OFFSET_ROOTED_2, 24, zeros24, "rooted_jailbroken");
    status.checksum_cleared = check_patch_status(OFFSET_CHECKSUM, 64, zeros64, "has-checksums");
    status.il2cpp_debug_cleared = check_patch_status(OFFSET_IL2CPP_DEBUG, 32, zeros24, "il2cpp_is_debugger_attached");
    status.wait_debug_cleared = check_patch_status(OFFSET_WAIT_DEBUG, 32, zeros24, "wait-for-native-debugger");
    status.port_debug_cleared = check_patch_status(OFFSET_PORT_DEBUG, 32, zeros24, "managed-debugger-fixed-port");
    
    // جمع‌بندی
    std::string summary = "\n📊 SUMMARY:\n";
    summary += "ptrace function: " + std::string(status.ptrace_patched ? "✅" : "❌") + "\n";
    summary += "/proc/self/status: " + std::string(status.status_cleared ? "✅" : "❌") + "\n";
    summary += "TracerPid: " + std::string(status.tracerpid_cleared ? "✅" : "❌") + "\n";
    summary += "ro.debuggable: " + std::string(status.ro_debuggable_cleared ? "✅" : "❌") + "\n";
    summary += "/proc/self/maps: " + std::string(status.maps_cleared ? "✅" : "❌") + "\n";
    summary += "rooted_or_jailbroken: " + std::string(status.rooted1_cleared ? "✅" : "❌") + "\n";
    summary += "rooted_jailbroken: " + std::string(status.rooted2_cleared ? "✅" : "❌") + "\n";
    summary += "has-checksums: " + std::string(status.checksum_cleared ? "✅" : "❌") + "\n";
    summary += "il2cpp_is_debugger_attached: " + std::string(status.il2cpp_debug_cleared ? "✅" : "❌") + "\n";
    summary += "wait-for-native-debugger: " + std::string(status.wait_debug_cleared ? "✅" : "❌") + "\n";
    summary += "managed-debugger-fixed-port: " + std::string(status.port_debug_cleared ? "✅" : "❌") + "\n";
    
    write_log(g_antiCheatLog, summary);
    write_log(g_antiCheatLog, "========== CHECK COMPLETE ==========\n");
    
    status.summary = summary;
    return status;
}

// ====== غیرفعال کردن آنتی‌چیت ======
static void DisableAntiCheat() {
    write_log(g_antiCheatLog, "\n========== DISABLE ANTI-CHEAT ==========");
    write_log(g_antiCheatLog, "Time: " + get_time());
    write_debug("🔘 Disable Anti-Cheat pressed");
    show_toast("🛡️ Disabling Anti-Cheat...");
    
    int success = 0;
    int total = 11;
    
    // 1. ptrace function -> mov x0,0; ret
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
    
    // 2. پاک کردن رشته‌ها با صفر
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
    
    g_antiCheatDisabled = true;
    write_log(g_antiCheatLog, "✅ Success: " + std::to_string(success) + "/" + std::to_string(total));
    write_log(g_antiCheatLog, "========== DISABLE COMPLETE ==========\n");
    
    // دوباره چک کن
    AntiCheatStatus status = CheckAntiCheatStatus();
    if (status.ptrace_patched && status.status_cleared) {
        show_toast("🛡️ Anti-Cheat Disabled!");
    } else {
        show_toast("⚠️ Anti-Cheat partially disabled");
    }
}

// ======================== هوک‌های پایه ========================
void (*orig_GtaMenuStart)(void *instance);
void hook_GtaMenuStart(void *instance) {
    if (instance) {
        g_gtaInstance = instance;
        if (!g_gameReady) {
            g_gameReady = true;
            write_debug("✅ Game ready!");
        }
    }
    if (orig_GtaMenuStart) orig_GtaMenuStart(instance);
}

static void install_basic_hooks() {
    void* startAddr = getAbsoluteAddress("libil2cpp.so", "0xF571A4");
    if (startAddr) {
        DobbyHook(startAddr, (dobby_dummy_func_t)hook_GtaMenuStart, (dobby_dummy_func_t*)&orig_GtaMenuStart);
        write_debug("✅ GtaMenuControl.Start() hooked (passive)");
    }
}

// ======================== منو ========================
jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    g_env = env;
    const char *features[] = {
        "Category_🛡️ Anti-Cheat",
        "Button_📊 Check Anti-Cheat",
        "Button_🔓 Disable Anti-Cheat",
        "Button_📁 Logs Path",
        "RichTextView_📁 /sdcard/Download/lac/",
        "RichTextView_📌 anti_cheat_log.txt",
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
            write_log(g_antiCheatLog, "🔘 Check Anti-Cheat pressed");
            CheckAntiCheatStatus();
            show_toast("📊 Check done! See log.");
            break;
        case 1:
            DisableAntiCheat();
            break;
        case 2:
            show_toast("📁 /sdcard/Download/lac/");
            write_log(g_antiCheatLog, "📁 Logs path: " + g_basePath);
            break;
    }
}

// ======================== ترد اصلی ========================
void hack_thread() {
    write_debug("⏳ Waiting for libil2cpp.so...");
    while (!isLibraryLoaded("libil2cpp.so")) sleep(1);
    write_debug("✅ libil2cpp.so loaded");
    install_basic_hooks();
    for (int i = 0; i < 15; i++) {
        if (g_gameReady) break;
        sleep(1);
    }
    write_debug("✅ Mod ready");
}

// ======================== تابع ورودی ========================
__attribute__((constructor))
void lib_main() {
    mkdir(g_basePath.c_str(), 0777);
    install_crash_handler();
    write_debug("🚀 Mod loaded");
    std::thread(hack_thread).detach();
}