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
static void* g_cachedNetworkManager = nullptr;
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

// ======================== گرفتن IP ========================
static std::string get_ip() {
    if (!g_targetIP.empty()) return g_targetIP;
    
    std::string ip = load_ip();
    if (ip.empty()) {
        ip = std::string(DEFAULT_IP) + ":" + std::string(DEFAULT_PORT);
        save_ip(ip);
    }
    return ip;
}

// ======================== ======== روش‌های پیدا کردن CustomNetworkManager ======== ========================

// ======================== روش ۱: از GtaMenuControl._networkManager ========================
static void* find_nm_method1() {
    write_connect_log("   Method 1: GtaMenuControl._networkManager (0x258)");
    
    if (g_gtaInstance == nullptr || !is_valid_address(g_gtaInstance)) {
        write_connect_log("      ❌ GtaMenuControl instance not ready");
        return nullptr;
    }
    
    void* nm = *(void**)((uintptr_t)g_gtaInstance + OFFSET_NETWORK_MANAGER);
    if (nm == nullptr || !is_valid_address(nm)) {
        write_connect_log("      ❌ _networkManager is null or invalid");
        return nullptr;
    }
    
    write_connect_log("      ✅ Found: 0x" + std::to_string((uintptr_t)nm));
    return nm;
}

// ======================== روش ۲: FindObjectOfType (JNI) ========================
static void* find_nm_method2(JNIEnv* env) {
    write_connect_log("   Method 2: FindObjectOfType (JNI)");
    
    if (env == nullptr) {
        write_connect_log("      ❌ JNIEnv is null");
        return nullptr;
    }
    
    try {
        jclass objClass = env->FindClass("UnityEngine/Object");
        if (objClass == nullptr) {
            env->ExceptionClear();
            write_connect_log("      ❌ UnityEngine.Object not found");
            return nullptr;
        }
        
        jmethodID findObj = env->GetStaticMethodID(objClass, "FindObjectOfType", "(Ljava/lang/Class;)Ljava/lang/Object;");
        if (findObj == nullptr) {
            env->ExceptionClear();
            write_connect_log("      ❌ FindObjectOfType not found");
            env->DeleteLocalRef(objClass);
            return nullptr;
        }
        
        jclass nmClass = env->FindClass("CustomNetworkManager");
        if (nmClass == nullptr) {
            env->ExceptionClear();
            write_connect_log("      ❌ CustomNetworkManager class not found");
            env->DeleteLocalRef(objClass);
            return nullptr;
        }
        
        jobject nmObj = env->CallStaticObjectMethod(objClass, findObj, nmClass);
        env->DeleteLocalRef(objClass);
        env->DeleteLocalRef(nmClass);
        
        if (nmObj == nullptr) {
            write_connect_log("      ❌ Instance not found");
            return nullptr;
        }
        
        write_connect_log("      ✅ Found: 0x" + std::to_string((uintptr_t)nmObj));
        return (void*)nmObj;
        
    } catch (...) {
        write_connect_log("      ❌ Exception in JNI");
        return nullptr;
    }
}

// ======================== روش ۳: GameObject.Find (JNI) ========================
static void* find_nm_method3(JNIEnv* env) {
    write_connect_log("   Method 3: GameObject.Find (JNI)");
    
    if (env == nullptr) {
        write_connect_log("      ❌ JNIEnv is null");
        return nullptr;
    }
    
    try {
        jclass gameObjClass = env->FindClass("UnityEngine.GameObject");
        if (gameObjClass == nullptr) {
            env->ExceptionClear();
            write_connect_log("      ❌ GameObject class not found");
            return nullptr;
        }
        
        jmethodID findMethod = env->GetStaticMethodID(gameObjClass, "Find", "(Ljava/lang/String;)LUnityEngine/GameObject;");
        if (findMethod == nullptr) {
            env->ExceptionClear();
            write_connect_log("      ❌ Find method not found");
            env->DeleteLocalRef(gameObjClass);
            return nullptr;
        }
        
        jstring name = env->NewStringUTF("NetworkManager");
        jobject go = env->CallStaticObjectMethod(gameObjClass, findMethod, name);
        env->DeleteLocalRef(name);
        
        if (go == nullptr) {
            write_connect_log("      ❌ GameObject not found");
            env->DeleteLocalRef(gameObjClass);
            return nullptr;
        }
        
        jmethodID getComp = env->GetMethodID(gameObjClass, "GetComponent", "(Ljava/lang/Class;)LUnityEngine/Component;");
        if (getComp == nullptr) {
            write_connect_log("      ❌ GetComponent not found");
            env->DeleteLocalRef(go);
            env->DeleteLocalRef(gameObjClass);
            return nullptr;
        }
        
        jclass nmClass = env->FindClass("CustomNetworkManager");
        if (nmClass == nullptr) {
            env->ExceptionClear();
            write_connect_log("      ❌ CustomNetworkManager class not found");
            env->DeleteLocalRef(go);
            env->DeleteLocalRef(gameObjClass);
            return nullptr;
        }
        
        jobject nmObj = env->CallObjectMethod(go, getComp, nmClass);
        env->DeleteLocalRef(go);
        env->DeleteLocalRef(gameObjClass);
        env->DeleteLocalRef(nmClass);
        
        if (nmObj == nullptr) {
            write_connect_log("      ❌ Component not found");
            return nullptr;
        }
        
        write_connect_log("      ✅ Found: 0x" + std::to_string((uintptr_t)nmObj));
        return (void*)nmObj;
        
    } catch (...) {
        write_connect_log("      ❌ Exception in JNI");
        return nullptr;
    }
}

