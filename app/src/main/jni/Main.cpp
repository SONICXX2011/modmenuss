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
#define OFFSET_GET_INSTANCE     0xF570C4
#define OFFSET_GTA_START        0xF571A4
#define OFFSET_MENU_BUTTONS     0x30
#define OFFSET_INTERACTABLE     0xD8
#define OFFSET_ENABLE_CALLED    0x20
#define OFFSET_GROUPS_ALLOW     0xE8
#define MAX_BUTTONS             30
#define MAX_WAIT                20

// ======================== مسیرها ========================
static std::string g_basePath = "/storage/emulated/0/Download/lac/";
static std::string g_debugLog = g_basePath + "mod_debug.txt";
static std::string g_ipResultLog = g_basePath + "ip_result.txt";
static std::string g_crashLog = g_basePath + "crash_log.txt";
static std::string g_reportLog = g_basePath + "full_report.txt";
static std::string g_configFile = g_basePath + "buttons_config.txt";

static bool g_crashHandlerInstalled = false;
static bool g_buttonsDisabled = false;
static void* g_gtaInstance = nullptr;

// ======================== لیست ایندکس‌های دیسیبل (0 تا 29) ========================
static bool g_disableState[MAX_BUTTONS] = {false};

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

// ======================== گرفتن instance ========================
static void* get_gta_instance() {
    // روش 1: get_Instance
    typedef void* (*get_instance_t)();
    get_instance_t get_Instance = (get_instance_t)getAbsoluteAddress("libil2cpp.so", "GtaMenuControl.get_Instance");
    if (get_Instance != nullptr) {
        void* instance = get_Instance();
        if (instance != nullptr && is_valid_address(instance)) {
            return instance;
        }
    }
    
    // روش 2: منتظر هوک
    for (int i = 0; i < MAX_WAIT && g_gtaInstance == nullptr; i++) {
        sleep(1);
    }
    if (g_gtaInstance != nullptr && is_valid_address(g_gtaInstance)) {
        return g_gtaInstance;
    }
    return nullptr;
}

// ======================== هوک ========================
void (*orig_GtaMenuStart)(void *instance);
void hook_GtaMenuStart(void *instance) {
    if (instance != nullptr && is_valid_address(instance)) {
        g_gtaInstance = instance;
        write_report("✅ Hook captured");
    }
    if (orig_GtaMenuStart) {
        orig_GtaMenuStart(instance);
    }
}

// ======================== دیسیبل کردن یک ایندکس ========================
static void disable_single_index(int index) {
    if (g_gtaInstance == nullptr || !is_valid_address(g_gtaInstance)) {
        write_report("❌ No instance for index " + std::to_string(index));
        return;
    }
    
    void** menuButtons = *(void***)((uintptr_t)g_gtaInstance + OFFSET_MENU_BUTTONS);
    if (menuButtons == nullptr || !is_valid_address(menuButtons)) {
        write_report("❌ menuButtons invalid");
        return;
    }
    
    if (index < 0 || index >= MAX_BUTTONS) {
        write_report("❌ Index out of range: " + std::to_string(index));
        return;
    }
    
    void* btn = menuButtons[index];
    if (btn == nullptr || !is_valid_address(btn)) {
        write_report("❌ Button at index " + std::to_string(index) + " is invalid");
        return;
    }
    
    // روش 1: m_Interactable
    bool* interactable = (bool*)((uintptr_t)btn + OFFSET_INTERACTABLE);
    if (interactable != nullptr && is_valid_address(interactable)) {
        *interactable = false;
    }
    
    // روش 2: m_EnableCalled
    bool* enableCalled = (bool*)((uintptr_t)btn + OFFSET_ENABLE_CALLED);
    if (enableCalled != nullptr && is_valid_address(enableCalled)) {
        *enableCalled = false;
    }
    
    // روش 3: m_GroupsAllowInteraction
    bool* groupsAllow = (bool*)((uintptr_t)btn + OFFSET_GROUPS_ALLOW);
    if (groupsAllow != nullptr && is_valid_address(groupsAllow)) {
        *groupsAllow = false;
    }
    
    write_report("   ❌ Disabled index " + std::to_string(index));
}

// ======================== اعمال دیسیبل کردن ========================
static void apply_disabled() {
    if (g_gtaInstance == nullptr || !is_valid_address(g_gtaInstance)) {
        write_report("❌ No instance for apply");
        write_result("❌ No instance");
        return;
    }
    
    write_report("\n========== APPLY DISABLED ==========");
    write_report("Time: " + get_time());
    
    int count = 0;
    for (int i = 0; i < MAX_BUTTONS; i++) {
        if (g_disableState[i]) {
            disable_single_index(i);
            count++;
        }
    }
    
    g_buttonsDisabled = true;
    write_report("✅ Applied " + std::to_string(count) + " indices");
    write_result("✅ Applied " + std::to_string(count) + " indices");
    write_report("========== DONE ==========\n");
}

