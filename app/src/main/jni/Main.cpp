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

// ======================== آفست‌ها (همه از دامپ) ========================
#define OFFSET_GET_INSTANCE         0xF570C4      // GtaMenuControl.get_Instance()
#define OFFSET_GTA_START            0xF571A4      // GtaMenuControl.Start()
#define OFFSET_GTA_CHAR_SELECT      0x40          // GtaMenuControl._charSelect
#define OFFSET_CHAR_CURRENT         0x38          // GtaCharacterSelect.currentChar
#define OFFSET_CHAR_NAME_INPUT      0x48          // GtaCharacterSelect.nameInput
#define OFFSET_INPUTFIELD_M_TEXT    0x180         // InputField.m_Text

// ======================== مسیرها ========================
static std::string g_basePath = "/storage/emulated/0/Download/lac/";
static std::string g_debugLog = g_basePath + "mod_debug.txt";
static std::string g_ipResultLog = g_basePath + "ip_result.txt";
static std::string g_crashLog = g_basePath + "crash_log.txt";
static std::string g_reportLog = g_basePath + "full_report.txt";
static std::string g_playerInfoLog = g_basePath + "player_info.txt";

// ======================== متغیرها ========================
static bool g_crashHandlerInstalled = false;
static void* g_gtaInstance = nullptr;
static bool g_gtaReady = false;

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

