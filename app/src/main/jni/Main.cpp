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

// ======================== آفست‌ها (از دامپ) ========================
#define OFFSET_GET_INSTANCE     0xF570C4      // GtaMenuControl.get_Instance()
#define OFFSET_GTA_START        0xF571A4      // GtaMenuControl.Start()
#define OFFSET_MENU_BUTTONS     0x30          // GtaMenuControl.menuButtons
#define OFFSET_INTERACTABLE     0xD8          // Selectable.m_Interactable

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

// ======================== کرش‌گیر کامل ========================
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

// ======================== چک کردن آدرس معتبر ========================
static bool is_valid_address(void* addr) {
    if (addr == nullptr) return false;
    uintptr_t ptr = (uintptr_t)addr;
    // آدرس‌های خیلی کوچک (مثل 0x8) یا خیلی بزرگ معتبر نیستن
    if (ptr < 0x1000) return false;
    if (ptr > 0x7FFFFFFFFFFFFF) return false;
    // چک کن که قابل خوندن باشه (با تابع KittyMemory)
    auto map = KittyMemory::getAddressMap(addr);
    if (!map.readable) return false;
    return true;
}

// ======================== گرفتن instance با get_Instance ========================
static void* get_instance_getter() {
    typedef void* (*get_instance_t)();
    get_instance_t get_Instance = (get_instance_t)getAbsoluteAddress("libil2cpp.so", "GtaMenuControl.get_Instance");
    if (get_Instance == nullptr) {
        write_report("   ❌ get_Instance not found!");
        return nullptr;
    }
    void* instance = get_Instance();
    if (instance != nullptr && is_valid_address(instance)) {
        write_report("   ✅ get_Instance: 0x" + std::to_string((uintptr_t)instance));
        return instance;
    }
    return nullptr;
}

// ======================== هوک روی Start ========================
void (*orig_GtaMenuStart)(void *instance);
void hook_GtaMenuStart(void *instance) {
    if (instance != nullptr && is_valid_address(instance)) {
        g_gtaInstance = instance;
        write_report("✅ Hook captured instance: 0x" + std::to_string((uintptr_t)instance));
    }
    if (orig_GtaMenuStart) {
        orig_GtaMenuStart(instance);
    }
}

