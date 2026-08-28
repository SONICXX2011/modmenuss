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
#include <iomanip>
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

// ======================== آفست‌ها (همه از دامپ) ========================
#define OFFSET_GET_INSTANCE     0xF570C4
#define OFFSET_GTA_START        0xF571A4
#define OFFSET_MENU_BUTTONS     0x30
#define OFFSET_INTERACTABLE     0xD8
#define OFFSET_ENABLE_CALLED    0x20
#define OFFSET_GROUPS_ALLOW     0xE8
#define OFFSET_TEXT_COMPONENT   0x108
#define MAX_BUTTONS             30
#define MAX_WAIT_ATTEMPTS       30

// ======================== مسیرها ========================
static std::string g_basePath = "/storage/emulated/0/Download/lac/";
static std::string g_debugLog = g_basePath + "mod_debug.txt";
static std::string g_ipResultLog = g_basePath + "ip_result.txt";
static std::string g_crashLog = g_basePath + "crash_log.txt";
static std::string g_reportLog = g_basePath + "full_report.txt";
static std::string g_buttonsReport = g_basePath + "buttons_report.txt";

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

static void write_report(const std::string& msg) {
    write_log(g_reportLog, msg);
}

static void write_result(const std::string& msg) {
    write_log(g_ipResultLog, msg);
}