static void write_player_info(const std::string& msg) {
    write_log(g_playerInfoLog, msg);
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
    f << "  x0: 0x" << std::hex << sc->regs[0] << "\n";
    f << "  x1: 0x" << std::hex << sc->regs[1] << "\n";
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

// ======================== تبدیل MonoString به std::string (با توابع IL2CPP) ========================
static std::string mono_string_to_utf8(void* monoString) {
    if (monoString == nullptr) return "";
    if (!is_valid_address(monoString)) return "";
    
    // ====== il2cpp_string_length ======
    typedef int32_t (*il2cpp_string_length_t)(void* str);
    il2cpp_string_length_t il2cpp_string_length = 
        (il2cpp_string_length_t)getAbsoluteAddress("libil2cpp.so", "il2cpp_string_length");
    
    if (il2cpp_string_length == nullptr) {
        write_report("   ❌ il2cpp_string_length not found!");
        return "";
    }
    
    int length = il2cpp_string_length(monoString);
    if (length <= 0 || length > 65536) {
        write_report("   ❌ Invalid string length: " + std::to_string(length));
        return "";
    }
    
    // ====== il2cpp_string_chars ======
    typedef uint16_t* (*il2cpp_string_chars_t)(void* str);
    il2cpp_string_chars_t il2cpp_string_chars = 
        (il2cpp_string_chars_t)getAbsoluteAddress("libil2cpp.so", "il2cpp_string_chars");
    
    if (il2cpp_string_chars == nullptr) {
        write_report("   ❌ il2cpp_string_chars not found!");
        return "";
    }
    
    uint16_t* chars = il2cpp_string_chars(monoString);
    if (chars == nullptr || !is_valid_address(chars)) {
        write_report("   ❌ Cannot read string chars!");
        return "";
    }
    
    std::string result;
    result.reserve(length);
    for (int i = 0; i < length; i++) {
        result += (char)(chars[i] & 0xFF);
    }
    
    return result;
}

// ======================== گرفتن GtaMenuControl instance ========================
static void* get_gta_menu_instance() {
    typedef void* (*get_instance_t)();
    get_instance_t get_Instance = (get_instance_t)getAbsoluteAddress("libil2cpp.so", "GtaMenuControl.get_Instance");
    
    if (get_Instance == nullptr) {
        write_report("❌ GtaMenuControl.get_Instance not found!");
        return nullptr;
    }
    
    void* instance = get_Instance();
    if (instance != nullptr && is_valid_address(instance)) {
        write_report("✅ GtaMenuControl instance: 0x" + std::to_string((uintptr_t)instance));
        return instance;
    }
    
    return nullptr;
}

// ======================== گرفتن GtaCharacterSelect instance ========================
static void* get_char_select_instance() {
    if (g_gtaInstance == nullptr || !is_valid_address(g_gtaInstance)) {
        g_gtaInstance = get_gta_menu_instance();
        if (g_gtaInstance == nullptr) {
            write_report("❌ Cannot get GtaMenuControl instance!");
            return nullptr;
        }
    }
    
    void* charSelect = *(void**)((uintptr_t)g_gtaInstance + OFFSET_GTA_CHAR_SELECT);
    if (charSelect == nullptr || !is_valid_address(charSelect)) {
        write_report("❌ GtaCharacterSelect is null!");
        return nullptr;
    }
    
    return charSelect;
}

// ======================== گرفتن اسم کاربر از nameInput ========================
static std::string get_player_username() {
    void* charSelect = get_char_select_instance();
    if (charSelect == nullptr) {
        write_report("❌ Cannot get GtaCharacterSelect instance!");
        return "";
    }
    
    void* nameInput = *(void**)((uintptr_t)charSelect + OFFSET_CHAR_NAME_INPUT);
    if (nameInput == nullptr || !is_valid_address(nameInput)) {
        write_report("❌ nameInput is null!");
        return "";
    }
    
    void** mTextPtr = (void**)((uintptr_t)nameInput + OFFSET_INPUTFIELD_M_TEXT);
    if (mTextPtr == nullptr || !is_valid_address(mTextPtr)) {
        write_report("❌ m_Text pointer is invalid!");
        return "";
    }
    
    void* monoString = *mTextPtr;
    if (monoString == nullptr) {
        write_report("❌ MonoString is null!");
        return "";
    }
    
    std::string username = mono_string_to_utf8(monoString);
    write_report("   📝 Username: " + username);
    return username;
}

// ======================== گرفتن کاراکتر ID ========================
static int get_player_character_id() {
    void* charSelect = get_char_select_instance();
    if (charSelect == nullptr) {
        write_report("❌ Cannot get GtaCharacterSelect instance!");
        return -1;
    }
    
    int* currentCharPtr = (int*)((uintptr_t)charSelect + OFFSET_CHAR_CURRENT);
    if (currentCharPtr == nullptr || !is_valid_address(currentCharPtr)) {
        write_report("❌ currentChar pointer is invalid!");
        return -1;
    }
    
    int charId = *currentCharPtr;
    write_report("   🎭 Character ID: " + std::to_string(charId));
    return charId;
}

// ======================== گرفتن اطلاعات کامل پلیر ========================
static void get_player_info() {
    write_report("\n========== GET PLAYER INFO ==========");
    write_report("Time: " + get_time());
    write_player_info("\n========== PLAYER INFO ==========");
    write_player_info("Time: " + get_time());
    
    try {
        // گرفتن instance
        if (g_gtaInstance == nullptr || !is_valid_address(g_gtaInstance)) {
            g_gtaInstance = get_gta_menu_instance();
            if (g_gtaInstance == nullptr) {
                write_report("❌ Failed to get GtaMenuControl instance!");
                write_result("❌ Failed to get instance!");
                return;
            }
        }
        write_report("✅ Using GtaMenuControl instance: 0x" + std::to_string((uintptr_t)g_gtaInstance));
        
        // گرفتن اسم از nameInput
        std::string username = get_player_username();
        if (username.empty()) {
            write_report("⚠️ Username is empty!");
        }
        
        // گرفتن کاراکتر ID
        int charId = get_player_character_id();
        if (charId < 0) {
            write_report("⚠️ Character ID is invalid!");
        }
        
        // ====== نمایش نتیجه ======
        std::string result = "👤 Username: " + (username.empty() ? "(empty)" : username) + 
                            " | 🎭 Character ID: " + (charId < 0 ? "(unknown)" : std::to_string(charId));
        
        write_report("✅ " + result);
        write_report("========== DONE ==========\n");
        
        // ذخیره در فایل مخصوص
        write_player_info("✅ " + result);
        write_player_info("========== DONE ==========\n");
        write_result("✅ " + result);
        
    } catch (const std::exception& e) {
        write_report("❌ Exception: " + std::string(e.what()));
        write_log(g_crashLog, "⚠️ Exception: " + std::string(e.what()));
    } catch (...) {
        write_report("❌ Unknown exception!");
        write_log(g_crashLog, "⚠️ Unknown exception!");
    }
}

// ======================== هوک ========================
void (*orig_GtaMenuStart)(void *instance);
void hook_GtaMenuStart(void *instance) {
    if (instance != nullptr && is_valid_address(instance)) {
        g_gtaInstance = instance;
        g_gtaReady = true;
        write_report("✅ Hook captured GtaMenuControl instance: 0x" + std::to_string((uintptr_t)instance));
        write_debug("✅ GtaMenuControl instance saved");
    }
    if (orig_GtaMenuStart) {
        orig_GtaMenuStart(instance);
    }
}

// ======================== منو ========================
jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    jobjectArray ret;
    
    const char *features[] = {
        OBFUSCATE("Category_👤 Player Info"),
        OBFUSCATE("Button_Get Player Info"),
        OBFUSCATE("RichTextView_📁 /sdcard/Download/lac/player_info.txt"),
    };
    
    int total = sizeof features / sizeof features[0];
    ret = (jobjectArray)env->NewObjectArray(total, env->FindClass(OBFUSCATE("java/lang/String")), env->NewStringUTF(""));
    
    if (ret == nullptr) {
        write_report("❌ Failed to create jobjectArray!");
        return nullptr;
    }
    
    for (int i = 0; i < total; i++) {
        env->SetObjectArrayElement(ret, i, env->NewStringUTF(features[i]));
    }
    
    write_report("✅ GetFeatureList returned " + std::to_string(total) + " features");
    return ret;
}

// ======================== تغییرات منو ========================
void Changes(JNIEnv *env, jclass clazz, jobject obj, jint featNum, jstring featName,
             jint value, jlong Lvalue, jboolean boolean, jstring text) {

    switch (featNum) {
        case 0:  // دکمه Get Player Info
            write_report("\n🔘 Get Player Info button pressed");
            write_result("🔘 Get Player Info button pressed");
            get_player_info();
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
    // ====== نصب هوک ======
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

    // ====== تلاش برای گرفتن instance ======
    for (int i = 0; i < 15 && g_gtaInstance == nullptr; i++) {
        sleep(1);
    }
    
    if (g_gtaInstance != nullptr) {
        write_report("✅ Got GtaMenuControl instance after wait");
    } else {
        // تلاش مستقیم با get_Instance
        g_gtaInstance = get_gta_menu_instance();
        if (g_gtaInstance != nullptr) {
            write_report("✅ Got GtaMenuControl instance via get_Instance");
        }
    }

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