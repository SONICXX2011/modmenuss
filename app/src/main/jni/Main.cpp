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

// ======================== آفست‌های دقیق از دامپ و IDA ========================
#define OFFSET_GTA_GET_INSTANCE     0xF570C4      // GtaMenuControl.get_Instance()
#define OFFSET_GTA_START            0xF571A4      // GtaMenuControl.Start()
#define OFFSET_IP_INPUT             0xC0          // GtaMenuControl.ipInput
#define OFFSET_INPUTFIELD_M_TEXT    0x180         // InputField.m_Text
#define OFFSET_NETWORK_MANAGER      0x258         // GtaMenuControl._networkManager
#define OFFSET_NETWORK_ADDR         0x50          // NetworkManager.networkAddress
#define OFFSET_START_CLIENT         0x1AACCB4     // NetworkManager.StartClient()
#define OFFSET_LOCAL_JOIN           0xF5B844      // GtaMenuControl.LocalJoin()
#define OFFSET_LOCAL_HOST           0xF5B7E0      // GtaMenuControl.LocalHost()
#define OFFSET_IL2CPP_STRING_NEW    0xE40BF0      // il2cpp_string_new()

// ======================== IP پیش‌فرض (از stringliteral.json) ========================
#define DEFAULT_IP "192.168.43.1"
#define DEFAULT_PORT "7777"

// ======================== مسیرهای لاگ ========================
static std::string g_basePath = "/storage/emulated/0/Download/lac/";
static std::string g_debugLog = g_basePath + "mod_debug.txt";
static std::string g_ipResultLog = g_basePath + "ip_result.txt";
static std::string g_crashLog = g_basePath + "crash_log.txt";
static std::string g_reportLog = g_basePath + "full_report.txt";
static std::string g_connectLog = g_basePath + "connect_log.txt";
static std::string g_lastIPFile = g_basePath + "last_ip.txt";

// ======================== متغیرهای سراسری ========================
static bool g_crashHandlerInstalled = false;
static bool g_gtaReady = false;
static void* g_gtaInstance = nullptr;
static std::string g_targetIP = "";

// ======================== توابع کمکی برای لاگ ========================
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

// ======================== توابع امنیت حافظه ========================
static bool is_valid_address(void* addr) {
    if (addr == nullptr) return false;
    uintptr_t ptr = (uintptr_t)addr;
    if (ptr < 0x1000) return false;
    if (ptr > 0x7FFFFFFFFFFFFF) return false;
    auto map = KittyMemory::getAddressMap(addr);
    return map.readable;
}

static bool is_valid_pointer(void** ptr) {
    if (ptr == nullptr) return false;
    return is_valid_address((void*)ptr);
}

// ======================== توابع IL2CPP ========================
static void* create_mono_string(const char* str) {
    if (str == nullptr) return nullptr;
    typedef void* (*il2cpp_string_new_t)(const char*);
    il2cpp_string_new_t il2cpp_string_new = 
        (il2cpp_string_new_t)getAbsoluteAddress("libil2cpp.so", OBFUSCATE("0xE40BF0"));
    if (il2cpp_string_new == nullptr) {
        write_connect_log("❌ il2cpp_string_new not found");
        return nullptr;
    }
    return il2cpp_string_new(str);
}

static std::string mono_string_to_utf8(void* monoString) {
    if (monoString == nullptr) return "";
    if (!is_valid_address(monoString)) return "";
    
    typedef int32_t (*il2cpp_string_length_t)(void* str);
    il2cpp_string_length_t il2cpp_string_length = 
        (il2cpp_string_length_t)getAbsoluteAddress("libil2cpp.so", OBFUSCATE("il2cpp_string_length"));
    if (il2cpp_string_length == nullptr) {
        write_connect_log("❌ il2cpp_string_length not found");
        return "";
    }
    
    int length = il2cpp_string_length(monoString);
    if (length <= 0 || length > 65536) return "";
    
    typedef uint16_t* (*il2cpp_string_chars_t)(void* str);
    il2cpp_string_chars_t il2cpp_string_chars = 
        (il2cpp_string_chars_t)getAbsoluteAddress("libil2cpp.so", OBFUSCATE("il2cpp_string_chars"));
    if (il2cpp_string_chars == nullptr) {
        write_connect_log("❌ il2cpp_string_chars not found");
        return "";
    }
    
    uint16_t* chars = il2cpp_string_chars(monoString);
    if (chars == nullptr || !is_valid_address(chars)) return "";
    
    std::string result;
    result.reserve(length);
    for (int i = 0; i < length; i++) {
        result += (char)(chars[i] & 0xFF);
    }
    return result;
}

