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

// ======================== ذخیره تنظیمات دکمه‌ها ========================
static std::vector<int> g_disabledIndices;
static std::vector<int> g_validIndices;

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
    typedef void* (*get_instance_t)();
    get_instance_t get_Instance = (get_instance_t)getAbsoluteAddress("libil2cpp.so", "GtaMenuControl.get_Instance");
    if (get_Instance != nullptr) {
        void* instance = get_Instance();
        if (instance != nullptr && is_valid_address(instance)) {
            return instance;
        }
    }
    
    for (int i = 0; i < 15 && g_gtaInstance == nullptr; i++) {
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
        write_report("✅ Hook captured: 0x" + std::to_string((uintptr_t)instance));
    }
    if (orig_GtaMenuStart) {
        orig_GtaMenuStart(instance);
    }
}

// ======================== اسکن دکمه‌های معتبر ========================
static void scan_valid_buttons() {
    g_validIndices.clear();
    
    void* instance = get_gta_instance();
    if (instance == nullptr || !is_valid_address(instance)) {
        write_report("❌ No instance for scanning!");
        return;
    }
    
    void** menuButtons = *(void***)((uintptr_t)instance + OFFSET_MENU_BUTTONS);
    if (menuButtons == nullptr || !is_valid_address(menuButtons)) {
        write_report("❌ menuButtons invalid!");
        return;
    }
    
    for (int i = 0; i < MAX_BUTTONS; i++) {
        void* btn = menuButtons[i];
        if (btn != nullptr && is_valid_address(btn)) {
            g_validIndices.push_back(i);
        }
    }
    
    write_report("✅ Scanned " + std::to_string(g_validIndices.size()) + " valid buttons");
}

// ======================== دیسیبل کردن یک ایندکس خاص ========================
static void disable_button_index(int index) {
    if (g_gtaInstance == nullptr || !is_valid_address(g_gtaInstance)) {
        write_report("❌ No instance!");
        return;
    }
    
    void** menuButtons = *(void***)((uintptr_t)g_gtaInstance + OFFSET_MENU_BUTTONS);
    if (menuButtons == nullptr || !is_valid_address(menuButtons)) {
        write_report("❌ menuButtons invalid!");
        return;
    }
    
    if (index < 0 || index >= MAX_BUTTONS) {
        write_report("❌ Invalid index: " + std::to_string(index));
        return;
    }
    
    void* btn = menuButtons[index];
    if (btn == nullptr || !is_valid_address(btn)) {
        write_report("❌ Button at index " + std::to_string(index) + " is invalid!");
        return;
    }
    
    // ۳ روش دیسیبل
    bool* interactable = (bool*)((uintptr_t)btn + OFFSET_INTERACTABLE);
    if (interactable != nullptr && is_valid_address(interactable)) {
        *interactable = false;
    }
    
    bool* enableCalled = (bool*)((uintptr_t)btn + OFFSET_ENABLE_CALLED);
    if (enableCalled != nullptr && is_valid_address(enableCalled)) {
        *enableCalled = false;
    }
    
    bool* groupsAllow = (bool*)((uintptr_t)btn + OFFSET_GROUPS_ALLOW);
    if (groupsAllow != nullptr && is_valid_address(groupsAllow)) {
        *groupsAllow = false;
    }
    
    write_report("   ❌ Disabled index: " + std::to_string(index));
}

// ======================== اعمال دیسیبل کردن همه ========================
static void apply_disabled_indices() {
    if (g_gtaInstance == nullptr || !is_valid_address(g_gtaInstance)) {
        write_report("❌ No instance!");
        return;
    }
    
    write_report("\n========== APPLY DISABLED INDICES ==========");
    write_report("Time: " + get_time());
    write_report("Disabled indices: " + std::to_string(g_disabledIndices.size()));
    
    for (int idx : g_disabledIndices) {
        disable_button_index(idx);
    }
    
    g_buttonsDisabled = true;
    write_report("✅ Applied " + std::to_string(g_disabledIndices.size()) + " disabled indices");
    write_result("✅ Applied " + std::to_string(g_disabledIndices.size()) + " disabled indices");
    write_report("========== DONE ==========\n");
}

// ======================== ذخیره تنظیمات در فایل ========================
static void save_config() {
    std::ofstream f(g_configFile);
    if (f.is_open()) {
        for (int idx : g_disabledIndices) {
            f << idx << "\n";
        }
        f.close();
        write_report("✅ Config saved: " + std::to_string(g_disabledIndices.size()) + " indices");
        write_result("✅ Config saved with " + std::to_string(g_disabledIndices.size()) + " indices");
    } else {
        write_report("❌ Failed to save config!");
    }
}

