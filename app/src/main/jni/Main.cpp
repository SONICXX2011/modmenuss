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

// ======================== هوک ========================
void (*orig_GtaMenuStart)(void *instance);
void hook_GtaMenuStart(void *instance) {
    if (instance != nullptr) {
        g_gtaInstance = instance;
        write_report("✅ Hook captured instance: 0x" + std::to_string((uintptr_t)instance));
    }
    if (orig_GtaMenuStart) {
        orig_GtaMenuStart(instance);
    }
}

// ======================== دیسیبل کردن ========================
static void disable_menu_buttons() {
    if (g_buttonsDisabled) {
        write_report("⚠️ Already disabled");
        return;
    }
    
    write_report("\n========== DISABLE ==========");
    write_report("Time: " + get_time());
    
    // منتظر بمون تا instance از هوک بیاد
    for (int i = 0; i < 15 && g_gtaInstance == nullptr; i++) {
        write_report("⏳ Waiting for instance... (" + std::to_string(i+1) + "/15)");
        sleep(1);
    }
    
    if (g_gtaInstance == nullptr) {
        write_report("❌ No instance after 15 seconds!");
        return;
    }
    
    write_report("✅ Using instance: 0x" + std::to_string((uintptr_t)g_gtaInstance));
    
    // گرفتن menuButtons
    void** menuButtons = *(void***)((uintptr_t)g_gtaInstance + OFFSET_MENU_BUTTONS);
    if (menuButtons == nullptr) {
        write_report("❌ menuButtons is null!");
        return;
    }
    write_report("✅ menuButtons at: 0x" + std::to_string((uintptr_t)menuButtons));
    
    // ====== شمارش و دیسیبل ======
    int total = 0;
    int disabled = 0;
    
    for (int i = 0; i < 25; i++) {
        void* btn = menuButtons[i];
        if (btn == nullptr) continue;
        
        total++;
        write_report("   Found button " + std::to_string(i) + ": 0x" + std::to_string((uintptr_t)btn));
        
        // LAN (0,1) و SETTINGS (4) فعال بمونن
        if (i == 0 || i == 1 || i == 4) {
            write_report("      🔵 Keeping: " + std::to_string(i));
            continue;
        }
        
        bool* interactable = (bool*)((uintptr_t)btn + OFFSET_INTERACTABLE);
        if (interactable != nullptr) {
            *interactable = false;
            disabled++;
            write_report("      ❌ Disabled: " + std::to_string(i));
        }
    }
    
    g_buttonsDisabled = true;
    write_report("✅ Total: " + std::to_string(total) + " buttons");
    write_report("✅ Disabled: " + std::to_string(disabled) + " buttons");
    write_report("========== DONE ==========\n");
}

// ======================== منو ========================
jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    jobjectArray ret;
    const char *features[] = {
        OBFUSCATE("Category_🔧 Tools"),
        OBFUSCATE("Button_Disable"),
        OBFUSCATE("RichTextView_📁 /sdcard/Download/lac/"),
    };
    int total = sizeof features / sizeof features[0];
    ret = (jobjectArray)env->NewObjectArray(total, env->FindClass("java/lang/String"), env->NewStringUTF(""));
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
            disable_menu_buttons();
            break;
        default:
            break;
    }
}

// ======================== ترد اصلی ========================
void hack_thread() {
    while (!isLibraryLoaded(targetLibName)) sleep(1);
    write_report("✅ lib loaded");
    
#if defined(__aarch64__)
    void* startAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0xF571A4"));
    if (startAddr != nullptr) {
        DobbyHook(startAddr, (dobby_dummy_func_t)hook_GtaMenuStart, (dobby_dummy_func_t*)&orig_GtaMenuStart);
        write_report("✅ Hook installed");
    }
#endif
    
    // بعد از نصب هوک، صبر کن و دیسیبل کن
    sleep(3);
    disable_menu_buttons();
    
    write_report("✅ hack done");
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