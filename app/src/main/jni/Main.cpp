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
static std::string g_applyLog = g_basePath + "apply_log.txt";

static bool g_crashHandlerInstalled = false;
static bool g_buttonsDisabled = false;
static void* g_gtaInstance = nullptr;

// ======================== لیست ایندکس‌های دیسیبل (از InputText) ========================
static std::vector<int> g_selectedIndices;

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

static void write_apply_log(const std::string& msg) {
    write_log(g_applyLog, msg);
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

// ======================== parse کردن ایندکس‌های وارد شده ========================
static std::vector<int> parse_indices(const std::string& input) {
    std::vector<int> result;
    std::stringstream ss(input);
    std::string token;
    
    while (std::getline(ss, token, ',')) {
        // حذف فاصله‌های اضافی
        token.erase(0, token.find_first_not_of(" \t\n\r"));
        token.erase(token.find_last_not_of(" \t\n\r") + 1);
        
        if (token.empty()) continue;
        
        char* endptr;
        long val = strtol(token.c_str(), &endptr, 10);
        if (*endptr == '\0' && val >= 0 && val < MAX_BUTTONS) {
            result.push_back((int)val);
        } else {
            write_report("   ⚠️ Invalid index: " + token);
        }
    }
    return result;
}

// ======================== دیسیبل کردن یک ایندکس ========================
static bool disable_single_index(int index, bool log_enabled) {
    if (g_gtaInstance == nullptr || !is_valid_address(g_gtaInstance)) {
        if (log_enabled) write_report("   ❌ No instance for index " + std::to_string(index));
        return false;
    }
    
    void** menuButtons = *(void***)((uintptr_t)g_gtaInstance + OFFSET_MENU_BUTTONS);
    if (menuButtons == nullptr || !is_valid_address(menuButtons)) {
        if (log_enabled) write_report("   ❌ menuButtons invalid");
        return false;
    }
    
    if (index < 0 || index >= MAX_BUTTONS) {
        if (log_enabled) write_report("   ❌ Index out of range: " + std::to_string(index));
        return false;
    }
    
    void* btn = menuButtons[index];
    if (btn == nullptr || !is_valid_address(btn)) {
        if (log_enabled) write_report("   ❌ Button at index " + std::to_string(index) + " is invalid");
        return false;
    }
    
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
    
    if (log_enabled) {
        write_report("   ❌ Disabled index " + std::to_string(index));
        write_apply_log("   Disabled index " + std::to_string(index));
    }
    return true;
}

// ======================== اعمال دیسیبل کردن بر اساس لیست ========================
static void apply_selected_indices() {
    if (g_selectedIndices.empty()) {
        write_report("⚠️ No indices selected to disable!");
        write_result("⚠️ No indices selected!");
        return;
    }
    
    if (g_gtaInstance == nullptr || !is_valid_address(g_gtaInstance)) {
        write_report("❌ No instance for apply");
        write_result("❌ No instance");
        return;
    }
    
    write_report("\n========== APPLY SELECTED INDICES ==========");
    write_report("Time: " + get_time());
    write_apply_log("\n========== APPLY LOG ==========");
    write_apply_log("Time: " + get_time());
    
    std::string indicesStr;
    for (size_t i = 0; i < g_selectedIndices.size(); i++) {
        if (i > 0) indicesStr += ", ";
        indicesStr += std::to_string(g_selectedIndices[i]);
    }
    write_report("📌 Indices to disable: " + indicesStr);
    write_apply_log("📌 Indices to disable: " + indicesStr);
    
    int successCount = 0;
    for (int idx : g_selectedIndices) {
        if (disable_single_index(idx, true)) {
            successCount++;
        }
    }
    
    g_buttonsDisabled = true;
    write_report("✅ Successfully disabled " + std::to_string(successCount) + " of " + std::to_string(g_selectedIndices.size()) + " indices");
    write_apply_log("✅ Successfully disabled " + std::to_string(successCount) + " of " + std::to_string(g_selectedIndices.size()) + " indices");
    write_result("✅ Disabled " + std::to_string(successCount) + " indices");
    write_report("========== DONE ==========\n");
    write_apply_log("========== DONE ==========\n");
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
    
    g_selectedIndices.clear();
    g_buttonsDisabled = false;
    write_report("✅ Enabled " + std::to_string(enabled) + " buttons");
    write_result("✅ Enabled all buttons");
    write_apply_log("✅ Enabled all buttons");
}

// ======================== ذخیره تنظیمات ========================
static void save_config() {
    std::ofstream f(g_configFile);
    if (f.is_open()) {
        for (int idx : g_selectedIndices) {
            f << idx << "\n";
        }
        f.close();
        write_report("✅ Config saved (" + std::to_string(g_selectedIndices.size()) + " indices)");
        write_result("✅ Config saved");
        write_apply_log("✅ Config saved: " + std::to_string(g_selectedIndices.size()) + " indices");
    } else {
        write_report("❌ Failed to save config");
    }
}

// ======================== بارگذاری تنظیمات ========================
static void load_config() {
    g_selectedIndices.clear();
    
    std::ifstream f(g_configFile);
    if (f.is_open()) {
        int idx;
        while (f >> idx) {
            if (idx >= 0 && idx < MAX_BUTTONS) {
                g_selectedIndices.push_back(idx);
            }
        }
        f.close();
        write_report("✅ Config loaded (" + std::to_string(g_selectedIndices.size()) + " indices)");
    } else {
        write_report("⚠️ No config file");
    }
}

// ======================== منو ========================
jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    load_config();
    
    // 6 المان: Category, InputText, 3 دکمه, RichTextView
    const int totalFeatures = 6;
    
    jobjectArray ret = (jobjectArray)env->NewObjectArray(
        totalFeatures,
        env->FindClass(OBFUSCATE("java/lang/String")),
        env->NewStringUTF("")
    );
    
    if (ret == nullptr) {
        write_report("❌ Failed to create jobjectArray!");
        return nullptr;
    }
    
    int idx = 0;
    env->SetObjectArrayElement(ret, idx++, env->NewStringUTF(OBFUSCATE("Category_🔘 Button Disabler")));
    env->SetObjectArrayElement(ret, idx++, env->NewStringUTF(OBFUSCATE("InputText_Enter Indices (e.g. 0,4,5)")));
    env->SetObjectArrayElement(ret, idx++, env->NewStringUTF(OBFUSCATE("Button_Apply Selected")));
    env->SetObjectArrayElement(ret, idx++, env->NewStringUTF(OBFUSCATE("Button_Enable All")));
    env->SetObjectArrayElement(ret, idx++, env->NewStringUTF(OBFUSCATE("Button_Extract & Save")));
    env->SetObjectArrayElement(ret, idx++, env->NewStringUTF(OBFUSCATE("RichTextView_📁 /sdcard/Download/lac/")));
    
    write_report("✅ GetFeatureList returned " + std::to_string(totalFeatures) + " features");
    return ret;
}

