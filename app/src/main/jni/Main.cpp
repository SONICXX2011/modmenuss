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

// ======================== IP پیش‌فرض ========================
#define DEFAULT_IP "5.57.37.224"
#define DEFAULT_PORT "9876"

// ======================== مسیرها ========================
static std::string g_basePath = "/storage/emulated/0/Download/lac/";
static std::string g_debugLog = g_basePath + "mod_debug.txt";
static std::string g_lastIPFile = g_basePath + "last_ip.txt";
static std::string g_ipResultLog = g_basePath + "ip_result.txt";
static std::string g_crashLog = g_basePath + "crash_log.txt";
static std::string g_reportLog = g_basePath + "full_report.txt";

// ======================== متغیرها ========================
static std::string g_targetIP = "";
static bool g_crashHandlerInstalled = false;
static void* g_gtaInstance = nullptr;
static bool g_gtaReady = false;
static bool g_buttonsDisabled = false;

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

static void create_directory() {
    mkdir(g_basePath.c_str(), 0777);
}

static void save_ip(const std::string& ip) {
    std::ofstream f(g_lastIPFile);
    if (f.is_open()) {
        f << ip << "\n";
        f.close();
        write_report("📝 IP saved: " + ip);
    }
}

static std::string load_ip() {
    std::ifstream f(g_lastIPFile);
    if (f.is_open()) {
        std::string ip;
        std::getline(f, ip);
        f.close();
        return ip;
    }
    return "";
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

// ======================== دیسیبل کردن دکمه‌ها (به جز LAN و SETTINGS) ========================
static void disable_menu_buttons() {
    if (g_buttonsDisabled) {
        write_report("⚠️ Buttons already disabled, skipping...");
        return;
    }
    
    write_report("\n========== DISABLE MENU BUTTONS ==========");
    write_report("Time: " + get_time());
    
    if (g_gtaInstance == nullptr) {
        write_report("❌ GtaMenuControl instance is null!");
        write_log(g_ipResultLog, "❌ GtaMenuControl instance is null!");
        return;
    }
    
    try {
        // گرفتن آرایه دکمه‌ها
        void** menuButtons = *(void***)((uintptr_t)g_gtaInstance + OFFSET_MENU_BUTTONS);
        if (menuButtons == nullptr) {
            write_report("❌ menuButtons is null!");
            write_log(g_ipResultLog, "❌ menuButtons is null!");
            return;
        }
        write_report("✅ menuButtons array: 0x" + std::to_string((uintptr_t)menuButtons));
        
        // دکمه‌هایی که باید فعال بمونن (LAN=0, LAN=1, SETTINGS=4)
        int keepActive[] = {0, 1, 4};
        int totalButtons = 8;
        int disabledCount = 0;
        
        for (int i = 0; i < totalButtons; i++) {
            // چک کن آیا این دکمه باید فعال بمونه؟
            bool shouldKeep = false;
            for (int j = 0; j < 3; j++) {
                if (keepActive[j] == i) {
                    shouldKeep = true;
                    break;
                }
            }
            
            if (shouldKeep) {
                write_report("   🔵 Keeping active: index " + std::to_string(i));
                continue;
            }
            
            void* button = menuButtons[i];
            if (button == nullptr) {
                write_report("   ⚠️ Button index " + std::to_string(i) + " is null!");
                continue;
            }
            
            // غیرفعال کردن دکمه (m_Interactable = false)
            bool* interactable = (bool*)((uintptr_t)button + OFFSET_INTERACTABLE);
            *interactable = false;
            disabledCount++;
            write_report("   ❌ Disabled button index: " + std::to_string(i));
        }
        
        g_buttonsDisabled = true;
        write_report("✅ " + std::to_string(disabledCount) + " buttons disabled! LAN and SETTINGS kept active!");
        write_log(g_ipResultLog, "✅ " + std::to_string(disabledCount) + " buttons disabled!");
        write_report("========== DISABLE COMPLETE ==========\n");
        
    } catch (const std::exception& e) {
        write_report("❌ Exception: " + std::string(e.what()));
        write_log(g_crashLog, "⚠️ Exception: " + std::string(e.what()));
    } catch (...) {
        write_report("❌ Unknown exception!");
        write_log(g_crashLog, "⚠️ Unknown exception!");
    }
}

// ======================== هوک روی GtaMenuControl.Start ========================
void (*orig_GtaMenuStart)(void *instance);
void hook_GtaMenuStart(void *instance) {
    write_report("========== GtaMenuControl.Start() HOOKED ==========");
    write_report("Time: " + get_time());
    
    if (instance != nullptr) {
        g_gtaInstance = instance;
        g_gtaReady = true;
        write_report("✅ GtaMenuControl instance saved: 0x" + std::to_string((uintptr_t)instance));
        write_debug("✅ GtaMenuControl instance saved");
        
        // بعد از گرفتن instance، دکمه‌ها رو دیسیبل کن
        disable_menu_buttons();
    } else {
        write_report("❌ instance is null!");
    }
    write_report("====================================================\n");
    
    if (orig_GtaMenuStart) {
        orig_GtaMenuStart(instance);
    }
}

// ======================== گرفتن instance با get_Instance ========================
static void* get_gta_instance() {
    typedef void* (*get_instance_t)();
    get_instance_t get_Instance = (get_instance_t)getAbsoluteAddress("libil2cpp.so", "GtaMenuControl.get_Instance");
    
    if (get_Instance == nullptr) {
        write_report("❌ GtaMenuControl.get_Instance not found!");
        return nullptr;
    }
    
    void* instance = get_Instance();
    if (instance != nullptr) {
        write_report("✅ GtaMenuControl instance via get_Instance: 0x" + std::to_string((uintptr_t)instance));
    }
    
    return instance;
}

// ======================== منو ========================
jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    jobjectArray ret;
    const char *features[] = {
        OBFUSCATE("Category_🔧 Tools"),
        OBFUSCATE("Button_Disable Menu Buttons"),  // featNum: 0
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
        case 0:  // دکمه Disable Menu Buttons
            write_report("🔘 Disable Menu Buttons button pressed");
            write_log(g_ipResultLog, "🔘 Disable Menu Buttons button pressed");
            disable_menu_buttons();
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

    // ====== بارگذاری IP ======
    g_targetIP = load_ip();
    if (g_targetIP.empty()) {
        write_report("📌 No saved IP, using default: " DEFAULT_IP ":" DEFAULT_PORT);
        g_targetIP = DEFAULT_IP ":" DEFAULT_PORT;
        save_ip(g_targetIP);
    } else {
        write_report("📌 Loaded IP from file: " + g_targetIP);
    }
    write_debug("📌 Current IP: " + g_targetIP);

    // ====== روش 1: گرفتن instance با get_Instance ======
    write_report("🔍 Trying get_Instance()...");
    void* instance1 = get_gta_instance();
    if (instance1 != nullptr) {
        g_gtaInstance = instance1;
        g_gtaReady = true;
        write_report("✅ Got instance via get_Instance()");
        // دیسیبل کردن دکمه‌ها
        disable_menu_buttons();
    }

#if defined(__aarch64__)
    // ====== روش 2: هوک روی Start ======
    if (!g_gtaReady) {
        write_report("🔍 Trying hook on GtaMenuControl.Start()...");
        void* startAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0xF571A4"));
        if (startAddr != nullptr) {
            int res = DobbyHook(startAddr, (dobby_dummy_func_t)hook_GtaMenuStart, (dobby_dummy_func_t*)&orig_GtaMenuStart);
            if (res == 0) {
                write_report("✅ GtaMenuControl.Start() hooked at 0x" + std::to_string((uintptr_t)startAddr));
                write_debug("✅ GtaMenuControl.Start() hooked");
            } else {
                write_report("❌ Failed to hook GtaMenuControl.Start()! error: " + std::to_string(res));
                write_debug("❌ GtaMenuControl.Start() hook failed");
            }
        } else {
            write_report("❌ GtaMenuControl.Start() address not found!");
            write_debug("❌ GtaMenuControl.Start() address not found");
        }
    }
#endif

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