// ======================== بارگذاری تنظیمات از فایل ========================
static void load_config() {
    g_disabledIndices.clear();
    
    std::ifstream f(g_configFile);
    if (f.is_open()) {
        int idx;
        while (f >> idx) {
            g_disabledIndices.push_back(idx);
        }
        f.close();
        write_report("✅ Config loaded: " + std::to_string(g_disabledIndices.size()) + " indices");
    } else {
        write_report("⚠️ No config file found, using default");
        // پیش‌فرض: فقط index 0 و 4 رو فعال نگه دار
        g_disabledIndices = {1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29};
    }
}

// ======================== منو ========================
jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    // ====== اول دکمه‌های معتبر رو اسکن کن ======
    scan_valid_buttons();
    load_config();
    
    std::vector<std::string> features;
    features.push_back("Category_🔘 Button Manager");
    features.push_back("Category_Select indices to disable:");
    
    // ====== دکمه‌های 0 تا 29 رو نشون بده ======
    for (int i = 0; i < MAX_BUTTONS; i++) {
        std::string label = "CheckBox_Index " + std::to_string(i);
        
        // اگه ایندکس معتبر نیست، غیرفعال کن
        bool isValid = false;
        for (int validIdx : g_validIndices) {
            if (validIdx == i) {
                isValid = true;
                break;
            }
        }
        
        if (!isValid) {
            label += " (INVALID)";
        }
        
        // اگه توی لیست دیسیبل هست، چک بزن
        bool isChecked = false;
        for (int disabledIdx : g_disabledIndices) {
            if (disabledIdx == i) {
                isChecked = true;
                break;
            }
        }
        
        if (isChecked) {
            label = "True_" + label;
        }
        
        features.push_back(label.c_str());
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

    // featNum: 0 = Index 0, 1 = Index 1, ... 29 = Index 29
    // 30 = Apply, 31 = Enable All, 32 = Extract & Save
    
    if (featNum >= 0 && featNum < MAX_BUTTONS) {
        // انتخاب/لغو انتخاب یک ایندکس
        int index = featNum;
        
        if (boolean) {
            // اضافه کردن به لیست دیسیبل
            bool alreadyExists = false;
            for (int idx : g_disabledIndices) {
                if (idx == index) {
                    alreadyExists = true;
                    break;
                }
            }
            if (!alreadyExists) {
                g_disabledIndices.push_back(index);
                write_report("   ➕ Added index " + std::to_string(index) + " to disable list");
            }
        } else {
            // حذف از لیست دیسیبل
            for (auto it = g_disabledIndices.begin(); it != g_disabledIndices.end(); ++it) {
                if (*it == index) {
                    g_disabledIndices.erase(it);
                    write_report("   ➖ Removed index " + std::to_string(index) + " from disable list");
                    break;
                }
            }
        }
        return;
    }
    
    // دکمه‌های کنترل
    switch (featNum) {
        case 30:  // Apply Selected
            write_report("🔘 Apply Selected button pressed");
            apply_disabled_indices();
            break;
            
        case 31:  // Enable All
            write_report("🔘 Enable All button pressed");
            if (g_gtaInstance != nullptr && is_valid_address(g_gtaInstance)) {
                void** menuButtons = *(void***)((uintptr_t)g_gtaInstance + OFFSET_MENU_BUTTONS);
                if (menuButtons != nullptr && is_valid_address(menuButtons)) {
                    int enabled = 0;
                    for (int i = 0; i < MAX_BUTTONS; i++) {
                        void* btn = menuButtons[i];
                        if (btn == nullptr || !is_valid_address(btn)) continue;
                        
                        bool* interactable = (bool*)((uintptr_t)btn + OFFSET_INTERACTABLE);
                        if (interactable != nullptr && is_valid_address(interactable)) {
                            *interactable = true;
                            enabled++;
                        }
                    }
                    g_buttonsDisabled = false;
                    g_disabledIndices.clear();
                    write_report("✅ Enabled " + std::to_string(enabled) + " buttons");
                    write_result("✅ Enabled all buttons");
                }
            }
            break;
            
        case 32:  // Extract & Save
            write_report("🔘 Extract & Save button pressed");
            save_config();
            write_report("✅ Config saved to: " + g_configFile);
            write_result("✅ Config saved!");
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
    
    // صبر کن تا منو لود بشه
    for (int i = 0; i < 20 && g_gtaInstance == nullptr; i++) {
        sleep(1);
    }
    
    // اسکن دکمه‌ها
    scan_valid_buttons();
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