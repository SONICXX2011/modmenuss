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
#include "KittyMemory/KittyMemory.hpp"

#define targetLibName OBFUSCATE("libil2cpp.so")
#define antiCheatLibName OBFUSCATE("libunity.so")

// ======================== آفست‌های آنتی‌چیت (از تحلیل) ========================
#define OFFSET_PTRACE_FUNC          0x10c3efc   // fcn.010c3efc (ptrace check)
#define OFFSET_STATUS_STRING        0x001733e1  // "/proc/self/status"
#define OFFSET_TRACERPID_STRING     0x001affcd  // "TracerPid:"
#define OFFSET_RO_DEBUGGABLE        0x001a6be4  // "ro.debuggable"
#define OFFSET_MAPS_STRING          0x00217f58  // "/proc/self/maps"
#define OFFSET_ROOTED_1             0x00168ea6  // "rooted_or_jailbroken"
#define OFFSET_ROOTED_2             0x0019cbf8  // "rooted_jailbroken"
#define OFFSET_CHECKSUM             0x01801a30  // "has-checksums"

// ======================== مسیرهای لاگ ========================
static std::string g_basePath = "/storage/emulated/0/Download/lac/";
static std::string g_antiCheatLog = g_basePath + "anti_cheat_log.txt";
static std::string g_crashLog = g_basePath + "crash_log.txt";
static std::string g_debugLog = g_basePath + "mod_debug.txt";

static bool g_crashHandlerInstalled = false;
static bool g_gameReady = false;
static void* g_gtaInstance = nullptr;
static JNIEnv* g_env = nullptr;

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

// ======================== Toast ساده (با JNIEnv ذخیره شده) ========================
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

// ======================== امنیت حافظه ========================
static bool is_valid_address(void* addr) {
    if (!addr) return false;
    uintptr_t ptr = (uintptr_t)addr;
    if (ptr < 0x1000) return false;
    if (ptr > 0x7FFFFFFFFFFFFF) return false;
    auto map = KittyMemory::getAddressMap(addr);
    return map.readable;
}

static bool safe_mem_read(uintptr_t addr, void* buffer, size_t len) {
    if (!is_valid_address((void*)addr)) return false;
    return KittyMemory::memRead((const void*)addr, buffer, len);
}

static bool safe_mem_write(uintptr_t addr, const void* buffer, size_t len) {
    if (!is_valid_address((void*)addr)) return false;
    return KittyMemory::memWrite((void*)addr, buffer, len);
}

// ======================== توابع IL2CPP پایه ========================
static void* get_absolute_address(const char* lib, uintptr_t offset) {
    return getAbsoluteAddress(lib, std::to_string(offset).c_str());
}

// ======================== ====== آنتی‌چیت ====== ========================

// ساختار برای ذخیره وضعیت
struct AntiCheatStatus {
    bool ptrace_patched;
    bool status_string_cleared;
    bool tracerpid_cleared;
    bool ro_debuggable_cleared;
    bool maps_cleared;
    bool rooted_1_cleared;
    bool rooted_2_cleared;
    bool checksum_cleared;
    std::string details;
};

static AntiCheatStatus g_acStatus;

// چک کردن وضعیت هر آفست
static bool check_patch_status(uintptr_t offset, const std::vector<uint8_t>& expected, const std::string& name) {
    uintptr_t addr = (uintptr_t)get_absolute_address(antiCheatLibName, offset);
    if (!addr || !is_valid_address((void*)addr)) {
        write_log(g_antiCheatLog, "❌ " + name + " address invalid (0x" + std::to_string(addr) + ")");
        return false;
    }
    std::vector<uint8_t> buffer(expected.size());
    if (!safe_mem_read(addr, buffer.data(), buffer.size())) {
        write_log(g_antiCheatLog, "❌ " + name + " cannot read memory");
        return false;
    }
    bool patched = (memcmp(buffer.data(), expected.data(), expected.size()) == 0);
    write_log(g_antiCheatLog, (patched ? "✅ " : "❌ ") + name + (patched ? " is PATCHED" : " is ACTIVE"));
    return patched;
}

