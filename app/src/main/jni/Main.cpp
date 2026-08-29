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
#include "KittyMemory/KittyMemory.hpp"

#define targetLibName OBFUSCATE("libil2cpp.so")

// ======================== IP پیش‌فرض ========================
#define DEFAULT_IP "5.57.37.224"
#define DEFAULT_PORT "9876"

// ======================== آفست‌ها (همه از دامپ) ========================
#define OFFSET_GET_INSTANCE         0xF570C4      // GtaMenuControl.get_Instance()
#define OFFSET_GTA_START            0xF571A4      // GtaMenuControl.Start()
#define OFFSET_IP_INPUT             0xC0          // GtaMenuControl.ipInput
#define OFFSET_INPUTFIELD_M_TEXT    0x180         // InputField.m_Text
#define OFFSET_NETWORK_MANAGER      0x258         // GtaMenuControl._networkManager
#define OFFSET_NETWORK_ADDR         0x50          // NetworkManager.networkAddress
#define OFFSET_START_CLIENT         0x1AACCB4     // NetworkManager.StartClient()

// ======================== مسیرها ========================
static std::string g_basePath = "/storage/emulated/0/Download/lac/";
static std::string g_debugLog = g_basePath + "mod_debug.txt";
static std::string g_ipResultLog = g_basePath + "ip_result.txt";
static std::string g_crashLog = g_basePath + "crash_log.txt";
static std::string g_reportLog = g_basePath + "full_report.txt";
static std::string g_connectLog = g_basePath + "connect_log.txt";

static bool g_crashHandlerInstalled = false;
static void* g_gtaInstance = nullptr;
static void* g_networkManagerInstance = nullptr;
static bool g_gtaReady = false;
static std::string g_targetIP = "";

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

static void write_connect_log(const std::string& msg) {
    write_log(g_connectLog, msg);
}

static void write_crash(const std::string& msg) {
    write_log(g_crashLog, msg);
}

static void create_directory() {
    mkdir(g_basePath.c_str(), 0777);
}

static void save_ip(const std::string& ip) {
    std::ofstream f(g_basePath + "last_ip.txt");
    if (f.is_open()) {
        f << ip << "\n";
        f.close();
        write_report("📝 IP saved: " + ip);
    }
}