// ======================== روش ۴: KittyMemory Scanner ========================
static void* find_nm_method4() {
    write_connect_log("   Method 4: KittyMemory Scanner");
    
    auto maps = KittyMemory::getAllMaps();
    for (auto &map : maps) {
        if (!map.readable || map.pathname.empty()) continue;
        
        uintptr_t found = KittyScanner::findDataFirst(map.startAddress, map.endAddress, "CustomNetworkManager", strlen("CustomNetworkManager"));
        if (found != 0) {
            write_connect_log("      ✅ Found string 'CustomNetworkManager' at 0x" + std::to_string(found));
            // جستجوی instance نزدیک
            for (uintptr_t addr = found - 0x200; addr < found + 0x200; addr += 8) {
                if (!is_valid_address((void*)addr)) continue;
                void* potential = *(void**)addr;
                if (potential != nullptr && is_valid_address(potential)) {
                    // چک اینکه آیا potential یک MonoBehaviour هست
                    // با چک کردن اینکه آیا آفست 0x68 (m_CachedPtr) معتبر هست
                    if (is_valid_address((void*)((uintptr_t)potential + 0x68))) {
                        write_connect_log("      ✅ Found potential instance at 0x" + std::to_string((uintptr_t)potential));
                        return potential;
                    }
                }
            }
        }
    }
    write_connect_log("      ❌ Not found");
    return nullptr;
}

// ======================== پیدا کردن CustomNetworkManager با همه روش‌ها ========================
static void* find_custom_network_manager(JNIEnv* env) {
    write_connect_log("\n🔍 Finding CustomNetworkManager...");
    
    void* nm = nullptr;
    
    // روش ۱: از GtaMenuControl
    nm = find_nm_method1();
    if (nm != nullptr) {
        write_connect_log("✅ Found with Method 1");
        return nm;
    }
    
    // روش ۲: FindObjectOfType
    nm = find_nm_method2(env);
    if (nm != nullptr) {
        write_connect_log("✅ Found with Method 2");
        g_cachedNetworkManager = nm;
        return nm;
    }
    
    // روش ۳: GameObject.Find
    nm = find_nm_method3(env);
    if (nm != nullptr) {
        write_connect_log("✅ Found with Method 3");
        g_cachedNetworkManager = nm;
        return nm;
    }
    
    // روش ۴: KittyMemory
    nm = find_nm_method4();
    if (nm != nullptr) {
        write_connect_log("✅ Found with Method 4");
        g_cachedNetworkManager = nm;
        return nm;
    }
    
    write_connect_log("❌ CustomNetworkManager not found with any method!");
    return nullptr;
}

// ======================== اتصال به سرور ========================
static void connect_to_server(JNIEnv* env) {
    write_connect_log("\n========== CONNECTING TO SERVER ==========");
    write_connect_log("Time: " + get_time());
    write_report("🔘 Connect button pressed");
    write_result("🔘 Connect button pressed");
    
    try {
        // 1. گرفتن IP
        std::string ip = get_ip();
        write_connect_log("📡 Target IP: " + ip);
        write_report("📡 Target IP: " + ip);
        
        // 2. پیدا کردن CustomNetworkManager
        void* nmInstance = find_custom_network_manager(env);
        if (nmInstance == nullptr) {
            write_connect_log("❌ CustomNetworkManager not found!");
            write_report("❌ CustomNetworkManager not found!");
            write_result("❌ NetworkManager not found!");
            return;
        }
        write_connect_log("✅ CustomNetworkManager instance: 0x" + std::to_string((uintptr_t)nmInstance));
        
        // 3. تنظیم networkAddress
        void* nmAddr = (void*)((uintptr_t)nmInstance + OFFSET_NETWORK_ADDR);
        if (!is_valid_address(nmAddr)) {
            write_connect_log("❌ networkAddress address is invalid: 0x" + std::to_string((uintptr_t)nmAddr));
            return;
        }
        
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
            write_result("✅ IP ready: " + g_targetIP);
            break;
        }
        
        case 2: {
            connect_to_server(env);
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
    void* startAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0xF571A4"));
    if (startAddr != nullptr && is_valid_address(startAddr)) {
        int res = DobbyHook(startAddr, (dobby_dummy_func_t)hook_GtaMenuStart, (dobby_dummy_func_t*)&orig_GtaMenuStart);
        if (res == 0) {
            write_report("✅ GtaMenuControl.Start() hooked");
        } else {
            write_report("❌ GtaMenuControl.Start() hook failed! error: " + std::to_string(res));
        }
    } else {
        write_report("❌ GtaMenuControl.Start() address not found");
    }
#endif

    for (int i = 0; i < 20 && g_gtaInstance == nullptr; i++) {
        sleep(1);
    }
    
    if (g_gtaInstance != nullptr) {
        write_report("✅ Got GtaMenuControl instance after wait");
        write_connect_log("✅ Got GtaMenuControl instance");
    } else {
        typedef void* (*get_instance_t)();
        get_instance_t get_Instance = (get_instance_t)getAbsoluteAddress("libil2cpp.so", "GtaMenuControl.get_Instance");
        if (get_Instance != nullptr) {
            g_gtaInstance = get_Instance();
            if (g_gtaInstance != nullptr) {
                write_report("✅ Got GtaMenuControl instance via get_Instance");
                write_connect_log("✅ Got GtaMenuControl instance via get_Instance");
            }
        }
    }

    // IP
    g_targetIP = load_ip();
    if (g_targetIP.empty()) {
        g_targetIP = std::string(DEFAULT_IP) + ":" + std::string(DEFAULT_PORT);
        save_ip(g_targetIP);
        write_report("📌 Default IP set: " + g_targetIP);
    } else {
        write_report("📌 IP loaded from file: " + g_targetIP);
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