// تابع اصلی چک کردن وضعیت آنتی‌چیت
static AntiCheatStatus CheckAntiCheatStatus() {
    write_log(g_antiCheatLog, "\n========== ANTI-CHEAT STATUS CHECK ==========");
    write_log(g_antiCheatLog, "Time: " + get_time());
    
    AntiCheatStatus status;
    status.ptrace_patched = false;
    status.status_string_cleared = false;
    status.tracerpid_cleared = false;
    status.ro_debuggable_cleared = false;
    status.maps_cleared = false;
    status.rooted_1_cleared = false;
    status.rooted_2_cleared = false;
    status.checksum_cleared = false;
    status.details = "";
    
    // 1. ptrace function (0x10c3efc) - expected: mov x0,0; ret (0xd2800000, 0xc0035fd6)
    {
        std::vector<uint8_t> expected = {0x00, 0x00, 0x80, 0xD2, 0xD6, 0x5F, 0x03, 0xC0}; // little-endian
        status.ptrace_patched = check_patch_status(OFFSET_PTRACE_FUNC, expected, "ptrace function");
    }
    
    // 2. "/proc/self/status" string (0x001733e1) - all zeros
    {
        std::vector<uint8_t> expected(20, 0); // پاک شده با صفر
        status.status_string_cleared = check_patch_status(OFFSET_STATUS_STRING, expected, "/proc/self/status string");
    }
    
    // 3. "TracerPid:" string (0x001affcd) - all zeros
    {
        std::vector<uint8_t> expected(12, 0);
        status.tracerpid_cleared = check_patch_status(OFFSET_TRACERPID_STRING, expected, "TracerPid: string");
    }
    
    // 4. "ro.debuggable" string (0x001a6be4) - all zeros
    {
        std::vector<uint8_t> expected(16, 0);
        status.ro_debuggable_cleared = check_patch_status(OFFSET_RO_DEBUGGABLE, expected, "ro.debuggable string");
    }
    
    // 5. "/proc/self/maps" string (0x00217f58) - all zeros
    {
        std::vector<uint8_t> expected(20, 0);
        status.maps_cleared = check_patch_status(OFFSET_MAPS_STRING, expected, "/proc/self/maps string");
    }
    
    // 6. "rooted_or_jailbroken" (0x00168ea6) - all zeros
    {
        std::vector<uint8_t> expected(24, 0);
        status.rooted_1_cleared = check_patch_status(OFFSET_ROOTED_1, expected, "rooted_or_jailbroken");
    }
    
    // 7. "rooted_jailbroken" (0x0019cbf8) - all zeros
    {
        std::vector<uint8_t> expected(24, 0);
        status.rooted_2_cleared = check_patch_status(OFFSET_ROOTED_2, expected, "rooted_jailbroken");
    }
    
    // 8. "has-checksums" (0x01801a30) - all zeros
    {
        std::vector<uint8_t> expected(64, 0);
        status.checksum_cleared = check_patch_status(OFFSET_CHECKSUM, expected, "has-checksums");
    }
    
    // جمع‌بندی
    std::string summary = "\n📊 SUMMARY:\n";
    summary += "ptrace function: " + std::string(status.ptrace_patched ? "✅ PATCHED" : "❌ ACTIVE") + "\n";
    summary += "/proc/self/status: " + std::string(status.status_string_cleared ? "✅ PATCHED" : "❌ ACTIVE") + "\n";
    summary += "TracerPid: " + std::string(status.tracerpid_cleared ? "✅ PATCHED" : "❌ ACTIVE") + "\n";
    summary += "ro.debuggable: " + std::string(status.ro_debuggable_cleared ? "✅ PATCHED" : "❌ ACTIVE") + "\n";
    summary += "/proc/self/maps: " + std::string(status.maps_cleared ? "✅ PATCHED" : "❌ ACTIVE") + "\n";
    summary += "rooted_or_jailbroken: " + std::string(status.rooted_1_cleared ? "✅ PATCHED" : "❌ ACTIVE") + "\n";
    summary += "rooted_jailbroken: " + std::string(status.rooted_2_cleared ? "✅ PATCHED" : "❌ ACTIVE") + "\n";
    summary += "has-checksums: " + std::string(status.checksum_cleared ? "✅ PATCHED" : "❌ ACTIVE") + "\n";
    
    write_log(g_antiCheatLog, summary);
    write_log(g_antiCheatLog, "========== CHECK COMPLETE ==========\n");
    
    status.details = summary;
    g_acStatus = status;
    return status;
}

// ====== غیرفعال کردن آنتی‌چیت ======
static void DisableAntiCheat() {
    write_log(g_antiCheatLog, "\n========== DISABLE ANTI-CHEAT ==========");
    write_log(g_antiCheatLog, "Time: " + get_time());
    write_debug("🔘 Disable Anti-Cheat pressed");
    
    int successCount = 0;
    int total = 8;
    
    // 1. ptrace function (0x10c3efc) -> mov x0,0; ret
    {
        uintptr_t addr = (uintptr_t)get_absolute_address(antiCheatLibName, OFFSET_PTRACE_FUNC);
        uint32_t patch[] = {0xd2800000, 0xc0035fd6}; // mov x0,0; ret
        if (safe_mem_write(addr, patch, 8)) {
            write_log(g_antiCheatLog, "✅ ptrace function patched");
            successCount++;
        } else {
            write_log(g_antiCheatLog, "❌ ptrace function patch failed");
        }
    }
    
    // 2. پاک کردن رشته‌ها با صفر
    struct StringPatch {
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
        {OFFSET_CHECKSUM, 64, "has-checksums"}
    };
    
    for (auto& p : patches) {
        uintptr_t addr = (uintptr_t)get_absolute_address(antiCheatLibName, p.offset);
        std::vector<uint8_t> zeros(p.len, 0);
        if (safe_mem_write(addr, zeros.data(), zeros.size())) {
            write_log(g_antiCheatLog, "✅ " + std::string(p.name) + " cleared");
            successCount++;
        } else {
            write_log(g_antiCheatLog, "❌ " + std::string(p.name) + " clear failed");
        }
    }
    
    write_log(g_antiCheatLog, "✅ Total patches applied: " + std::to_string(successCount) + "/" + std::to_string(total));
    write_log(g_antiCheatLog, "========== DISABLE COMPLETE ==========\n");
    
    // دوباره وضعیت رو چک کن و به کاربر نشون بده
    AntiCheatStatus newStatus = CheckAntiCheatStatus();
    if (newStatus.ptrace_patched && newStatus.status_string_cleared) {
        show_toast("🛡️ Anti-Cheat Disabled!");
    } else {
        show_toast("⚠️ Anti-Cheat partially disabled");
    }
}

// ======================== هوک‌های پایه (فقط برای گرفتن instance) ========================
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
        "Category_🛡️ Anti-Cheat Tools",
        "Button_📊 Check Anti-Cheat Status",
        "Button_🔓 Disable Anti-Cheat",
        "Button_📋 Show Logs Path",
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
    // صبر کن تا game ready بشه
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