// ======================== فعال کردن همه ========================
static void enable_all() {
    if (g_gtaInstance == nullptr || !is_valid_address(g_gtaInstance)) {
        write_report("❌ No instance");
        return;
    }
    
    void** menuButtons = *(void***)((uintptr_t)g_gtaInstance + OFFSET_MENU_BUTTONS);
    if (menuButtons == nullptr || !is_valid_address(menuButtons)) {
        write_report("❌ menuButtons invalid");
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
    
    for (int i = 0; i < MAX_BUTTONS; i++) {
        g_disableState[i] = false;
    }
    g_buttonsDisabled = false;
    write_report("✅ Enabled " + std::to_string(enabled) + " buttons");
    write_result("✅ Enabled all buttons");
}

// ======================== ذخیره تنظیمات ========================
static void save_config() {
    std::ofstream f(g_configFile);
    if (f.is_open()) {
        for (int i = 0; i < MAX_BUTTONS; i++) {
            if (g_disableState[i]) {
                f << i << "\n";
            }
        }
        f.close();
        write_report("✅ Config saved");
        write_result("✅ Config saved");
    } else {
        write_report("❌ Failed to save config");
    }
}

// ======================== بارگذاری تنظیمات ========================
static void load_config() {
    for (int i = 0; i < MAX_BUTTONS; i++) {
        g_disableState[i] = false;
    }
    
    std::ifstream f(g_configFile);
    if (f.is_open()) {
        int idx;
        while (f >> idx) {
            if (idx >= 0 && idx < MAX_BUTTONS) {
                g_disableState[idx] = true;
            }
        }
        f.close();
        write_report("✅ Config loaded");
    } else {
        write_report("⚠️ No config, default: keep 0 and 4 active");
        // پیش‌فرض: همه به جز 0 و 4 دیسیبل
        for (int i = 0; i < MAX_BUTTONS; i++) {
            if (i != 0 && i != 4) {
                g_disableState[i] = true;
            }
        }
    }
}

// ======================== منو ========================
jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    load_config();
    
    std::vector<std::string> features;
    features.push_back("Category_🔘 Button Manager (0-29)");
    
    // 30 تا CheckBox برای ایندکس‌های 0 تا 29
    for (int i = 0; i < MAX_BUTTONS; i++) {
        std::string label = "CheckBox_Index " + std::to_string(i);
        if (g_disableState[i]) {
            label = "True_" + label;
        }
        features.push_back(label);
    }
    
    features.push_back("Category_");
    features.push_back("Button_Apply Selected");
    features.push_back("Button_Enable All");
    features.push_back("Button_Extract & Save");
    features.push_back("RichTextView_📁 /sdcard/Download/lac/");
    
    jobjectArray ret = (jobjectArray)env->NewObjectArray(
        features.size(),
        env->FindClass("java/lang/String"),
        env->NewStringUTF("")
    );
    
    for (size_t i = 0; i < features.size(); i++) {
        env->SetObjectArrayElement(ret, i, env->NewStringUTF(features[i].c_str()));
    }
    
    return ret;
}

// ======================== تغییرات منو ========================
void Changes(JNIEnv *env, jclass clazz, jobject obj, jint featNum, jstring featName,
             jint value, jlong Lvalue, jboolean boolean, jstring text) {

    // featNum 0-29 = CheckBox ایندکس
    if (featNum >= 0 && featNum < MAX_BUTTONS) {
        g_disableState[featNum] = boolean;
        write_report("   " + std::to_string(featNum) + " -> " + (boolean ? "ON" : "OFF"));
        return;
    }
    
    // 30 = Apply, 31 = Enable All, 32 = Extract & Save
    switch (featNum) {
        case 30:
            write_report("🔘 Apply Selected pressed");
            apply_disabled();
            break;
        case 31:
            write_report("🔘 Enable All pressed");
            enable_all();
            break;
        case 32:
            write_report("🔘 Extract & Save pressed");
            save_config();
            break;
        default:
            write_report("Unknown featNum: " + std::to_string(featNum));
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
    
    // منتظر لودینگ
    for (int i = 0; i < MAX_WAIT && g_gtaInstance == nullptr; i++) {
        sleep(1);
    }
    
    load_config();
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