// ======================== تغییرات منو ========================
void Changes(JNIEnv *env, jclass clazz, jobject obj, jint featNum, jstring featName,
             jint value, jlong Lvalue, jboolean boolean, jstring text) {

    // featNum 0 = InputText (ورودی ایندکس‌ها)
    // featNum 1 = Apply Selected
    // featNum 2 = Enable All
    // featNum 3 = Extract & Save
    
    switch (featNum) {
        case 0: {
            // ====== InputText: دریافت ایندکس‌ها ======
            const char* textStr = (text != nullptr) ? env->GetStringUTFChars(text, nullptr) : "";
            if (textStr != nullptr && strlen(textStr) > 0) {
                std::string input(textStr);
                g_selectedIndices = parse_indices(input);
                write_report("📝 Indices entered: " + input);
                write_report("📌 Parsed " + std::to_string(g_selectedIndices.size()) + " indices");
                write_result("📝 Entered: " + input + " -> " + std::to_string(g_selectedIndices.size()) + " indices");
                env->ReleaseStringUTFChars(text, textStr);
            } else {
                write_report("⚠️ Empty input");
            }
            break;
        }
        
        case 1:
            // ====== Apply Selected ======
            write_report("🔘 Apply Selected pressed");
            write_result("🔘 Apply Selected pressed");
            apply_selected_indices();
            break;
            
        case 2:
            // ====== Enable All ======
            write_report("🔘 Enable All pressed");
            write_result("🔘 Enable All pressed");
            enable_all();
            break;
            
        case 3:
            // ====== Extract & Save ======
            write_report("🔘 Extract & Save pressed");
            write_result("🔘 Extract & Save pressed");
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