// ======================== دیسیبل کردن دکمه‌ها ========================
static void disable_menu_buttons() {
    if (g_buttonsDisabled) {
        write_report("⚠️ Already disabled, skipping...");
        return;
    }
    
    write_report("\n========== DISABLE MENU BUTTONS ==========");
    write_report("Time: " + get_time());
    
    // ====== مرحله 1: گرفتن instance ======
    void* instance = nullptr;
    
    // تلاش 1: get_Instance
    write_report("   Method 1: get_Instance()");
    instance = get_instance_getter();
    if (instance != nullptr) {
        write_report("✅ Got instance via get_Instance");
        g_gtaInstance = instance;
    }
    
    // تلاش 2: منتظر هوک
    if (instance == nullptr) {
        write_report("   Method 2: Waiting for hook...");
        for (int i = 0; i < 15 && g_gtaInstance == nullptr; i++) {
            write_report("   ⏳ Waiting... (" + std::to_string(i+1) + "/15)");
            sleep(1);
        }
        instance = g_gtaInstance;
        if (instance != nullptr) {
            write_report("✅ Got instance via hook");
        }
    }
    
    // تلاش 3: FindObjectOfType (JNI)
    if (instance == nullptr) {
        write_report("   Method 3: FindObjectOfType");
        JNIEnv* env = nullptr;
        // فرض میکنیم env از قبل موجوده
        // اینجا نیاز به env داریم که از تابع Changes میاد
        write_report("   ⚠️ Skipping (needs JNIEnv)");
    }
    
    if (instance == nullptr || !is_valid_address(instance)) {
        write_report("❌ Failed to get instance!");
        write_result("❌ Failed to get instance!");
        return;
    }
    
    write_report("✅ Using instance: 0x" + std::to_string((uintptr_t)instance));
    
    // ====== مرحله 2: گرفتن menuButtons ======
    void** menuButtons = *(void***)((uintptr_t)instance + OFFSET_MENU_BUTTONS);
    if (menuButtons == nullptr || !is_valid_address(menuButtons)) {
        write_report("❌ menuButtons is null or invalid!");
        write_result("❌ menuButtons invalid!");
        return;
    }
    write_report("✅ menuButtons at: 0x" + std::to_string((uintptr_t)menuButtons));
    
    // ====== مرحله 3: شمارش و دیسیبل کردن ======
    int totalButtons = 0;
    int disabledCount = 0;
    int invalidCount = 0;
    
    for (int i = 0; i < 30; i++) {
        void* btn = menuButtons[i];
        
        // اگه آدرس معتبر نباشه، رد کن
        if (!is_valid_address(btn)) {
            invalidCount++;
            continue;
        }
        
        totalButtons++;
        
        // LAN (0,1) و SETTINGS (4) فعال بمونن
        if (i == 0 || i == 1 || i == 4) {
            write_report("   🔵 Keeping active: index " + std::to_string(i) + " (0x" + std::to_string((uintptr_t)btn) + ")");
            continue;
        }
        
        // غیرفعال کردن با m_Interactable
        bool* interactable = (bool*)((uintptr_t)btn + OFFSET_INTERACTABLE);
        if (interactable != nullptr && is_valid_address(interactable)) {
            *interactable = false;
            disabledCount++;
            write_report("   ❌ Disabled: index " + std::to_string(i) + " (0x" + std::to_string((uintptr_t)btn) + ")");
        } else {
            write_report("   ⚠️ Cannot disable index " + std::to_string(i) + " (interactable invalid)");
        }
    }
    
    g_buttonsDisabled = true;
    write_report("✅ Total buttons found: " + std::to_string(totalButtons));
    write_report("✅ Disabled buttons: " + std::to_string(disabledCount));
    write_report("✅ Invalid addresses skipped: " + std::to_string(invalidCount));
    write_result("✅ Disabled " + std::to_string(disabledCount) + " buttons (LAN & SETTINGS kept active)");
    write_report("========== DISABLE COMPLETE ==========\n");
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
            write_result("🔘 Disable button pressed");
            g_buttonsDisabled = false;
            disable_menu_buttons();
            break;
            
        case 1:
            write_report("🔘 Enable button pressed");
            write_result("🔘 Enable button pressed");
            if (g_gtaInstance != nullptr && is_valid_address(g_gtaInstance)) {
                void** menuButtons = *(void***)((uintptr_t)g_gtaInstance + OFFSET_MENU_BUTTONS);
                if (menuButtons != nullptr && is_valid_address(menuButtons)) {
                    int enabled = 0;
                    for (int i = 0; i < 30; i++) {
                        void* btn = menuButtons[i];
                        if (!is_valid_address(btn)) continue;
                        bool* interactable = (bool*)((uintptr_t)btn + OFFSET_INTERACTABLE);
                        if (interactable != nullptr && is_valid_address(interactable)) {
                            *interactable = true;
                            enabled++;
                        }
                    }
                    g_buttonsDisabled = false;
                    write_report("✅ Enabled " + std::to_string(enabled) + " buttons");
                    write_result("✅ Enabled " + std::to_string(enabled) + " buttons");
                }
            } else {
                write_report("❌ No valid instance to enable buttons");
            }
            break;
            
        default:
            write_report("Unknown featNum: " + std::to_string(featNum));
            break;
    }
}

// ======================== ترد اصلی ========================
void hack_thread() {
    int waitCount = 0;

    write_report("⏳ Waiting for libil2cpp.so to load...");
    write_debug("⏳ Waiting for libil2cpp.so...");

    while (!isLibraryLoaded(targetLibName) && waitCount < 30) {
        sleep(1);
        waitCount++;
    }

    if (waitCount >= 30) {
        write_report("❌ Timeout waiting for libil2cpp.so");
        write_debug("⏰ Timeout!");
        return;
    }

    write_report("✅ libil2cpp.so loaded successfully");
    write_debug("✅ libil2cpp.so loaded!");

#if defined(__aarch64__)
    // ====== نصب هوک روی Start ======
    void* startAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0xF571A4"));
    if (startAddr != nullptr && is_valid_address(startAddr)) {
        int res = DobbyHook(startAddr, (dobby_dummy_func_t)hook_GtaMenuStart, (dobby_dummy_func_t*)&orig_GtaMenuStart);
        if (res == 0) {
            write_report("✅ GtaMenuControl.Start() hooked at 0x" + std::to_string((uintptr_t)startAddr));
            write_debug("✅ GtaMenuControl.Start() hooked");
        } else {
            write_report("❌ Failed to hook GtaMenuControl.Start()! error: " + std::to_string(res));
            write_debug("❌ Hook failed");
        }
    } else {
        write_report("❌ GtaMenuControl.Start() address not found!");
        write_debug("❌ Address not found");
    }
#endif

    // ====== تلاش اولیه برای دیسیبل کردن ======
    disable_menu_buttons();

    write_report("✅ hack_thread finished successfully");
    write_debug("✅ hack_thread finished");
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

    write_report("🚀 lib_main called - mod loading");
    write_debug("🚀 lib_main called");
    std::thread(hack_thread).detach();
}