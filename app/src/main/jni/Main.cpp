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
#define OFFSET_START_HOST           0x1AADC24     // NetworkManager.StartHost()
#define OFFSET_LOCAL_JOIN           0xF5B844      // GtaMenuControl.LocalJoin()
#define OFFSET_LOCAL_HOST           0xF5B7E0      // GtaMenuControl.LocalHost()
#define OFFSET_ON_CLIENT_CONNECT    0xF7FA8C      // CustomNetworkManager.OnClientConnect()
#define OFFSET_ON_CLIENT_DISCONNECT 0xF80270      // CustomNetworkManager.OnClientDisconnect()

// ======================== مسیرها ========================
static std::string g_basePath = "/storage/emulated/0/Download/lac/";
static std::string g_debugLog = g_basePath + "mod_debug.txt";
static std::string g_ipResultLog = g_basePath + "ip_result.txt";
static std::string g_crashLog = g_basePath + "crash_log.txt";
static std::string g_reportLog = g_basePath + "full_report.txt";
static std::string g_connectLog = g_basePath + "connect_log.txt";
static std::string g_hookLog = g_basePath + "hook_log.txt";

static bool g_crashHandlerInstalled = false;
static void* g_gtaInstance = nullptr;
static void* g_networkManagerInstance = nullptr;
static bool g_gtaReady = false;
static bool g_startClientHookInstalled = false;
static bool g_connectHookInstalled = false;
static bool g_disconnectHookInstalled = false;
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