static void write_buttons_report(const std::string& msg) {
    write_log(g_buttonsReport, msg);
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

// ======================== چک آدرس معتبر ========================
static bool is_valid_address(void* addr) {
    if (addr == nullptr) return false;
    uintptr_t ptr = (uintptr_t)addr;
    if (ptr < 0x1000) return false;
    if (ptr > 0x7FFFFFFFFFFFFF) return false;
    auto map = KittyMemory::getAddressMap(addr);
    return map.readable;
}

// ======================== گرفتن متن دکمه (امن) ========================
static std::string get_button_text_safe(void* button) {
    if (button == nullptr) return "NULL";
    if (!is_valid_address(button)) return "INVALID";
    
    uintptr_t textAddr = (uintptr_t)button + OFFSET_TEXT_COMPONENT;
    if (!is_valid_address((void*)textAddr)) return "NO_TEXT_ADDR";
    
    void* textComponent = *(void**)textAddr;
    if (textComponent == nullptr || !is_valid_address(textComponent)) return "NO_TEXT_COMP";
    
    typedef const char* (*get_text_t)(void*);
    get_text_t get_text = (get_text_t)getAbsoluteAddress("libil2cpp.so", "Text.get_text");
    if (get_text == nullptr) return "NO_GET_TEXT";
    
    const char* text = get_text(textComponent);
    if (text == nullptr) return "NULL_TEXT";
    
    return std::string(text);
}

// ======================== گرفتن instance با انتظار برای لودینگ ========================
static void* get_gta_instance_with_wait() {
    void* instance = nullptr;
    
    // روش ۱: get_Instance
    typedef void* (*get_instance_t)();
    get_instance_t get_Instance = (get_instance_t)getAbsoluteAddress("libil2cpp.so", "GtaMenuControl.get_Instance");
    
    for (int attempt = 0; attempt < MAX_WAIT_ATTEMPTS; attempt++) {
        // امتحان get_Instance
        if (get_Instance != nullptr) {
            instance = get_Instance();
            if (instance != nullptr && is_valid_address(instance)) {
                write_report("   ✅ get_Instance() succeeded on attempt " + std::to_string(attempt + 1));
                return instance;
            }
        }
        
        // امتحان هوک
        if (g_gtaInstance != nullptr && is_valid_address(g_gtaInstance)) {
            write_report("   ✅ Hook captured instance on attempt " + std::to_string(attempt + 1));
            return g_gtaInstance;
        }
        
        // منتظر بمون تا لودینگ تموم بشه
        if (attempt < MAX_WAIT_ATTEMPTS - 1) {
            write_report("   ⏳ Waiting for menu to load... (" + std::to_string(attempt + 1) + "/" + std::to_string(MAX_WAIT_ATTEMPTS) + ")");
            sleep(1);
        }
    }
    
    write_report("❌ Failed to get instance after " + std::to_string(MAX_WAIT_ATTEMPTS) + " attempts!");
    return nullptr;
}

// ======================== هوک ========================
void (*orig_GtaMenuStart)(void *instance);
void hook_GtaMenuStart(void *instance) {
    if (instance != nullptr && is_valid_address(instance)) {
        g_gtaInstance = instance;
        write_report("✅ Hook captured: 0x" + std::to_string((uintptr_t)instance));
    }
    if (orig_GtaMenuStart) {
        orig_GtaMenuStart(instance);
    }
}

// ======================== دیسیبل کردن بر اساس اسم ========================
static void disable_menu_buttons_by_name() {
    if (g_buttonsDisabled) {
        write_report("⚠️ Already disabled");
        return;
    }
    
    write_report("\n========== DISABLE BY NAME ==========");
    write_report("Time: " + get_time());
    
    // ====== صبر کن تا منو لود بشه ======
    void* instance = get_gta_instance_with_wait();
    if (instance == nullptr || !is_valid_address(instance)) {
        write_report("❌ No instance after waiting!");
        write_result("❌ No instance!");
        return;
    }
    
    write_report("✅ Using instance: 0x" + std::to_string((uintptr_t)instance));
    
    // ====== گرفتن menuButtons ======
    void** menuButtons = *(void***)((uintptr_t)instance + OFFSET_MENU_BUTTONS);
    if (menuButtons == nullptr || !is_valid_address(menuButtons)) {
        write_report("❌ menuButtons invalid!");
        write_result("❌ menuButtons invalid!");
        return;
    }
    write_report("✅ menuButtons at: 0x" + std::to_string((uintptr_t)menuButtons));
    
    // ====== گزارش اولیه ======
    write_buttons_report("\n========== BEFORE DISABLE ==========");
    write_buttons_report("Time: " + get_time());
    write_buttons_report("+-------+------------------+----------------------+----------------------+--------+");
    write_buttons_report("| Index | Name             | Address              | Interactable         | Active |");
    write_buttons_report("+-------+------------------+----------------------+----------------------+--------+");
    
    int total = 0, disabled = 0;
    
    for (int i = 0; i < MAX_BUTTONS; i++) {
        void* btn = menuButtons[i];
        if (btn == nullptr || !is_valid_address(btn)) {
            std::string status = (btn == nullptr) ? "NULL" : "INVALID";
            std::stringstream ss;
            ss << "| " << std::setw(5) << i << " | " << std::setw(16) << status 
               << " | " << std::setw(20) << "N/A" 
               << " | " << std::setw(20) << "N/A" 
               << " | " << std::setw(6) << "N/A" << " |";
            write_buttons_report(ss.str());
            continue;
        }
        
        total++;
        std::string name = get_button_text_safe(btn);
        if (name.length() > 16) name = name.substr(0, 13) + "...";
        
        bool* interactable = (bool*)((uintptr_t)btn + OFFSET_INTERACTABLE);
        std::string intStatus = (interactable != nullptr && is_valid_address(interactable)) 
                                ? (*interactable ? "true" : "false") : "ERR";
        
        std::stringstream ss;
        ss << "| " << std::setw(5) << i 
           << " | " << std::setw(16) << name 
           << " | 0x" << std::setw(18) << std::hex << (uintptr_t)btn 
           << " | " << std::setw(20) << intStatus 
           << " | " << std::setw(6) << "?" << " |";
        write_buttons_report(ss.str());
        
        // ====== دیسیبل کردن ======
        // فقط LAN و SETTINGS رو فعال نگه دار
        std::string fullName = get_button_text_safe(btn);
        if (fullName == "LAN" || fullName == "SETTINGS") {
            write_report("   🔵 Keeping: " + fullName + " (index " + std::to_string(i) + ")");
            continue;
        }
        
        // روش 1: m_Interactable = false
        if (interactable != nullptr && is_valid_address(interactable)) {
            *interactable = false;
        }
        
        // روش 2: m_EnableCalled = false
        bool* enableCalled = (bool*)((uintptr_t)btn + OFFSET_ENABLE_CALLED);
        if (enableCalled != nullptr && is_valid_address(enableCalled)) {
            *enableCalled = false;
        }
        
        // روش 3: m_GroupsAllowInteraction = false
        bool* groupsAllow = (bool*)((uintptr_t)btn + OFFSET_GROUPS_ALLOW);
        if (groupsAllow != nullptr && is_valid_address(groupsAllow)) {
            *groupsAllow = false;
        }
        
        disabled++;
        write_report("   ❌ Disabled: " + fullName + " (index " + std::to_string(i) + ")");
    }
    
    write_buttons_report("+-------+------------------+----------------------+----------------------+--------+");
    write_buttons_report("========== END OF REPORT ==========\n");
    
    g_buttonsDisabled = true;
    write_report("✅ Total: " + std::to_string(total) + ", Disabled: " + std::to_string(disabled));
    write_result("✅ Disabled " + std::to_string(disabled) + " buttons (kept LAN and SETTINGS)");
    write_report("========== DONE ==========\n");
}

// ======================== فعال کردن دکمه‌ها ========================
static void enable_menu_buttons() {
    if (g_gtaInstance == nullptr || !is_valid_address(g_gtaInstance)) {
        write_report("❌ No instance!");
        return;
    }
    
    void** menuButtons = *(void***)((uintptr_t)g_gtaInstance + OFFSET_MENU_BUTTONS);
    if (menuButtons == nullptr || !is_valid_address(menuButtons)) {
        write_report("❌ menuButtons invalid!");
        return;
    }
    
    int enabled = 0;
    for (int i = 0; i < MAX_BUTTONS; i++) {
        void* btn = menuButtons[i];
        if (btn == nullptr || !is_valid_address(btn)) continue;
        
        bool* interactable = (bool*)((uintptr_t)btn + OFFSET_INTERACTABLE);
        if (interactable != nullptr && is_valid_address(interactable)) {
            *interactable = true;
            enabled++;
        }
        
        bool* enableCalled = (bool*)((uintptr_t)btn + OFFSET_ENABLE_CALLED);
        if (enableCalled != nullptr && is_valid_address(enableCalled)) {
            *enableCalled = true;
        }
        
        bool* groupsAllow = (bool*)((uintptr_t)btn + OFFSET_GROUPS_ALLOW);
        if (groupsAllow != nullptr && is_valid_address(groupsAllow)) {
            *groupsAllow = true;
        }
    }
    
    g_buttonsDisabled = false;
    write_report("✅ Enabled " + std::to_string(enabled) + " buttons");
    write_result("✅ Enabled " + std::to_string(enabled) + " buttons");
}

// ======================== منو ========================
jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    jobjectArray ret;
    const char *features[] = {
        OBFUSCATE("Category_🔧 Tools"),
        OBFUSCATE("Button_Disable (keep LAN & SETTINGS)"),
        OBFUSCATE("Button_Enable All"),
        OBFUSCATE("RichTextView_📁 /sdcard/Download/lac/"),
    };
    int total = sizeof features / sizeof features[0];
    ret = (jobjectArray)env->NewObjectArray(total, env->FindClass(OBFUSCATE("java/lang/String")), env->NewStringUTF(""));
    for (int i = 0; i < total; i++) {
        env->SetObjectArrayElement(ret, i, env->NewStringUTF(features[i]));
    }
    return ret;
}

