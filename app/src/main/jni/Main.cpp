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
#include "KittyMemory/KittyMemory.hpp"

#define targetLibName OBFUSCATE("libil2cpp.so")

// ======================== آفست‌های دقیق ========================
#define OFFSET_GTA_GET_INSTANCE     0xF570C4      // GtaMenuControl.get_Instance()
#define OFFSET_GTA_START            0xF571A4      // GtaMenuControl.Start()
#define OFFSET_IP_INPUT             0xC0          // GtaMenuControl.ipInput
#define OFFSET_INPUTFIELD_M_TEXT    0x180         // InputField.m_Text
#define OFFSET_NETWORK_MANAGER      0x258         // GtaMenuControl._networkManager
#define OFFSET_NETWORK_ADDR         0x50          // NetworkManager.networkAddress
#define OFFSET_NETWORK_TRANSPORT    0x48          // NetworkManager.transport
#define OFFSET_KCP_PORT             0x88          // KcpTransport.port
#define OFFSET_IL2CPP_STRING_NEW    0xE40BF0      // il2cpp_string_new()

// ======================== IP و پورت پیش‌فرض ========================
#define DEFAULT_IP "5.57.37.224"
#define DEFAULT_PORT 9876

// ======================== مسیرها ========================
static std::string g_basePath = "/storage/emulated/0/Download/lac/";
static std::string g_debugLog = g_basePath + "mod_debug.txt";
static std::string g_ipResultLog = g_basePath + "ip_result.txt";
static std::string g_crashLog = g_basePath + "crash_log.txt";
static std::string g_reportLog = g_basePath + "full_report.txt";
static std::string g_connectLog = g_basePath + "connect_log.txt";
static std::string g_lastIPFile = g_basePath + "last_ip.txt";

static bool g_crashHandlerInstalled = false;
static bool g_gtaReady = false;
static void* g_gtaInstance = nullptr;
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

// ======================== گرفتن NetworkManager ========================
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
    write_connect_log("✅ NetworkManager: 0x" + std::to_string((uintptr_t)nm));
    return nm;
}

// ======================== گرفتن Transport ========================
static void* get_transport() {
    void* nm = get_network_manager();
    if (nm == nullptr) return nullptr;
    void* transport = *(void**)((uintptr_t)nm + OFFSET_NETWORK_TRANSPORT);
    if (transport == nullptr || !is_valid_address(transport)) {
        write_connect_log("❌ Transport is null or invalid");
        return nullptr;
    }
    write_connect_log("✅ Transport: 0x" + std::to_string((uintptr_t)transport));
    return transport;
}