static void write_hook_log(const std::string& msg) {
    write_log(g_hookLog, msg);
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

static std::string get_ip() {
    if (!g_targetIP.empty()) return g_targetIP;
    std::string ip = load_ip();
    if (ip.empty()) {
        ip = std::string(DEFAULT_IP) + ":" + std::string(DEFAULT_PORT);
        save_ip(ip);
    }
    return ip;
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
    return map.readable && map.startAddress <= (uintptr_t)addr && (uintptr_t)addr < map.endAddress;
}

// ======================== تبدیل MonoString ========================
static std::string mono_string_to_utf8(void* monoString) {
    if (monoString == nullptr) return "";
    if (!is_valid_address(monoString)) return "";
    
    typedef int32_t (*il2cpp_string_length_t)(void*);
    il2cpp_string_length_t il2cpp_string_length = 
        (il2cpp_string_length_t)getAbsoluteAddress("libil2cpp.so", "il2cpp_string_length");
    if (il2cpp_string_length == nullptr) return "";
    
    int length = il2cpp_string_length(monoString);
    if (length <= 0 || length > 65536) return "";
    
    typedef uint16_t* (*il2cpp_string_chars_t)(void*);
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

// ======================== ساخت MonoString ========================
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

// ======================== تزریق IP ========================
static void inject_ip_to_input(const std::string& ip) {
    void* instance = get_gta_instance();
    if (instance == nullptr) {
        write_report("❌ Cannot get instance for IP injection");
        return;
    }
    
    void* ipInput = *(void**)((uintptr_t)instance + OFFSET_IP_INPUT);
    if (ipInput == nullptr || !is_valid_address(ipInput)) {
        write_report("❌ ipInput is null or invalid");
        return;
    }
    
    void** mTextPtr = (void**)((uintptr_t)ipInput + OFFSET_INPUTFIELD_M_TEXT);
    if (mTextPtr == nullptr || !is_valid_address(mTextPtr)) {
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

// ======================== تنظیم networkAddress در NetworkManager ========================
static bool set_network_address(const std::string& ip, void* nmInstance) {
    if (nmInstance == nullptr || !is_valid_address(nmInstance)) {
        write_connect_log("❌ NetworkManager instance invalid");
        return false;
    }
    
    void* nmAddr = (void*)((uintptr_t)nmInstance + OFFSET_NETWORK_ADDR);
    if (!is_valid_address(nmAddr)) {
        write_connect_log("❌ networkAddress address invalid");
        return false;
    }
    
    void** nmAddrPtr = (void**)nmAddr;
    if (nmAddrPtr == nullptr || !is_valid_address(nmAddrPtr)) {
        write_connect_log("❌ networkAddress pointer invalid");
        return false;
    }
    
    void* monoString = create_mono_string(ip.c_str());
    if (monoString == nullptr) {
        write_connect_log("❌ Failed to create mono string for networkAddress");
        return false;
    }
    
    *nmAddrPtr = monoString;
    write_connect_log("✅ networkAddress set to: " + ip);
    write_report("✅ networkAddress set to: " + ip);
    return true;
}

// ======================== ======== روش‌های اتصال ======== ========================

// ====== روش ۱: صدا زدن LocalJoin با آفست مستقیم ======
static void connect_method1() {
    write_connect_log("\n========== [METHOD 1] LocalJoin (Direct Offset) ==========");
    write_connect_log("Time: " + get_time());
    write_report("🔘 Method 1: LocalJoin Direct pressed");
    write_result("🔘 Method 1");
    
    try {
        std::string ip = get_ip();
        write_connect_log("📡 Target IP: " + ip);
        write_report("📡 Target IP: " + ip);
        
        // 1. گرفتن GtaMenuControl
        void* gtaInstance = get_gta_instance();
        if (gtaInstance == nullptr || !is_valid_address(gtaInstance)) {
            write_connect_log("❌ GtaMenuControl instance is null or invalid");
            write_report("❌ GtaMenuControl instance not found");
            return;
        }
        write_connect_log("✅ GtaMenuControl instance: 0x" + std::to_string((uintptr_t)gtaInstance));
        
        // 2. تزریق IP در ipInput
        inject_ip_to_input(ip);
        
        // 3. گرفتن آدرس LocalJoin
        void* localJoinAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0xF5B844"));
        if (localJoinAddr == nullptr || !is_valid_address(localJoinAddr)) {
            write_connect_log("❌ LocalJoin address not found");
            write_report("❌ LocalJoin address not found");
            return;
        }
        write_connect_log("✅ LocalJoin address: 0x" + std::to_string((uintptr_t)localJoinAddr));
        
        // 4. صدا زدن LocalJoin
        typedef void (*local_join_t)(void*);
        local_join_t LocalJoin = (local_join_t)localJoinAddr;
        
        write_connect_log("🔄 Calling LocalJoin()...");
        write_report("🔄 Calling LocalJoin()...");
        LocalJoin(gtaInstance);
        
        write_connect_log("✅ LocalJoin() called successfully");
        write_report("✅ LocalJoin() called successfully");
        write_result("✅ Method 1: LocalJoin called");
        
    } catch (const std::exception& e) {
        write_connect_log("❌ Exception: " + std::string(e.what()));
        write_report("❌ Exception: " + std::string(e.what()));
        write_crash("⚠️ Exception: " + std::string(e.what()));
    } catch (...) {
        write_connect_log("❌ Unknown exception!");
        write_report("❌ Unknown exception!");
        write_crash("⚠️ Unknown exception!");
    }
    write_connect_log("========== METHOD 1 FINISHED ==========\n");
}

// ====== روش ۲: صدا زدن StartClient با آفست مستقیم ======
static void connect_method2() {
    write_connect_log("\n========== [METHOD 2] StartClient (Direct Offset) ==========");
    write_connect_log("Time: " + get_time());
    write_report("🔘 Method 2: StartClient Direct pressed");
    write_result("🔘 Method 2");
    
    try {
        std::string ip = get_ip();
        write_connect_log("📡 Target IP: " + ip);
        write_report("📡 Target IP: " + ip);
        
        // 1. گرفتن NetworkManager
        void* nmInstance = get_network_manager_instance();
        if (nmInstance == nullptr || !is_valid_address(nmInstance)) {
            write_connect_log("❌ NetworkManager instance is null or invalid");
            write_report("❌ NetworkManager not found");
            return;
        }
        write_connect_log("✅ NetworkManager instance: 0x" + std::to_string((uintptr_t)nmInstance));
        
        // 2. تنظیم networkAddress
        if (!set_network_address(ip, nmInstance)) {
            write_connect_log("❌ Failed to set networkAddress");
            return;
        }
        
        // 3. گرفتن آدرس StartClient
        void* startClientAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0x1AACCB4"));
        if (startClientAddr == nullptr || !is_valid_address(startClientAddr)) {
            write_connect_log("❌ StartClient address not found");
            write_report("❌ StartClient address not found");
            return;
        }
        write_connect_log("✅ StartClient address: 0x" + std::to_string((uintptr_t)startClientAddr));
        
        // 4. صدا زدن StartClient
        typedef void (*start_client_t)(void*);
        start_client_t StartClient = (start_client_t)startClientAddr;
        
        write_connect_log("🔄 Calling StartClient()...");
        write_report("🔄 Calling StartClient()...");
        StartClient(nmInstance);
        
        write_connect_log("✅ StartClient() called successfully");
        write_report("✅ StartClient() called successfully");
        write_result("✅ Method 2: StartClient called");
        
    } catch (const std::exception& e) {
        write_connect_log("❌ Exception: " + std::string(e.what()));
        write_report("❌ Exception: " + std::string(e.what()));
        write_crash("⚠️ Exception: " + std::string(e.what()));
    } catch (...) {
        write_connect_log("❌ Unknown exception!");
        write_report("❌ Unknown exception!");
        write_crash("⚠️ Unknown exception!");
    }
    write_connect_log("========== METHOD 2 FINISHED ==========\n");
}

// ====== روش ۳: JNI + ClassLoader ======
static void connect_method3(JNIEnv* env) {
    if (env == nullptr) {
        write_connect_log("❌ JNIEnv is null!");
        return;
    }
    
    write_connect_log("\n========== [METHOD 3] JNI + ClassLoader ==========");
    write_connect_log("Time: " + get_time());
    write_report("🔘 Method 3: JNI + ClassLoader pressed");
    write_result("🔘 Method 3");
    
    try {
        std::string ip = get_ip();
        write_connect_log("📡 Target IP: " + ip);
        write_report("📡 Target IP: " + ip);
        
        // 1. گرفتن Activity
        jclass unityPlayerClass = env->FindClass("com/unity3d/player/UnityPlayer");
        if (unityPlayerClass == nullptr) {
            env->ExceptionClear();
            write_connect_log("❌ UnityPlayer class not found");
            write_report("❌ UnityPlayer class not found");
            return;
        }
        write_connect_log("✅ UnityPlayer class found");
        
        jfieldID currentActivityField = env->GetStaticFieldID(unityPlayerClass, "currentActivity", "Landroid/app/Activity;");
        if (currentActivityField == nullptr) {
            env->ExceptionClear();
            write_connect_log("❌ currentActivity field not found");
            env->DeleteLocalRef(unityPlayerClass);
            return;
        }
        
        jobject activity = env->GetStaticObjectField(unityPlayerClass, currentActivityField);
        if (activity == nullptr) {
            write_connect_log("❌ Activity is null");
            env->DeleteLocalRef(unityPlayerClass);
            return;
        }
        write_connect_log("✅ Activity obtained");
        
        // 2. ClassLoader
        jclass activityCls = env->GetObjectClass(activity);
        jmethodID getClassLoader = env->GetMethodID(activityCls, "getClassLoader", "()Ljava/lang/ClassLoader;");
        if (getClassLoader == nullptr) {
            env->ExceptionClear();
            write_connect_log("❌ getClassLoader method not found");
            env->DeleteLocalRef(activityCls);
            env->DeleteLocalRef(activity);
            env->DeleteLocalRef(unityPlayerClass);
            return;
        }
        
        jobject classLoader = env->CallObjectMethod(activity, getClassLoader);
        if (classLoader == nullptr) {
            write_connect_log("❌ ClassLoader is null");
            env->DeleteLocalRef(activityCls);
            env->DeleteLocalRef(activity);
            env->DeleteLocalRef(unityPlayerClass);
            return;
        }
        write_connect_log("✅ ClassLoader obtained");
        
        // 3. پیدا کردن کلاس GtaMenuControl
        jclass classLoaderCls = env->FindClass("java/lang/ClassLoader");
        if (classLoaderCls == nullptr) {
            env->ExceptionClear();
            write_connect_log("❌ ClassLoader class not found");
            env->DeleteLocalRef(classLoader);
            env->DeleteLocalRef(activityCls);
            env->DeleteLocalRef(activity);
            env->DeleteLocalRef(unityPlayerClass);
            return;
        }
        
        jmethodID loadClass = env->GetMethodID(classLoaderCls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
        if (loadClass == nullptr) {
            env->ExceptionClear();
            write_connect_log("❌ loadClass method not found");
            env->DeleteLocalRef(classLoaderCls);
            env->DeleteLocalRef(classLoader);
            env->DeleteLocalRef(activityCls);
            env->DeleteLocalRef(activity);
            env->DeleteLocalRef(unityPlayerClass);
            return;
        }
        
        jstring className = env->NewStringUTF("GtaMenuControl");
        jclass gtaClass = (jclass)env->CallObjectMethod(classLoader, loadClass, className);
        env->DeleteLocalRef(className);
        
        if (gtaClass == nullptr) {
            env->ExceptionClear();
            write_connect_log("❌ GtaMenuControl class not found with ClassLoader");
            write_report("❌ GtaMenuControl class not found");
            env->DeleteLocalRef(classLoaderCls);
            env->DeleteLocalRef(classLoader);
            env->DeleteLocalRef(activityCls);
            env->DeleteLocalRef(activity);
            env->DeleteLocalRef(unityPlayerClass);
            return;
        }
        write_connect_log("✅ GtaMenuControl class found with ClassLoader");
        
        // 4. get_Instance
        jmethodID getInstance = env->GetStaticMethodID(gtaClass, "get_Instance", "()LGtaMenuControl;");
        if (getInstance == nullptr) {
            env->ExceptionClear();
            write_connect_log("❌ get_Instance method not found");
            env->DeleteLocalRef(gtaClass);
            env->DeleteLocalRef(classLoaderCls);
            env->DeleteLocalRef(classLoader);
            env->DeleteLocalRef(activityCls);
            env->DeleteLocalRef(activity);
            env->DeleteLocalRef(unityPlayerClass);
            return;
        }
        write_connect_log("✅ get_Instance method found");
        
        jobject gtaObj = env->CallStaticObjectMethod(gtaClass, getInstance);
        if (gtaObj == nullptr) {
            write_connect_log("❌ GtaMenuControl instance is null");
            env->DeleteLocalRef(gtaClass);
            env->DeleteLocalRef(classLoaderCls);
            env->DeleteLocalRef(classLoader);
            env->DeleteLocalRef(activityCls);
            env->DeleteLocalRef(activity);
            env->DeleteLocalRef(unityPlayerClass);
            return;
        }
        write_connect_log("✅ GtaMenuControl instance obtained via JNI");
        
        // 5. تنظیم IP در ipInput (از طریق JNI)
        jfieldID ipInputField = env->GetFieldID(gtaClass, "ipInput", "LUnityEngine/UI/InputField;");
        if (ipInputField != nullptr) {
            jobject ipInput = env->GetObjectField(gtaObj, ipInputField);
            if (ipInput != nullptr) {
                jclass inputFieldClass = env->GetObjectClass(ipInput);
                jfieldID mTextField = env->GetFieldID(inputFieldClass, "m_Text", "Ljava/lang/String;");
                if (mTextField != nullptr) {
                    jstring jip = env->NewStringUTF(ip.c_str());
                    env->SetObjectField(ipInput, mTextField, jip);
                    env->DeleteLocalRef(jip);
                    write_connect_log("✅ IP injected via JNI");
                }
                env->DeleteLocalRef(inputFieldClass);
                env->DeleteLocalRef(ipInput);
            }
        } else {
            env->ExceptionClear();
            write_connect_log("⚠️ ipInput field not found, skipping");
        }
        
        // 6. صدا زدن LocalJoin از طریق JNI
        jmethodID localJoinMethod = env->GetMethodID(gtaClass, "LocalJoin", "()V");
        if (localJoinMethod == nullptr) {
            env->ExceptionClear();
            write_connect_log("❌ LocalJoin method not found");
            env->DeleteLocalRef(gtaObj);
            env->DeleteLocalRef(gtaClass);
            env->DeleteLocalRef(classLoaderCls);
            env->DeleteLocalRef(classLoader);
            env->DeleteLocalRef(activityCls);
            env->DeleteLocalRef(activity);
            env->DeleteLocalRef(unityPlayerClass);
            return;
        }
        write_connect_log("✅ LocalJoin method found");
        
        write_connect_log("🔄 Calling LocalJoin() via JNI...");
        write_report("🔄 Calling LocalJoin() via JNI...");
        env->CallVoidMethod(gtaObj, localJoinMethod);
        
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            write_connect_log("⚠️ Exception during LocalJoin call");
            write_report("⚠️ Exception during LocalJoin call");
        } else {
            write_connect_log("✅ LocalJoin() called successfully via JNI");
            write_report("✅ LocalJoin() called successfully via JNI");
            write_result("✅ Method 3: JNI + ClassLoader called");
        }
        
        env->DeleteLocalRef(gtaObj);
        env->DeleteLocalRef(gtaClass);
        env->DeleteLocalRef(classLoaderCls);
        env->DeleteLocalRef(classLoader);
        env->DeleteLocalRef(activityCls);
        env->DeleteLocalRef(activity);
        env->DeleteLocalRef(unityPlayerClass);
        
    } catch (const std::exception& e) {
        write_connect_log("❌ Exception: " + std::string(e.what()));
        write_report("❌ Exception: " + std::string(e.what()));
        write_crash("⚠️ Exception: " + std::string(e.what()));
    } catch (...) {
        write_connect_log("❌ Unknown exception!");
        write_report("❌ Unknown exception!");
        write_crash("⚠️ Unknown exception!");
    }
    write_connect_log("========== METHOD 3 FINISHED ==========\n");
}

// ====== روش ۴: JNI + FindObjectOfType ======
static void connect_method4(JNIEnv* env) {
    if (env == nullptr) {
        write_connect_log("❌ JNIEnv is null!");
        return;
    }
    
    write_connect_log("\n========== [METHOD 4] JNI + FindObjectOfType ==========");
    write_connect_log("Time: " + get_time());
    write_report("🔘 Method 4: JNI + FindObjectOfType pressed");
    write_result("🔘 Method 4");
    
    try {
        std::string ip = get_ip();
        write_connect_log("📡 Target IP: " + ip);
        write_report("📡 Target IP: " + ip);
        
        // 1. پیدا کردن UnityEngine.Object
        jclass objClass = env->FindClass("UnityEngine/Object");
        if (objClass == nullptr) {
            env->ExceptionClear();
            write_connect_log("❌ UnityEngine.Object class not found");
            write_report("❌ UnityEngine.Object class not found");
            return;
        }
        write_connect_log("✅ UnityEngine.Object class found");
        
        // 2. FindObjectOfType
        jmethodID findObj = env->GetStaticMethodID(objClass, "FindObjectOfType", "(Ljava/lang/Class;)Ljava/lang/Object;");
        if (findObj == nullptr) {
            env->ExceptionClear();
            write_connect_log("❌ FindObjectOfType method not found");
            env->DeleteLocalRef(objClass);
            return;
        }
        write_connect_log("✅ FindObjectOfType method found");
        
        // 3. پیدا کردن CustomNetworkManager
        jclass nmClass = env->FindClass("CustomNetworkManager");
        if (nmClass == nullptr) {
            env->ExceptionClear();
            write_connect_log("❌ CustomNetworkManager class not found, trying NetworkManager...");
            nmClass = env->FindClass("NetworkManager");
            if (nmClass == nullptr) {
                env->ExceptionClear();
                write_connect_log("❌ NetworkManager class not found");
                write_report("❌ NetworkManager class not found");
                env->DeleteLocalRef(objClass);
                return;
            }
        }
        write_connect_log("✅ CustomNetworkManager/NetworkManager class found");
        
        // 4. پیدا کردن instance
        jobject nmObj = env->CallStaticObjectMethod(objClass, findObj, nmClass);
        env->DeleteLocalRef(objClass);
        env->DeleteLocalRef(nmClass);
        
        if (nmObj == nullptr) {
            write_connect_log("❌ CustomNetworkManager instance not found");
            write_report("❌ CustomNetworkManager instance not found");
            return;
        }
        write_connect_log("✅ CustomNetworkManager instance found: 0x" + std::to_string((uintptr_t)nmObj));
        
        // 5. تنظیم networkAddress از طریق JNI
        jclass nmObjClass = env->GetObjectClass(nmObj);
        jfieldID addrField = env->GetFieldID(nmObjClass, "networkAddress", "Ljava/lang/String;");
        if (addrField != nullptr) {
            jstring jip = env->NewStringUTF(ip.c_str());
            env->SetObjectField(nmObj, addrField, jip);
            env->DeleteLocalRef(jip);
            write_connect_log("✅ networkAddress set via JNI");
            write_report("✅ networkAddress set via JNI");
        } else {
            env->ExceptionClear();
            write_connect_log("⚠️ networkAddress field not found, skipping");
        }
        
        // 6. صدا زدن StartClient
        jmethodID startClient = env->GetMethodID(nmObjClass, "StartClient", "()V");
        if (startClient == nullptr) {
            env->ExceptionClear();
            write_connect_log("❌ StartClient method not found");
            write_report("❌ StartClient method not found");
            env->DeleteLocalRef(nmObj);
            env->DeleteLocalRef(nmObjClass);
            return;
        }
        write_connect_log("✅ StartClient method found");
        
        write_connect_log("🔄 Calling StartClient() via JNI...");
        write_report("🔄 Calling StartClient() via JNI...");
        env->CallVoidMethod(nmObj, startClient);
        
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            write_connect_log("⚠️ Exception during StartClient call");
            write_report("⚠️ Exception during StartClient call");
        } else {
            write_connect_log("✅ StartClient() called successfully via JNI");
            write_report("✅ StartClient() called successfully via JNI");
            write_result("✅ Method 4: JNI + FindObjectOfType called");
        }
        
        env->DeleteLocalRef(nmObj);
        env->DeleteLocalRef(nmObjClass);
        
    } catch (const std::exception& e) {
        write_connect_log("❌ Exception: " + std::string(e.what()));
        write_report("❌ Exception: " + std::string(e.what()));
        write_crash("⚠️ Exception: " + std::string(e.what()));
    } catch (...) {
        write_connect_log("❌ Unknown exception!");
        write_report("❌ Unknown exception!");
        write_crash("⚠️ Unknown exception!");
    }
    write_connect_log("========== METHOD 4 FINISHED ==========\n");
}

// ======================== ======== هوک‌ها ======== ========================

// ====== هوک روی StartClient ======
void (*orig_StartClient)(void *instance);
void hook_StartClient(void *instance) {
    write_hook_log("\n========== 🔥 StartClient() HOOKED ==========");
    write_hook_log("Time: " + get_time());
    write_hook_log("📡 Instance: 0x" + std::to_string((uintptr_t)instance));
    write_connect_log("🔥 StartClient() HOOKED at " + get_time());
    write_report("🔥 StartClient() was called by game!");
    write_result("🔥 StartClient hooked");
    
    if (instance != nullptr && is_valid_address(instance)) {
        void* nmAddr = (void*)((uintptr_t)instance + OFFSET_NETWORK_ADDR);
        if (is_valid_address(nmAddr)) {
            void** nmAddrPtr = (void**)nmAddr;
            if (nmAddrPtr != nullptr && *nmAddrPtr != nullptr) {
                std::string currentIP = mono_string_to_utf8(*nmAddrPtr);
                write_hook_log("📡 Current networkAddress: " + currentIP);
                write_connect_log("📡 Current IP in NetworkManager: " + currentIP);
            }
        }
    }
    
    if (orig_StartClient) {
        orig_StartClient(instance);
        write_hook_log("✅ Original StartClient executed");
        write_connect_log("✅ Original StartClient executed");
    } else {
        write_hook_log("❌ orig_StartClient is null!");
        write_connect_log("❌ orig_StartClient is null!");
    }
    write_hook_log("========== HOOK FINISHED ==========\n");
}

// ====== نصب هوک StartClient ======
static void install_startclient_hook() {
    if (g_startClientHookInstalled) {
        write_report("⚠️ StartClient hook already installed");
        return;
    }
    
    write_report("🔧 Installing StartClient hook...");
    write_hook_log("🔧 Installing StartClient hook...");
    
#if defined(__aarch64__)
    void* addr = getAbsoluteAddress(targetLibName, OBFUSCATE("0x1AACCB4"));
    if (addr != nullptr && is_valid_address(addr)) {
        int res = DobbyHook(addr, (dobby_dummy_func_t)hook_StartClient, (dobby_dummy_func_t*)&orig_StartClient);
        if (res == 0) {
            g_startClientHookInstalled = true;
            write_report("✅ StartClient hook installed at 0x" + std::to_string((uintptr_t)addr));
            write_hook_log("✅ StartClient hook installed");
        } else {
            write_report("❌ StartClient hook failed! error: " + std::to_string(res));
            write_hook_log("❌ StartClient hook failed");
        }
    } else {
        write_report("❌ StartClient address not found");
        write_hook_log("❌ StartClient address not found");
    }
#else
    write_report("❌ Not ARM64 architecture");
#endif
}

// ====== هوک OnClientConnect ======
void (*orig_OnClientConnect)(void *instance, void *conn);
void hook_OnClientConnect(void *instance, void *conn) {
    write_hook_log("\n========== ✅ Client CONNECTED! ==========");
    write_hook_log("Time: " + get_time());
    write_connect_log("✅ Client CONNECTED to server!");
    write_report("✅ Client connected to server!");
    write_result("✅ Connected!");
    
    if (orig_OnClientConnect) {
        orig_OnClientConnect(instance, conn);
        write_hook_log("✅ Original OnClientConnect executed");
    } else {
        write_hook_log("❌ orig_OnClientConnect is null!");
    }
    write_hook_log("========== CONNECT FINISHED ==========\n");
}

// ====== هوک OnClientDisconnect ======
void (*orig_OnClientDisconnect)(void *instance);
void hook_OnClientDisconnect(void *instance) {
    write_hook_log("\n========== ❌ Client DISCONNECTED! ==========");
    write_hook_log("Time: " + get_time());
    write_connect_log("❌ Client DISCONNECTED!");
    write_report("❌ Client disconnected!");
    write_result("❌ Disconnected!");
    
    if (orig_OnClientDisconnect) {
        orig_OnClientDisconnect(instance);
        write_hook_log("✅ Original OnClientDisconnect executed");
    } else {
        write_hook_log("❌ orig_OnClientDisconnect is null!");
    }
    write_hook_log("========== DISCONNECT FINISHED ==========\n");
}

// ====== نصب هوک‌های Connection ======
static void install_connection_hooks() {
    write_report("🔧 Installing connection hooks...");
    write_hook_log("🔧 Installing connection hooks...");
    
#if defined(__aarch64__)
    // OnClientConnect
    void* connectAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0xF7FA8C"));
    if (connectAddr != nullptr && is_valid_address(connectAddr)) {
        int res = DobbyHook(connectAddr, (dobby_dummy_func_t)hook_OnClientConnect, (dobby_dummy_func_t*)&orig_OnClientConnect);
        if (res == 0) {
            g_connectHookInstalled = true;
            write_report("✅ OnClientConnect hook installed");
            write_hook_log("✅ OnClientConnect hook installed");
        } else {
            write_report("❌ OnClientConnect hook failed! error: " + std::to_string(res));
            write_hook_log("❌ OnClientConnect hook failed");
        }
    } else {
        write_report("❌ OnClientConnect address not found");
        write_hook_log("❌ OnClientConnect address not found");
    }
    
    // OnClientDisconnect
    void* disconnectAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0xF80270"));
    if (disconnectAddr != nullptr && is_valid_address(disconnectAddr)) {
        int res = DobbyHook(disconnectAddr, (dobby_dummy_func_t)hook_OnClientDisconnect, (dobby_dummy_func_t*)&orig_OnClientDisconnect);
        if (res == 0) {
            g_disconnectHookInstalled = true;
            write_report("✅ OnClientDisconnect hook installed");
            write_hook_log("✅ OnClientDisconnect hook installed");
        } else {
            write_report("❌ OnClientDisconnect hook failed! error: " + std::to_string(res));
            write_hook_log("❌ OnClientDisconnect hook failed");
        }
    } else {
        write_report("❌ OnClientDisconnect address not found");
        write_hook_log("❌ OnClientDisconnect address not found");
    }
#else
    write_report("❌ Not ARM64 architecture");
#endif
}

// ======================== هوک GtaMenuControl.Start ========================
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
        OBFUSCATE("Category_🔗 Connect Methods"),
        OBFUSCATE("Button_Method 1: LocalJoin (Direct)"),
        OBFUSCATE("Button_Method 2: StartClient (Direct)"),
        OBFUSCATE("Button_Method 3: JNI + ClassLoader"),
        OBFUSCATE("Button_Method 4: JNI + FindObjectOfType"),
        OBFUSCATE("Category_🔧 Hooks"),
        OBFUSCATE("Button_Install StartClient Hook"),
        OBFUSCATE("Button_Install Connection Hooks"),
        OBFUSCATE("RichTextView_📁 Logs: /sdcard/Download/lac/"),
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
                g_targetIP = get_ip();
                save_ip(g_targetIP);
            }
            inject_ip_to_input(g_targetIP);
            write_result("✅ IP injected: " + g_targetIP);
            break;
        }
        
        case 2: { connect_method1(); break; }
        case 3: { connect_method2(); break; }
        case 4: { connect_method3(env); break; }
        case 5: { connect_method4(env); break; }
        
        case 6: { install_startclient_hook(); break; }
        case 7: { install_connection_hooks(); break; }
        
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
    // ====== هوک روی GtaMenuControl.Start ======
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
        write_report("❌ GtaMenuControl.Start() address not found");
        write_debug("❌ Address not found");
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

    // ====== IP ======
    g_targetIP = get_ip();
    save_ip(g_targetIP);
    write_report("📌 IP: " + g_targetIP);
    write_debug("📌 Current IP: " + g_targetIP);

    // ====== تزریق خودکار IP ======
    if (g_gtaInstance != nullptr) {
        inject_ip_to_input(g_targetIP);
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