void Changes(JNIEnv *env, jclass clazz, jobject obj, jint featNum, jstring featName,
             jint value, jlong Lvalue, jboolean boolean, jstring text) {
    switch (featNum) {
        case 0:
            write_report("🔘 Disable button pressed");
            g_buttonsDisabled = false;
            disable_menu_buttons_by_name();
            break;
        case 1:
            write_report("🔘 Enable button pressed");
            enable_menu_buttons();
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
    if (startAddr != nullptr && is_valid_address(startAddr)) {
        DobbyHook(startAddr, (dobby_dummy_func_t)hook_GtaMenuStart, (dobby_dummy_func_t*)&orig_GtaMenuStart);
        write_report("✅ Hook installed");
    }
#endif
    
    // ====== صبر کن تا منو لود بشه و بعد دیسیبل کن ======
    // اینجا منتظر میمونه تا instance بیاد
    for (int i = 0; i < 30 && g_gtaInstance == nullptr; i++) {
        sleep(1);
    }
    
    if (g_gtaInstance != nullptr) {
        disable_menu_buttons_by_name();
    } else {
        write_report("⚠️ Instance not available after 30 seconds, will retry on button press");
    }
    
    write_report("✅ hack done");
}

// ======================== تابع ورودی ========================
__attribute__((constructor))
void lib_main() {
    create_directory();
    install_crash_handler();
    write_report("🚀 lib_main called");
    std::thread(hack_thread).detach();
}