// ======================== ====== تابع اصلی: تنظیم IP و Port ====== ========================
static void SetIPAndPort(const std::string& ip, uint16_t port) {
    write_connect_log("\n========== SET IP & PORT ==========");
    write_connect_log("Time: " + get_time());
    write_report("🔘 Set IP & Port pressed");
    write_result("🔘 Set IP & Port");
    
    try {
        // 1. گرفتن NetworkManager
        void* nm = get_network_manager();
        if (nm == nullptr) {
            write_connect_log("❌ NetworkManager not found");
            write_result("❌ Failed: NetworkManager not found");
            return;
        }
        
        // ====== 2. تنظیم networkAddress (آفست 0x50) ======
        void** addrPtr = (void**)((uintptr_t)nm + OFFSET_NETWORK_ADDR);
        if (!is_valid_pointer(addrPtr)) {
            write_connect_log("❌ networkAddress pointer invalid");
            write_result("❌ Failed: invalid address");
            return;
        }
        void* monoStr = create_mono_string(ip.c_str());
        if (monoStr == nullptr) {
            write_connect_log("❌ Failed to create mono string");
            write_result("❌ Failed: mono string");
            return;
        }
        *addrPtr = monoStr;
        write_connect_log("✅ networkAddress set to: " + ip);
        
        // ====== 3. گرفتن transport (آفست 0x48) ======
        void* transport = get_transport();
        if (transport == nullptr) {
            write_connect_log("❌ Transport not found");
            write_result("❌ Failed: Transport not found");
            return;
        }
        
        // ====== 4. تنظیم port (آفست 0x88) ======
        uint16_t* portPtr = (uint16_t*)((uintptr_t)transport + OFFSET_KCP_PORT);
        if (!is_valid_address((void*)portPtr)) {
            write_connect_log("❌ Port pointer invalid");
            write_result("❌ Failed: Port invalid");
            return;
        }
        *portPtr = port;
        write_connect_log("✅ Port set to: " + std::to_string(port));
        
        // ====== 5. تزریق IP به UI (فقط برای نمایش) ======
        void* instance = get_gta_instance();
        if (instance) {
            void* ipInput = *(void**)((uintptr_t)instance + OFFSET_IP_INPUT);
            if (ipInput && is_valid_address(ipInput)) {
                void** mTextPtr = (void**)((uintptr_t)ipInput + OFFSET_INPUTFIELD_M_TEXT);
                if (mTextPtr && is_valid_pointer(mTextPtr)) {
                    std::string ipPort = ip + ":" + std::to_string(port);
                    void* monoIpPort = create_mono_string(ipPort.c_str());
                    if (monoIpPort) {
                        *mTextPtr = monoIpPort;
                        write_connect_log("✅ IP injected to UI: " + ipPort);
                    }
                }
            }
        }
        
        write_connect_log("✅ IP and Port set successfully!");
        write_result("✅ Set IP: " + ip + " | Port: " + std::to_string(port));
        write_connect_log("========== DONE ==========\n");
        
    } catch (const std::exception& e) {
        write_connect_log("❌ Exception: " + std::string(e.what()));
        write_crash("⚠️ Exception: " + std::string(e.what()));
        write_result("❌ Crashed");
    } catch (...) {
        write_connect_log("❌ Unknown exception!");
        write_crash("⚠️ Unknown exception!");
        write_result("❌ Crashed");
    }
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
            size_t colon = ip.find(':');
            std::string ipPart = (colon != std::string::npos) ? ip.substr(0, colon) : ip;
            int port = (colon != std::string::npos) ? std::stoi(ip.substr(colon + 1)) : DEFAULT_PORT;
            write_connect_log("📌 Auto-setting IP: " + ipPart + " Port: " + std::to_string(port));
            SetIPAndPort(ipPart, (uint16_t)port);
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
        OBFUSCATE("Category_🌐 Network"),
        OBFUSCATE("InputText_Enter IP:Port"),
        OBFUSCATE("Button_Set IP & Port"),
        OBFUSCATE("Button_📋 Show Current IP"),
        OBFUSCATE("RichTextView_📁 /sdcard/Download/lac/"),
        OBFUSCATE("RichTextView_📌 Check connect_log.txt"),
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
            
            case 1: { // Set IP & Port
                write_report("🔘 Set IP & Port pressed");
                if (g_targetIP.empty()) {
                    g_targetIP = load_ip();
                    if (g_targetIP.empty()) {
                        g_targetIP = std::string(DEFAULT_IP) + ":" + std::to_string(DEFAULT_PORT);
                        save_ip(g_targetIP);
                    }
                }
                size_t colon = g_targetIP.find(':');
                std::string ip = (colon != std::string::npos) ? g_targetIP.substr(0, colon) : g_targetIP;
                int port = (colon != std::string::npos) ? std::stoi(g_targetIP.substr(colon + 1)) : DEFAULT_PORT;
                SetIPAndPort(ip, (uint16_t)port);
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
    // ====== نصب هوک ======
    void* startAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0xF571A4"));
    if (startAddr != nullptr && is_valid_address(startAddr)) {
        int res = DobbyHook(startAddr, (dobby_dummy_func_t)hook_GtaMenuStart, (dobby_dummy_func_t*)&orig_GtaMenuStart);
        if (res == 0) {
            write_report("✅ GtaMenuControl.Start() hooked");
            write_debug("✅ GtaMenuControl.Start() hooked");
        } else {
            write_report("❌ Hook failed, error: " + std::to_string(res));
            write_debug("❌ Hook failed");
        }
    }
#endif

    // ====== بارگذاری IP ======
    g_targetIP = load_ip();
    if (g_targetIP.empty()) {
        g_targetIP = std::string(DEFAULT_IP) + ":" + std::to_string(DEFAULT_PORT);
        save_ip(g_targetIP);
        write_report("📌 Default IP set: " + g_targetIP);
    } else {
        write_report("📌 IP loaded from file: " + g_targetIP);
    }

    // ====== گرفتن instance ======
    void* instance = get_gta_instance();
    if (instance != nullptr) {
        write_report("✅ Got instance via get_Instance");
    } else {
        write_report("⚠️ get_Instance returned null, waiting for hook...");
        for (int i = 0; i < 15 && g_gtaInstance == nullptr; i++) {
            sleep(1);
        }
        if (g_gtaInstance != nullptr) {
            write_report("✅ Got instance via hook");
        } else {
            write_report("❌ Failed to get instance!");
        }
    }

    // ====== تنظیم خودکار IP و Port ======
    if (g_gtaInstance != nullptr) {
        size_t colon = g_targetIP.find(':');
        std::string ip = (colon != std::string::npos) ? g_targetIP.substr(0, colon) : g_targetIP;
        int port = (colon != std::string::npos) ? std::stoi(g_targetIP.substr(colon + 1)) : DEFAULT_PORT;
        write_connect_log("📌 Auto-setting IP: " + ip + " Port: " + std::to_string(port));
        SetIPAndPort(ip, (uint16_t)port);
    }

    write_report("✅ hack_thread finished successfully");
    write_debug("✅ hack_thread finished");
}

// ======================== تابع ورودی ========================
__attribute__((constructor))
void lib_main() {
    create_directory();
    install_crash_handler();

    write_report("🚀 lib_main called - mod loading");
    write_debug("🚀 lib_main called");
    std::thread(hack_thread).detach();
}