static std::string load_ip() {
    std::ifstream f(g_basePath + "last_ip.txt");
    if (f.is_open()) {
        std::string ip;
        std::getline(f, ip);
        f.close();
        return ip;
    }
    return "";
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

// ======================== تبدیل MonoString به std::string ========================
static std::string mono_string_to_utf8(void* monoString) {
    if (monoString == nullptr) return "";
    if (!is_valid_address(monoString)) return "";
    
    typedef int32_t (*il2cpp_string_length_t)(void* str);
    il2cpp_string_length_t il2cpp_string_length = 
        (il2cpp_string_length_t)getAbsoluteAddress("libil2cpp.so", "il2cpp_string_length");
    if (il2cpp_string_length == nullptr) return "";
    
    int length = il2cpp_string_length(monoString);
    if (length <= 0 || length > 65536) return "";
    
    typedef uint16_t* (*il2cpp_string_chars_t)(void* str);
    il2cpp_string_chars_t il2cpp_string_chars = 
        (il2cpp_string_chars_t)getAbsoluteAddress("libil2cpp.so", "il2cpp_string_chars");
    if (il2cpp_string_chars == nullptr) return "";
    
    uint16_t* chars = il2cpp_string_chars(monoString);
    if (chars == nullptr) return "";
    
    std::string result;
    result.reserve(length);
    for (int i = 0; i < length; i++) {
        result += (char)(chars[i] & 0xFF);
    }
    return result;
}

// ======================== ساخت MonoString جدید ========================
static void* create_mono_string(const char* str) {
    if (str == nullptr) return nullptr;
    
    typedef void* (*il2cpp_string_new_t)(const char*);
    il2cpp_string_new_t il2cpp_string_new = 
        (il2cpp_string_new_t)getAbsoluteAddress("libil2cpp.so", "il2cpp_string_new");
    if (il2cpp_string_new == nullptr) {
        write_connect_log("❌ il2cpp_string_new not found");
        return nullptr;
    }
    
    return il2cpp_string_new(str);
}

// ======================== گرفتن GtaMenuControl instance ========================
static void* get_gta_instance() {
    if (g_gtaInstance != nullptr && is_valid_address(g_gtaInstance)) {
        return g_gtaInstance;
    }
    
    typedef void* (*get_instance_t)();
    get_instance_t get_Instance = (get_instance_t)getAbsoluteAddress("libil2cpp.so", "GtaMenuControl.get_Instance");
    if (get_Instance == nullptr) {
        write_connect_log("❌ GtaMenuControl.get_Instance not found");
        return nullptr;
    }
    
    void* instance = get_Instance();
    if (instance != nullptr && is_valid_address(instance)) {
        g_gtaInstance = instance;
        write_connect_log("✅ GtaMenuControl instance: 0x" + std::to_string((uintptr_t)instance));
        return instance;
    }
    
    return nullptr;
}

// ======================== گرفتن IP از ipInput ========================
static std::string get_ip_from_input() {
    void* instance = get_gta_instance();
    if (instance == nullptr) {
        write_connect_log("❌ Cannot get GtaMenuControl instance for IP");
        return "";
    }
    
    void* ipInput = *(void**)((uintptr_t)instance + OFFSET_IP_INPUT);
    if (ipInput == nullptr || !is_valid_address(ipInput)) {
        write_connect_log("❌ ipInput is null or invalid");
        return "";
    }
    
    void** mTextPtr = (void**)((uintptr_t)ipInput + OFFSET_INPUTFIELD_M_TEXT);
    if (mTextPtr == nullptr || !is_valid_address(mTextPtr)) {
        write_connect_log("❌ m_Text pointer invalid");
        return "";
    }
    
    void* monoString = *mTextPtr;
    if (monoString == nullptr) {
        write_connect_log("❌ MonoString is null");
        return "";
    }
    
    return mono_string_to_utf8(monoString);
}

// ======================== تزریق IP به ipInput ========================
static void inject_ip_to_input(const std::string& ip) {
    void* instance = get_gta_instance();
    if (instance == nullptr) {
        write_report("❌ Cannot get instance for IP injection");
        return;
    }
    
    void* ipInput = *(void**)((uintptr_t)instance + OFFSET_IP_INPUT);
    if (ipInput == nullptr || !is_valid_address(ipInput)) {
        write_report("❌ ipInput is null");
        return;
    }
    
    void** mTextPtr = (void**)((uintptr_t)ipInput + OFFSET_INPUTFIELD_M_TEXT);
    if (mTextPtr == nullptr) {
        write_report("❌ m_Text pointer invalid");
        return;
    }
    
    void* monoString = create_mono_string(ip.c_str());
    if (monoString == nullptr) {
        write_report("❌ Failed to create mono string");
        return;
    }
    
    *mTextPtr = monoString;
    write_report("✅ IP injected: " + ip);
    write_result("✅ IP injected: " + ip);
}

// ======================== گرفتن NetworkManager instance ========================
static void* get_network_manager_instance() {
    if (g_networkManagerInstance != nullptr && is_valid_address(g_networkManagerInstance)) {
        return g_networkManagerInstance;
    }
    
    void* gtaInstance = get_gta_instance();
    if (gtaInstance == nullptr) {
        write_connect_log("❌ Cannot get GtaMenuControl instance for NetworkManager");
        return nullptr;
    }
    
    void* nmInstance = *(void**)((uintptr_t)gtaInstance + OFFSET_NETWORK_MANAGER);
    if (nmInstance == nullptr || !is_valid_address(nmInstance)) {
        write_connect_log("❌ NetworkManager instance is null or invalid");
        return nullptr;
    }
    
    g_networkManagerInstance = nmInstance;
    write_connect_log("✅ NetworkManager instance: 0x" + std::to_string((uintptr_t)nmInstance));
    return nmInstance;
}

// ======================== اتصال به سرور ========================
static void connect_to_server() {
    write_connect_log("\n========== CONNECTING TO SERVER ==========");
    write_connect_log("Time: " + get_time());
    write_report("🔘 Connect button pressed");
    write_result("🔘 Connect button pressed");
    
    try {
        // 1. گرفتن IP (از ipInput یا فایل یا پیش‌فرض)
        std::string ip = get_ip_from_input();
        if (ip.empty()) {
            ip = load_ip();
            if (ip.empty()) {
                ip = std::string(DEFAULT_IP) + ":" + std::string(DEFAULT_PORT);
                save_ip(ip);
                write_connect_log("📌 Using default IP: " + ip);
            } else {
                inject_ip_to_input(ip);
                write_connect_log("📌 Loaded IP from file: " + ip);
            }
        } else {
            save_ip(ip);
            write_connect_log("📌 IP from input: " + ip);
        }
        g_targetIP = ip;
        write_connect_log("📡 Target IP: " + ip);
        write_report("📡 Target IP: " + ip);
        
        // 2. گرفتن NetworkManager
        void* nmInstance = get_network_manager_instance();
        if (nmInstance == nullptr || !is_valid_address(nmInstance)) {
            write_connect_log("❌ NetworkManager instance not found!");
            write_report("❌ NetworkManager not found!");
            write_result("❌ NetworkManager not found!");
            return;
        }
        write_connect_log("✅ NetworkManager instance: 0x" + std::to_string((uintptr_t)nmInstance));
        
        // 3. تنظیم networkAddress
        void* nmAddr = (void*)((uintptr_t)nmInstance + OFFSET_NETWORK_ADDR);
        void** nmAddrPtr = (void**)nmAddr;
        if (nmAddrPtr == nullptr || !is_valid_address(nmAddrPtr)) {
            write_connect_log("❌ networkAddress pointer invalid");
            return;
        }
        
        void* monoString = create_mono_string(ip.c_str());
        if (monoString == nullptr) {
            write_connect_log("❌ Failed to create mono string");
            return;
        }
        
        *nmAddrPtr = monoString;
        write_connect_log("✅ networkAddress set to: " + ip);
        write_report("✅ networkAddress set to: " + ip);
        
        // 4. صدا زدن StartClient
        void* startClientAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0x1AACCB4"));
        if (startClientAddr == nullptr || !is_valid_address(startClientAddr)) {
            write_connect_log("❌ StartClient address not found!");
            write_report("❌ StartClient not found!");
            write_result("❌ StartClient not found!");
            return;
        }
        write_connect_log("✅ StartClient address: 0x" + std::to_string((uintptr_t)startClientAddr));
        
        typedef void (*start_client_t)(void*);
        start_client_t StartClient = (start_client_t)startClientAddr;
        
        write_connect_log("🔄 Calling StartClient()...");
        write_report("🔄 Calling StartClient()...");
        StartClient(nmInstance);
        
        write_connect_log("✅ StartClient() called successfully!");
        write_report("✅ StartClient() called successfully!");
        write_result("✅ Connecting...");
        
        // 5. لاگ موفقیت
        write_connect_log("✅ Connection initiated. Check game for connecting status.");
        
    } catch (const std::exception& e) {
        write_connect_log("❌ Exception: " + std::string(e.what()));
        write_report("❌ Exception: " + std::string(e.what()));
        write_crash("⚠️ Exception: " + std::string(e.what()));
    } catch (...) {
        write_connect_log("❌ Unknown exception!");
        write_report("❌ Unknown exception!");
        write_crash("⚠️ Unknown exception!");
    }
    write_connect_log("========== CONNECT FINISHED ==========\n");
}

// ======================== هوک GtaMenuControl.Start (برای گرفتن instance) ========================
void (*orig_GtaMenuStart)(void *instance);
void hook_GtaMenuStart(void *instance) {
    if (instance != nullptr && is_valid_address(instance)) {
        g_gtaInstance = instance;
        g_gtaReady = true;
        write_report("✅ Hook captured GtaMenuControl instance");
        write_connect_log("✅ GtaMenuControl instance captured: 0x" + std::to_string((uintptr_t)instance));
    }
    if (orig_GtaMenuStart) {
        orig_GtaMenuStart(instance);
    }
}

// ======================== منو ========================
jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    jobjectArray ret;
    
    const char *features[] = {
        OBFUSCATE("Category_🌐 Network"),
        OBFUSCATE("InputText_Enter IP"),
        OBFUSCATE("Button_Inject IP"),
        OBFUSCATE("Button_Connect (StartClient)"),
        OBFUSCATE("RichTextView_📁 Logs: /sdcard/Download/lac/connect_log.txt"),
    };
    
    int total = sizeof features / sizeof features[0];
    ret = (jobjectArray)env->NewObjectArray(total, env->FindClass(OBFUSCATE("java/lang/String")), env->NewStringUTF(""));
    
    if (ret == nullptr) {
        write_report("❌ Failed to create jobjectArray");
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

    const char* textStr = nullptr;
    if (text != nullptr) {
        textStr = env->GetStringUTFChars(text, nullptr);
    }
    
    switch (featNum) {
        case 0: {
            if (textStr != nullptr && strlen(textStr) > 0) {
                g_targetIP = textStr;
                save_ip(g_targetIP);
                inject_ip_to_input(g_targetIP);
                write_report("📝 IP entered: " + g_targetIP);
                write_result("📝 IP entered: " + g_targetIP);
            }
            break;
        }
        
        case 1: {
            write_report("🔘 Inject IP pressed");
            if (g_targetIP.empty()) {
                g_targetIP = load_ip();
                if (g_targetIP.empty()) {
                    g_targetIP = std::string(DEFAULT_IP) + ":" + std::string(DEFAULT_PORT);
                }
            }
            inject_ip_to_input(g_targetIP);
            write_result("✅ IP injected: " + g_targetIP);
            break;
        }
        
        case 2: {
            connect_to_server();
            break;
        }
        
        default: {
            write_report("Unknown featNum: " + std::to_string(featNum));
            break;
        }
    }
    
    if (textStr != nullptr) {
        env->ReleaseStringUTFChars(text, textStr);
    }
}

// ======================== ترد اصلی ========================
void hack_thread() {
    int waitCount = 0;

    write_report("⏳ Waiting for libil2cpp.so to load...");

    while (!isLibraryLoaded(targetLibName) && waitCount < 30) {
        sleep(1);
        waitCount++;
    }

    if (waitCount >= 30) {
        write_report("❌ Timeout waiting for libil2cpp.so");
        return;
    }

    write_report("✅ libil2cpp.so loaded successfully");

#if defined(__aarch64__)
    // ====== نصب هوک روی Start (برای گرفتن instance) ======
    void* startAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0xF571A4"));
    if (startAddr != nullptr && is_valid_address(startAddr)) {
        int res = DobbyHook(startAddr, (dobby_dummy_func_t)hook_GtaMenuStart, (dobby_dummy_func_t*)&orig_GtaMenuStart);
        if (res == 0) {
            write_report("✅ GtaMenuControl.Start() hooked at 0x" + std::to_string((uintptr_t)startAddr));
        } else {
            write_report("❌ Failed to hook GtaMenuControl.Start()! error: " + std::to_string(res));
        }
    } else {
        write_report("❌ GtaMenuControl.Start() address not found");
    }
#endif

    // ====== گرفتن instance ======
    for (int i = 0; i < 20 && g_gtaInstance == nullptr; i++) {
        sleep(1);
    }
    
    if (g_gtaInstance != nullptr) {
        write_report("✅ Got GtaMenuControl instance after wait");
        write_connect_log("✅ Got GtaMenuControl instance after wait");
    } else {
        g_gtaInstance = get_gta_instance();
        if (g_gtaInstance != nullptr) {
            write_report("✅ Got GtaMenuControl instance via get_Instance");
            write_connect_log("✅ Got GtaMenuControl instance via get_Instance");
        }
    }

    // ====== گرفتن NetworkManager ======
    if (g_gtaInstance != nullptr) {
        g_networkManagerInstance = get_network_manager_instance();
        if (g_networkManagerInstance != nullptr) {
            write_report("✅ Got NetworkManager instance");
            write_connect_log("✅ Got NetworkManager instance");
        }
    }

    // ====== IP پیش‌فرض ======
    g_targetIP = load_ip();
    if (g_targetIP.empty()) {
        g_targetIP = std::string(DEFAULT_IP) + ":" + std::string(DEFAULT_PORT);
        save_ip(g_targetIP);
        write_report("📌 Default IP set: " + g_targetIP);
    } else {
        write_report("📌 IP loaded from file: " + g_targetIP);
    }

    // ====== تزریق خودکار IP (اگه instance آماده باشه) ======
    if (g_gtaInstance != nullptr) {
        inject_ip_to_input(g_targetIP);
    }

    write_report("✅ hack_thread finished successfully");
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
    std::thread(hack_thread).detach();
}