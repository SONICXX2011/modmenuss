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

#define DEFAULT_IP "5.57.37.224"
#define DEFAULT_PORT "9876"

#define OFFSET_GET_INSTANCE         0xF570C4
#define OFFSET_GTA_START            0xF571A4
#define OFFSET_IP_INPUT             0xC0
#define OFFSET_INPUTFIELD_M_TEXT    0x180
#define OFFSET_NETWORK_MANAGER      0x258
#define OFFSET_NETWORK_ADDR         0x50

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
static std::string g_targetIP = "";

// ======================== توابع کمکی (همه تعریف شدن) ========================
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

// ====== اینجا write_debug تعریف شده ======
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

static std::string get_ip() {
    if (!g_targetIP.empty()) return g_targetIP;
    std::ifstream f(g_basePath + "last_ip.txt");
    if (f.is_open()) {
        std::string ip;
        std::getline(f, ip);
        f.close();
        return ip;
    }
    return std::string(DEFAULT_IP) + ":" + std::string(DEFAULT_PORT);
}

static void save_ip(const std::string& ip) {
    std::ofstream f(g_basePath + "last_ip.txt");
    if (f.is_open()) {
        f << ip << "\n";
        f.close();
        write_report("📝 IP saved: " + ip);
    }
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

// ======================== چک آدرس ========================
static bool is_valid_address(void* addr) {
    if (addr == nullptr) return false;
    uintptr_t ptr = (uintptr_t)addr;
    if (ptr < 0x1000) return false;
    if (ptr > 0x7FFFFFFFFFFFFF) return false;
    auto map = KittyMemory::getAddressMap(addr);
    return map.readable;
}

// ======================== IL2CPP توابع ========================
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

// ======================== گرفتن GtaMenuControl ========================
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

// ======================== گرفتن NetworkManager ========================
static void* get_network_manager() {
    if (g_networkManagerInstance != nullptr && is_valid_address(g_networkManagerInstance)) {
        return g_networkManagerInstance;
    }
    
    void* gtaInstance = get_gta_instance();
    if (gtaInstance == nullptr) {
        write_connect_log("❌ Cannot get GtaMenuControl for NetworkManager");
        return nullptr;
    }
    
    void* nmInstance = *(void**)((uintptr_t)gtaInstance + OFFSET_NETWORK_MANAGER);
    if (nmInstance == nullptr || !is_valid_address(nmInstance)) {
        write_connect_log("❌ NetworkManager invalid");
        return nullptr;
    }
    
    g_networkManagerInstance = nmInstance;
    write_connect_log("✅ NetworkManager: 0x" + std::to_string((uintptr_t)nmInstance));
    return nmInstance;
}

// ======================== دکمه Set IP ========================
static void set_ip_in_network_manager() {
    write_connect_log("\n========== SET IP IN NETWORKMANAGER ==========");
    write_connect_log("Time: " + get_time());
    write_report("🔘 Set IP in NetworkManager pressed");
    write_result("🔘 Set IP pressed");
    
    try {
        std::string ip = get_ip();
        write_connect_log("📡 Target IP: " + ip);
        write_report("📡 Target IP: " + ip);
        
        void* nmInstance = get_network_manager();
        if (nmInstance == nullptr) {
            write_connect_log("❌ NetworkManager not found");
            write_result("❌ NetworkManager not found");
            return;
        }
        write_connect_log("✅ NetworkManager: 0x" + std::to_string((uintptr_t)nmInstance));
        
        void* nmAddr = (void*)((uintptr_t)nmInstance + OFFSET_NETWORK_ADDR);
        if (!is_valid_address(nmAddr)) {
            write_connect_log("❌ networkAddress address invalid");
            return;
        }
        write_connect_log("✅ networkAddress address: 0x" + std::to_string((uintptr_t)nmAddr));
        
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
        write_result("✅ IP set in NetworkManager: " + ip);
        
        write_connect_log("✅ Done! Now click Connect in game.");
        write_report("✅ Done! Now click Connect in game.");
        
    } catch (const std::exception& e) {
        write_connect_log("❌ Exception: " + std::string(e.what()));
        write_report("❌ Exception: " + std::string(e.what()));
        write_crash("⚠️ Exception: " + std::string(e.what()));
    } catch (...) {
        write_connect_log("❌ Unknown exception");
        write_report("❌ Unknown exception");
        write_crash("⚠️ Unknown exception");
    }
    write_connect_log("========== SET IP FINISHED ==========\n");
}

// ======================== هوک GtaMenuControl.Start ========================
void (*orig_GtaMenuStart)(void *instance);
void hook_GtaMenuStart(void *instance) {
    if (instance != nullptr && is_valid_address(instance)) {
        g_gtaInstance = instance;
        write_report("✅ Hook captured GtaMenuControl instance");
        write_connect_log("✅ GtaMenuControl captured: 0x" + std::to_string((uintptr_t)instance));
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
        OBFUSCATE("Button_Set IP in NetworkManager"),
        OBFUSCATE("RichTextView_📁 Logs: /sdcard/Download/lac/connect_log.txt"),
        OBFUSCATE("RichTextView_📌 Then click Connect in game"),
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
                write_report("📝 IP entered: " + g_targetIP);
                write_result("📝 IP entered: " + g_targetIP);
            }
            break;
        }
        
        case 1: {
            set_ip_in_network_manager();
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
    while (!isLibraryLoaded(targetLibName)) sleep(1);
    write_report("✅ lib loaded");
    write_debug("✅ libil2cpp.so loaded!");
    
#if defined(__aarch64__)
    void* startAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0xF571A4"));
    if (startAddr != nullptr) {
        int res = DobbyHook(startAddr, (dobby_dummy_func_t)hook_GtaMenuStart, (dobby_dummy_func_t*)&orig_GtaMenuStart);
        if (res == 0) {
            write_report("✅ GtaMenuControl.Start() hooked");
            write_debug("✅ GtaMenuControl.Start() hooked");
        } else {
            write_report("❌ GtaMenuControl.Start() hook failed");
            write_debug("❌ Hook failed");
        }
    } else {
        write_report("❌ GtaMenuControl.Start() address not found");
        write_debug("❌ Address not found");
    }
#endif
    
    for (int i = 0; i < 15 && g_gtaInstance == nullptr; i++) sleep(1);
    
    g_targetIP = get_ip();
    save_ip(g_targetIP);
    write_report("📌 IP: " + g_targetIP);
    write_debug("📌 Current IP: " + g_targetIP);
    
    // تنظیم خودکار IP در NetworkManager
    if (g_gtaInstance != nullptr) {
        void* nmInstance = get_network_manager();
        if (nmInstance != nullptr) {
            void* nmAddr = (void*)((uintptr_t)nmInstance + OFFSET_NETWORK_ADDR);
            if (is_valid_address(nmAddr)) {
                void** nmAddrPtr = (void**)nmAddr;
                if (nmAddrPtr != nullptr) {
                    void* monoString = create_mono_string(g_targetIP.c_str());
                    if (monoString != nullptr) {
                        *nmAddrPtr = monoString;
                        write_report("✅ Auto-set IP in NetworkManager");
                    }
                }
            }
        }
    }
    
    write_report("✅ hack done");
    write_debug("✅ hack_thread finished");
}

__attribute__((constructor))
void lib_main() {
    create_directory();
    install_crash_handler();
    write_report("🚀 lib_main called");
    write_debug("🚀 lib_main called");
    std::thread(hack_thread).detach();
}