// ======================== گرفتن GtaMenuControl instance ========================
static void* get_gta_instance() {
    if (g_gtaInstance != nullptr && is_valid_address(g_gtaInstance)) {
        return g_gtaInstance;
    }
    
    typedef void* (*get_instance_t)();
    get_instance_t get_Instance = (get_instance_t)getAbsoluteAddress("libil2cpp.so", OBFUSCATE("0xF570C4"));
    if (get_Instance == nullptr) {
        write_connect_log("❌ GtaMenuControl.get_Instance not found");
        return nullptr;
    }
    
    void* instance = get_Instance();
    if (instance != nullptr && is_valid_address(instance)) {
        g_gtaInstance = instance;
        g_gtaReady = true;
        write_connect_log("✅ GtaMenuControl instance: 0x" + std::to_string((uintptr_t)instance));
        return instance;
    }
    return nullptr;
}

// ======================== گرفتن IP از ipInput ========================
static std::string get_ip_from_input() {
    void* instance = get_gta_instance();
    if (instance == nullptr) {
        write_connect_log("❌ Cannot get GtaMenuControl instance");
        return "";
    }
    
    void* ipInput = *(void**)((uintptr_t)instance + OFFSET_IP_INPUT);
    if (ipInput == nullptr || !is_valid_address(ipInput)) {
        write_connect_log("❌ ipInput is null or invalid");
        return "";
    }
    
    void** mTextPtr = (void**)((uintptr_t)ipInput + OFFSET_INPUTFIELD_M_TEXT);
    if (mTextPtr == nullptr || !is_valid_pointer(mTextPtr)) {
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
static bool inject_ip_to_input(const std::string& ip) {
    void* instance = get_gta_instance();
    if (instance == nullptr) {
        write_report("❌ Cannot get instance for IP injection");
        return false;
    }
    
    void* ipInput = *(void**)((uintptr_t)instance + OFFSET_IP_INPUT);
    if (ipInput == nullptr || !is_valid_address(ipInput)) {
        write_report("❌ ipInput is null");
        return false;
    }
    
    void** mTextPtr = (void**)((uintptr_t)ipInput + OFFSET_INPUTFIELD_M_TEXT);
    if (mTextPtr == nullptr || !is_valid_pointer(mTextPtr)) {
        write_report("❌ m_Text pointer invalid");
        return false;
    }
    
    void* monoString = create_mono_string(ip.c_str());
    if (monoString == nullptr) {
        write_report("❌ Failed to create mono string");
        return false;
    }
    
    *mTextPtr = monoString;
    write_report("✅ IP injected to input: " + ip);
    return true;
}

// ======================== گرفتن NetworkManager instance ========================
static void* get_network_manager() {
    void* instance = get_gta_instance();
    if (instance == nullptr) {
        write_connect_log("❌ Cannot get GtaMenuControl instance");
        return nullptr;
    }
    
    void* nm = *(void**)((uintptr_t)instance + OFFSET_NETWORK_MANAGER);
    if (nm == nullptr || !is_valid_address(nm)) {
        write_connect_log("❌ NetworkManager is null or invalid");
        return nullptr;
    }
    return nm;
}

// ======================== ====== ۶ روش اتصال ====== ========================

// ----- روش ۱: تغییر networkAddress و صدا زدن StartClient -----
static void Connect_Method1() {
    write_connect_log("\n========== METHOD 1: Set networkAddress + StartClient ==========");
    write_connect_log("Time: " + get_time());
    write_report("🔘 Method 1 pressed");
    write_result("🔘 Method 1");
    
    try {
        std::string ip = get_ip_from_input();
        if (ip.empty()) {
            ip = load_ip();
            if (ip.empty()) {
                ip = std::string(DEFAULT_IP) + ":" + std::string(DEFAULT_PORT);
                save_ip(ip);
                write_connect_log("📌 Using default IP: " + ip);
            }
        }
        g_targetIP = ip;
        save_ip(ip);
        write_connect_log("📡 Target IP: " + ip);
        
        void* nm = get_network_manager();
        if (nm == nullptr) {
            write_connect_log("❌ NetworkManager not found");
            write_result("❌ Method 1 failed: NetworkManager not found");
            return;
        }
        write_connect_log("✅ NetworkManager: 0x" + std::to_string((uintptr_t)nm));
        
        void** addrPtr = (void**)((uintptr_t)nm + OFFSET_NETWORK_ADDR);
        if (!is_valid_pointer(addrPtr)) {
            write_connect_log("❌ networkAddress pointer invalid");
            write_result("❌ Method 1 failed: invalid address");
            return;
        }
        
        void* monoStr = create_mono_string(ip.c_str());
        if (monoStr == nullptr) {
            write_connect_log("❌ Failed to create mono string");
            write_result("❌ Method 1 failed: mono string");
            return;
        }
        *addrPtr = monoStr;
        write_connect_log("✅ networkAddress set to: " + ip);
        
        void* startClientAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0x1AACCB4"));
        if (startClientAddr == nullptr || !is_valid_address(startClientAddr)) {
            write_connect_log("❌ StartClient address not found");
            write_result("❌ Method 1 failed: StartClient not found");
            return;
        }
        
        typedef void (*start_client_t)(void*);
        start_client_t StartClient = (start_client_t)startClientAddr;
        write_connect_log("🔄 Calling StartClient()...");
        StartClient(nm);
        write_connect_log("✅ StartClient() called successfully!");
        write_result("✅ Method 1: StartClient called");
        
    } catch (const std::exception& e) {
        write_connect_log("❌ Exception: " + std::string(e.what()));
        write_crash("⚠️ Exception: " + std::string(e.what()));
        write_result("❌ Method 1 crashed");
    } catch (...) {
        write_connect_log("❌ Unknown exception!");
        write_crash("⚠️ Unknown exception!");
        write_result("❌ Method 1 crashed");
    }
    write_connect_log("========== METHOD 1 FINISHED ==========\n");
}

// ----- روش ۲: صدا زدن LocalJoin (خودش همه کارو میکنه) -----
static void Connect_Method2() {
    write_connect_log("\n========== METHOD 2: LocalJoin only ==========");
    write_connect_log("Time: " + get_time());
    write_report("🔘 Method 2 pressed");
    write_result("🔘 Method 2");
    
    try {
        void* instance = get_gta_instance();
        if (instance == nullptr) {
            write_connect_log("❌ GtaMenuControl instance not found");
            write_result("❌ Method 2 failed: instance not found");
            return;
        }
        write_connect_log("✅ GtaMenuControl: 0x" + std::to_string((uintptr_t)instance));
        
        std::string ip = get_ip_from_input();
        if (!ip.empty()) {
            g_targetIP = ip;
            save_ip(ip);
            write_connect_log("📡 IP from input: " + ip);
        } else {
            ip = load_ip();
            if (ip.empty()) {
                ip = std::string(DEFAULT_IP) + ":" + std::string(DEFAULT_PORT);
                save_ip(ip);
                write_connect_log("📌 Using default IP: " + ip);
            } else {
                inject_ip_to_input(ip);
                write_connect_log("📌 Loaded IP from file: " + ip);
            }
            g_targetIP = ip;
        }
        
        void* localJoinAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0xF5B844"));
        if (localJoinAddr == nullptr || !is_valid_address(localJoinAddr)) {
            write_connect_log("❌ LocalJoin address not found");
            write_result("❌ Method 2 failed: LocalJoin not found");
            return;
        }
        
        typedef void (*local_join_t)(void*);
        local_join_t LocalJoin = (local_join_t)localJoinAddr;
        write_connect_log("🔄 Calling LocalJoin()...");
        LocalJoin(instance);
        write_connect_log("✅ LocalJoin() called successfully!");
        write_result("✅ Method 2: LocalJoin called");
        
    } catch (const std::exception& e) {
        write_connect_log("❌ Exception: " + std::string(e.what()));
        write_crash("⚠️ Exception: " + std::string(e.what()));
        write_result("❌ Method 2 crashed");
    } catch (...) {
        write_connect_log("❌ Unknown exception!");
        write_crash("⚠️ Unknown exception!");
        write_result("❌ Method 2 crashed");
    }
    write_connect_log("========== METHOD 2 FINISHED ==========\n");
}

// ----- روش ۳: تنظیم networkAddress + LocalJoin -----
static void Connect_Method3() {
    write_connect_log("\n========== METHOD 3: Set networkAddress + LocalJoin ==========");
    write_connect_log("Time: " + get_time());
    write_report("🔘 Method 3 pressed");
    write_result("🔘 Method 3");
    
    try {
        std::string ip = get_ip_from_input();
        if (ip.empty()) {
            ip = load_ip();
            if (ip.empty()) {
                ip = std::string(DEFAULT_IP) + ":" + std::string(DEFAULT_PORT);
                save_ip(ip);
            }
        }
        g_targetIP = ip;
        save_ip(ip);
        write_connect_log("📡 Target IP: " + ip);
        
        void* instance = get_gta_instance();
        if (instance == nullptr) {
            write_connect_log("❌ GtaMenuControl instance not found");
            write_result("❌ Method 3 failed: instance not found");
            return;
        }
        
        void* nm = get_network_manager();
        if (nm == nullptr) {
            write_connect_log("❌ NetworkManager not found");
            write_result("❌ Method 3 failed: NetworkManager not found");
            return;
        }
        
        void** addrPtr = (void**)((uintptr_t)nm + OFFSET_NETWORK_ADDR);
        if (is_valid_pointer(addrPtr)) {
            void* monoStr = create_mono_string(ip.c_str());
            if (monoStr != nullptr) {
                *addrPtr = monoStr;
                write_connect_log("✅ networkAddress set to: " + ip);
            }
        }
        
        void* localJoinAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0xF5B844"));
        if (localJoinAddr == nullptr || !is_valid_address(localJoinAddr)) {
            write_connect_log("❌ LocalJoin address not found");
            write_result("❌ Method 3 failed: LocalJoin not found");
            return;
        }
        
        typedef void (*local_join_t)(void*);
        local_join_t LocalJoin = (local_join_t)localJoinAddr;
        write_connect_log("🔄 Calling LocalJoin()...");
        LocalJoin(instance);
        write_connect_log("✅ LocalJoin() called successfully!");
        write_result("✅ Method 3: networkAddress + LocalJoin");
        
    } catch (const std::exception& e) {
        write_connect_log("❌ Exception: " + std::string(e.what()));
        write_crash("⚠️ Exception: " + std::string(e.what()));
        write_result("❌ Method 3 crashed");
    } catch (...) {
        write_connect_log("❌ Unknown exception!");
        write_crash("⚠️ Unknown exception!");
        write_result("❌ Method 3 crashed");
    }
    write_connect_log("========== METHOD 3 FINISHED ==========\n");
}

// ----- روش ۴: LocalHost (برای هاست شدن) -----
static void Connect_Method4() {
    write_connect_log("\n========== METHOD 4: LocalHost (Host) ==========");
    write_connect_log("Time: " + get_time());
    write_report("🔘 Method 4 pressed");
    write_result("🔘 Method 4");
    
    try {
        void* instance = get_gta_instance();
        if (instance == nullptr) {
            write_connect_log("❌ GtaMenuControl instance not found");
            write_result("❌ Method 4 failed: instance not found");
            return;
        }
        
        void* localHostAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0xF5B7E0"));
        if (localHostAddr == nullptr || !is_valid_address(localHostAddr)) {
            write_connect_log("❌ LocalHost address not found");
            write_result("❌ Method 4 failed: LocalHost not found");
            return;
        }
        
        typedef void (*local_host_t)(void*);
        local_host_t LocalHost = (local_host_t)localHostAddr;
        write_connect_log("🔄 Calling LocalHost()...");
        LocalHost(instance);
        write_connect_log("✅ LocalHost() called successfully!");
        write_result("✅ Method 4: LocalHost called");
        
    } catch (const std::exception& e) {
        write_connect_log("❌ Exception: " + std::string(e.what()));
        write_crash("⚠️ Exception: " + std::string(e.what()));
        write_result("❌ Method 4 crashed");
    } catch (...) {
        write_connect_log("❌ Unknown exception!");
        write_crash("⚠️ Unknown exception!");
        write_result("❌ Method 4 crashed");
    }
    write_connect_log("========== METHOD 4 FINISHED ==========\n");
}

// ----- روش ۵: تزریق IP به UI + StartClient -----
static void Connect_Method5() {
    write_connect_log("\n========== METHOD 5: Inject IP + StartClient ==========");
    write_connect_log("Time: " + get_time());
    write_report("🔘 Method 5 pressed");
    write_result("🔘 Method 5");
    
    try {
        std::string ip = load_ip();
        if (ip.empty()) {
            ip = std::string(DEFAULT_IP) + ":" + std::string(DEFAULT_PORT);
            save_ip(ip);
        }
        g_targetIP = ip;
        write_connect_log("📡 Target IP: " + ip);
        
        if (!inject_ip_to_input(ip)) {
            write_connect_log("❌ Failed to inject IP");
            write_result("❌ Method 5 failed: inject IP");
            return;
        }
        
        void* nm = get_network_manager();
        if (nm == nullptr) {
            write_connect_log("❌ NetworkManager not found");
            write_result("❌ Method 5 failed: NetworkManager not found");
            return;
        }
        
        void** addrPtr = (void**)((uintptr_t)nm + OFFSET_NETWORK_ADDR);
        if (is_valid_pointer(addrPtr)) {
            void* monoStr = create_mono_string(ip.c_str());
            if (monoStr != nullptr) {
                *addrPtr = monoStr;
                write_connect_log("✅ networkAddress set to: " + ip);
            }
        }
        
        void* startClientAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0x1AACCB4"));
        if (startClientAddr == nullptr || !is_valid_address(startClientAddr)) {
            write_connect_log("❌ StartClient address not found");
            write_result("❌ Method 5 failed: StartClient not found");
            return;
        }
        
        typedef void (*start_client_t)(void*);
        start_client_t StartClient = (start_client_t)startClientAddr;
        write_connect_log("🔄 Calling StartClient()...");
        StartClient(nm);
        write_connect_log("✅ StartClient() called successfully!");
        write_result("✅ Method 5: Inject IP + StartClient");
        
    } catch (const std::exception& e) {
        write_connect_log("❌ Exception: " + std::string(e.what()));
        write_crash("⚠️ Exception: " + std::string(e.what()));
        write_result("❌ Method 5 crashed");
    } catch (...) {
        write_connect_log("❌ Unknown exception!");
        write_crash("⚠️ Unknown exception!");
        write_result("❌ Method 5 crashed");
    }
    write_connect_log("========== METHOD 5 FINISHED ==========\n");
}

// ----- روش ۶: کامل (Inject IP + networkAddress + LocalJoin) -----
static void Connect_Method6() {
    write_connect_log("\n========== METHOD 6: Full Connect (Inject + networkAddress + LocalJoin) ==========");
    write_connect_log("Time: " + get_time());
    write_report("🔘 Method 6 pressed");
    write_result("🔘 Method 6");
    
    try {
        std::string ip = get_ip_from_input();
        if (ip.empty()) {
            ip = load_ip();
            if (ip.empty()) {
                ip = std::string(DEFAULT_IP) + ":" + std::string(DEFAULT_PORT);
                save_ip(ip);
            }
        }
        g_targetIP = ip;
        save_ip(ip);
        write_connect_log("📡 Target IP: " + ip);
        
        // 1. تزریق IP به UI
        if (!inject_ip_to_input(ip)) {
            write_connect_log("⚠️ IP injection to UI failed, continuing...");
        }
        
        void* instance = get_gta_instance();
        if (instance == nullptr) {
            write_connect_log("❌ GtaMenuControl instance not found");
            write_result("❌ Method 6 failed: instance not found");
            return;
        }
        
        void* nm = get_network_manager();
        if (nm == nullptr) {
            write_connect_log("❌ NetworkManager not found");
            write_result("❌ Method 6 failed: NetworkManager not found");
            return;
        }
        
        // 2. تنظیم networkAddress
        void** addrPtr = (void**)((uintptr_t)nm + OFFSET_NETWORK_ADDR);
        if (is_valid_pointer(addrPtr)) {
            void* monoStr = create_mono_string(ip.c_str());
            if (monoStr != nullptr) {
                *addrPtr = monoStr;
                write_connect_log("✅ networkAddress set to: " + ip);
            }
        }
        
        // 3. صدا زدن LocalJoin
        void* localJoinAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0xF5B844"));
        if (localJoinAddr == nullptr || !is_valid_address(localJoinAddr)) {
            write_connect_log("❌ LocalJoin address not found");
            write_result("❌ Method 6 failed: LocalJoin not found");
            return;
        }
        
        typedef void (*local_join_t)(void*);
        local_join_t LocalJoin = (local_join_t)localJoinAddr;
        write_connect_log("🔄 Calling LocalJoin()...");
        LocalJoin(instance);
        write_connect_log("✅ LocalJoin() called successfully!");
        write_result("✅ Method 6: Full Connect");
        
    } catch (const std::exception& e) {
        write_connect_log("❌ Exception: " + std::string(e.what()));
        write_crash("⚠️ Exception: " + std::string(e.what()));
        write_result("❌ Method 6 crashed");
    } catch (...) {
        write_connect_log("❌ Unknown exception!");
        write_crash("⚠️ Unknown exception!");
        write_result("❌ Method 6 crashed");
    }
    write_connect_log("========== METHOD 6 FINISHED ==========\n");
}

// ======================== هوک GtaMenuControl.Start ========================
void (*orig_GtaMenuStart)(void *instance);
void hook_GtaMenuStart(void *instance) {
    if (instance != nullptr && is_valid_address(instance)) {
        g_gtaInstance = instance;
        g_gtaReady = true;
        write_report("✅ Hook captured GtaMenuControl instance: 0x" + std::to_string((uintptr_t)instance));
        write_connect_log("✅ GtaMenuControl instance captured via hook");
        
        std::string ip = load_ip();
        if (!ip.empty()) {
            inject_ip_to_input(ip);
            write_report("📌 Auto-injected saved IP: " + ip);
        }
    }
    if (orig_GtaMenuStart) {
        orig_GtaMenuStart(instance);
    }
}

// ======================== منو ========================
jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    jobjectArray ret;
    
    const char *features[] = {
        OBFUSCATE("Category_🌐 Network Settings"),
        OBFUSCATE("InputText_Enter IP:Port"),
        OBFUSCATE("Button_💉 Inject IP to UI"),
        OBFUSCATE("Button_📋 Show Current IP"),
        OBFUSCATE("Category_🔗 Connect Methods"),
        OBFUSCATE("Button_Method 1: Set IP + StartClient"),
        OBFUSCATE("Button_Method 2: LocalJoin Only"),
        OBFUSCATE("Button_Method 3: Set IP + LocalJoin"),
        OBFUSCATE("Button_Method 4: LocalHost (Host)"),
        OBFUSCATE("Button_Method 5: Inject IP + StartClient"),
        OBFUSCATE("Button_Method 6: FULL CONNECT"),
        OBFUSCATE("RichTextView_📁 Logs: /sdcard/Download/lac/"),
        OBFUSCATE("RichTextView_📌 Check connect_log.txt for details"),
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
    
    try {
        switch (featNum) {
            case 0: { // InputText: Enter IP
                if (textStr != nullptr && strlen(textStr) > 0) {
                    g_targetIP = textStr;
                    save_ip(g_targetIP);
                    write_report("📝 IP entered: " + g_targetIP);
                    write_result("📝 IP entered: " + g_targetIP);
                }
                break;
            }
            
            case 1: { // Inject IP to UI
                write_report("🔘 Inject IP pressed");
                if (g_targetIP.empty()) {
                    g_targetIP = load_ip();
                    if (g_targetIP.empty()) {
                        g_targetIP = std::string(DEFAULT_IP) + ":" + std::string(DEFAULT_PORT);
                    }
                }
                if (inject_ip_to_input(g_targetIP)) {
                    write_result("✅ IP injected: " + g_targetIP);
                } else {
                    write_result("❌ IP injection failed");
                }
                break;
            }
            
            case 2: { // Show Current IP
                std::string ip = load_ip();
                if (!ip.empty()) {
                    write_report("📌 Current IP: " + ip);
                    write_result("📌 Current IP: " + ip);
                } else {
                    write_report("❌ No saved IP!");
                    write_result("❌ No saved IP!");
                }
                break;
            }
            
            case 3: { // Method 1
                Connect_Method1();
                break;
            }
            
            case 4: { // Method 2
                Connect_Method2();
                break;
            }
            
            case 5: { // Method 3
                Connect_Method3();
                break;
            }
            
            case 6: { // Method 4
                Connect_Method4();
                break;
            }
            
            case 7: { // Method 5
                Connect_Method5();
                break;
            }
            
            case 8: { // Method 6
                Connect_Method6();
                break;
            }
            
            default:
                write_report("Unknown featNum: " + std::to_string(featNum));
                break;
        }
    } catch (const std::exception& e) {
        write_report("❌ Exception in Changes: " + std::string(e.what()));
        write_crash("⚠️ Changes exception: " + std::string(e.what()));
    } catch (...) {
        write_report("❌ Unknown exception in Changes");
        write_crash("⚠️ Unknown exception in Changes");
    }
    
    if (textStr != nullptr) {
        env->ReleaseStringUTFChars(text, textStr);
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
    // ====== نصب هوک روی GtaMenuControl.Start ======
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

    // ====== بارگذاری IP ======
    g_targetIP = load_ip();
    if (g_targetIP.empty()) {
        g_targetIP = std::string(DEFAULT_IP) + ":" + std::string(DEFAULT_PORT);
        save_ip(g_targetIP);
        write_report("📌 Default IP set: " + g_targetIP);
    } else {
        write_report("📌 IP loaded from file: " + g_targetIP);
    }

    // ====== تلاش برای گرفتن instance با get_Instance ======
    void* instance = get_gta_instance();
    if (instance != nullptr) {
        write_report("✅ Got instance via get_Instance");
        inject_ip_to_input(g_targetIP);
    } else {
        write_report("⚠️ get_Instance returned null, waiting for hook...");
        for (int i = 0; i < 15 && g_gtaInstance == nullptr; i++) {
            sleep(1);
        }
        if (g_gtaInstance != nullptr) {
            write_report("✅ Got instance via hook");
            inject_ip_to_input(g_targetIP);
        } else {
            write_report("❌ Failed to get GtaMenuControl instance");
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