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
#define OFFSET_START_CLIENT         0x1AACCB4
#define OFFSET_LOCAL_JOIN           0xF5B844
#define OFFSET_ON_CLIENT_CONNECT    0xF7FA8C

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
static bool g_gtaReady = false;
static bool g_connectHooksInstalled = false;

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

// ======================== گرفتن instance با انتظار ========================
static void* get_gta_instance_safe() {
    if (g_gtaInstance != nullptr && is_valid_address(g_gtaInstance)) {
        return g_gtaInstance;
    }
    
    typedef void* (*get_instance_t)();
    get_instance_t get_Instance = (get_instance_t)getAbsoluteAddress("libil2cpp.so", "GtaMenuControl.get_Instance");
    if (get_Instance != nullptr) {
        void* instance = get_Instance();
        if (instance != nullptr && is_valid_address(instance)) {
            g_gtaInstance = instance;
            g_gtaReady = true;
            write_connect_log("✅ GtaMenuControl instance from get_Instance");
            return instance;
        }
    }
    
    for (int i = 0; i < 15 && g_gtaInstance == nullptr; i++) {
        write_connect_log("⏳ Waiting for GtaMenuControl... (" + std::to_string(i+1) + "/15)");
        sleep(1);
    }
    
    if (g_gtaInstance != nullptr && is_valid_address(g_gtaInstance)) {
        g_gtaReady = true;
        write_connect_log("✅ GtaMenuControl instance from hook");
        return g_gtaInstance;
    }
    
    write_connect_log("❌ GtaMenuControl instance not found!");
    return nullptr;
}

static void* get_network_manager_safe() {
    if (g_networkManagerInstance != nullptr && is_valid_address(g_networkManagerInstance)) {
        return g_networkManagerInstance;
    }
    
    void* gtaInstance = get_gta_instance_safe();
    if (gtaInstance == nullptr) return nullptr;
    
    void* nmInstance = *(void**)((uintptr_t)gtaInstance + OFFSET_NETWORK_MANAGER);
    if (nmInstance == nullptr || !is_valid_address(nmInstance)) {
        write_connect_log("❌ NetworkManager invalid");
        return nullptr;
    }
    
    g_networkManagerInstance = nmInstance;
    write_connect_log("✅ NetworkManager: 0x" + std::to_string((uintptr_t)nmInstance));
    return nmInstance;
}

static void inject_ip_to_input(const std::string& ip) {
    void* instance = get_gta_instance_safe();
    if (instance == nullptr) return;
    
    void* ipInput = *(void**)((uintptr_t)instance + OFFSET_IP_INPUT);
    if (ipInput == nullptr || !is_valid_address(ipInput)) return;
    
    void** mTextPtr = (void**)((uintptr_t)ipInput + OFFSET_INPUTFIELD_M_TEXT);
    if (mTextPtr == nullptr) return;
    
    void* monoString = create_mono_string(ip.c_str());
    if (monoString == nullptr) return;
    
    *mTextPtr = monoString;
    write_connect_log("✅ IP injected: " + ip);
}

// ======================== اجرا در UI Thread با Handler ========================
static void call_on_ui_thread(JNIEnv* env, void* instance, void* method_addr) {
    if (env == nullptr) {
        write_connect_log("❌ JNIEnv is null!");
        return;
    }
    
    write_connect_log("🔄 Running method on UI Thread via Handler...");
    
    // 1. گرفتن Activity
    jclass unityPlayerClass = env->FindClass("com/unity3d/player/UnityPlayer");
    if (unityPlayerClass == nullptr) {
        env->ExceptionClear();
        write_connect_log("❌ UnityPlayer class not found");
        return;
    }
    
    jfieldID currentActivityField = env->GetStaticFieldID(unityPlayerClass, "currentActivity", "Landroid/app/Activity;");
    jobject activity = env->GetStaticObjectField(unityPlayerClass, currentActivityField);
    if (activity == nullptr) {
        env->DeleteLocalRef(unityPlayerClass);
        write_connect_log("❌ Activity is null");
        return;
    }
    write_connect_log("✅ Activity obtained");
    
    // 2. ایجاد Handler
    jclass handlerClass = env->FindClass("android/os/Handler");
    if (handlerClass == nullptr) {
        env->ExceptionClear();
        write_connect_log("❌ Handler class not found");
        env->DeleteLocalRef(activity);
        env->DeleteLocalRef(unityPlayerClass);
        return;
    }
    
    jmethodID handlerInit = env->GetMethodID(handlerClass, "<init>", "()V");
    jobject handler = env->NewObject(handlerClass, handlerInit);
    if (handler == nullptr) {
        write_connect_log("❌ Handler creation failed");
        env->DeleteLocalRef(handlerClass);
        env->DeleteLocalRef(activity);
        env->DeleteLocalRef(unityPlayerClass);
        return;
    }
    write_connect_log("✅ Handler created");
    
    // 3. صدا زدن متد (چون Changes در Thread اصلی هست، مستقیم صدا می‌زنیم)
    typedef void (*call_func_t)(void*);
    call_func_t func = (call_func_t)method_addr;
    func(instance);
    write_connect_log("✅ Method executed on UI Thread");
    
    env->DeleteLocalRef(handler);
    env->DeleteLocalRef(handlerClass);
    env->DeleteLocalRef(activity);
    env->DeleteLocalRef(unityPlayerClass);
}

// ======================== روش ۱: StartClient (UI Thread) ========================
static void connect_method1(JNIEnv* env) {
    write_connect_log("\n========== METHOD 1: StartClient (UI Thread) ==========");
    write_connect_log("Time: " + get_time());
    write_report("🔘 Method 1: StartClient pressed");
    write_result("🔘 Method 1");
    
    try {
        std::string ip = get_ip();
        write_connect_log("📡 Target IP: " + ip);
        
        // 1. تزریق IP
        inject_ip_to_input(ip);
        write_connect_log("✅ IP injected");
        
        // 2. گرفتن NetworkManager
        void* nmInstance = get_network_manager_safe();
        if (nmInstance == nullptr) {
            write_connect_log("❌ NetworkManager not found");
            write_result("❌ NetworkManager not found");
            return;
        }
        write_connect_log("✅ NetworkManager: 0x" + std::to_string((uintptr_t)nmInstance));
        
        // 3. تنظیم networkAddress
        void* nmAddr = (void*)((uintptr_t)nmInstance + OFFSET_NETWORK_ADDR);
        if (is_valid_address(nmAddr)) {
            void** nmAddrPtr = (void**)nmAddr;
            if (nmAddrPtr != nullptr) {
                void* monoString = create_mono_string(ip.c_str());
                if (monoString != nullptr) {
                    *nmAddrPtr = monoString;
                    write_connect_log("✅ networkAddress set to: " + ip);
                }
            }
        }
        
        // 4. گرفتن آدرس StartClient
        void* startClientAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0x1AACCB4"));
        if (startClientAddr == nullptr || !is_valid_address(startClientAddr)) {
            write_connect_log("❌ StartClient address not found");
            write_result("❌ StartClient address not found");
            return;
        }
        write_connect_log("✅ StartClient address: 0x" + std::to_string((uintptr_t)startClientAddr));
        
        // 5. صدا زدن در UI Thread
        write_connect_log("🔄 Calling StartClient() on UI Thread...");
        call_on_ui_thread(env, nmInstance, startClientAddr);
        write_connect_log("✅ StartClient() called successfully");
        write_result("✅ StartClient called");
        
    } catch (const std::exception& e) {
        write_connect_log("❌ Exception: " + std::string(e.what()));
        write_report("❌ Exception: " + std::string(e.what()));
        write_crash("⚠️ Exception: " + std::string(e.what()));
    } catch (...) {
        write_connect_log("❌ Unknown exception");
        write_report("❌ Unknown exception");
        write_crash("⚠️ Unknown exception");
    }
    write_connect_log("========== METHOD 1 FINISHED ==========\n");
}

// ======================== روش ۲: LocalJoin (UI Thread) ========================
static void connect_method2(JNIEnv* env) {
    write_connect_log("\n========== METHOD 2: LocalJoin (UI Thread) ==========");
    write_connect_log("Time: " + get_time());
    write_report("🔘 Method 2: LocalJoin pressed");
    write_result("🔘 Method 2");
    
    try {
        std::string ip = get_ip();
        write_connect_log("📡 Target IP: " + ip);
        
        // 1. تزریق IP
        inject_ip_to_input(ip);
        write_connect_log("✅ IP injected");
        
        // 2. گرفتن GtaMenuControl
        void* gtaInstance = get_gta_instance_safe();
        if (gtaInstance == nullptr) {
            write_connect_log("❌ GtaMenuControl not found");
            write_result("❌ GtaMenuControl not found");
            return;
        }
        write_connect_log("✅ GtaMenuControl: 0x" + std::to_string((uintptr_t)gtaInstance));
        
        // 3. گرفتن آدرس LocalJoin
        void* localJoinAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0xF5B844"));
        if (localJoinAddr == nullptr || !is_valid_address(localJoinAddr)) {
            write_connect_log("❌ LocalJoin address not found");
            write_result("❌ LocalJoin address not found");
            return;
        }
        write_connect_log("✅ LocalJoin address: 0x" + std::to_string((uintptr_t)localJoinAddr));
        
        // 4. صدا زدن در UI Thread
        write_connect_log("🔄 Calling LocalJoin() on UI Thread...");
        call_on_ui_thread(env, gtaInstance, localJoinAddr);
        write_connect_log("✅ LocalJoin() called successfully");
        write_result("✅ LocalJoin called");
        
    } catch (const std::exception& e) {
        write_connect_log("❌ Exception: " + std::string(e.what()));
        write_report("❌ Exception: " + std::string(e.what()));
        write_crash("⚠️ Exception: " + std::string(e.what()));
    } catch (...) {
        write_connect_log("❌ Unknown exception");
        write_report("❌ Unknown exception");
        write_crash("⚠️ Unknown exception");
    }
    write_connect_log("========== METHOD 2 FINISHED ==========\n");
}

// ======================== هوک OnClientConnect ========================
void (*orig_OnClientConnect)(void *instance, void *conn);
void hook_OnClientConnect(void *instance, void *conn) {
    write_connect_log("\n========== ✅ CLIENT CONNECTED! ==========");
    write_connect_log("Time: " + get_time());
    write_report("✅ Client connected to server!");
    write_result("✅ Connected!");
    
    if (orig_OnClientConnect) {
        orig_OnClientConnect(instance, conn);
        write_connect_log("✅ Original OnClientConnect executed");
    }
}

static void install_connect_hooks() {
    if (g_connectHooksInstalled) {
        write_report("⚠️ Connect hooks already installed");
        return;
    }
    
    write_report("🔧 Installing OnClientConnect hook...");
#if defined(__aarch64__)
    void* addr = getAbsoluteAddress(targetLibName, OBFUSCATE("0xF7FA8C"));
    if (addr != nullptr && is_valid_address(addr)) {
        int res = DobbyHook(addr, (dobby_dummy_func_t)hook_OnClientConnect, (dobby_dummy_func_t*)&orig_OnClientConnect);
        if (res == 0) {
            g_connectHooksInstalled = true;
            write_report("✅ OnClientConnect hook installed");
            write_connect_log("✅ OnClientConnect hook installed");
        } else {
            write_report("❌ OnClientConnect hook failed! error: " + std::to_string(res));
            write_connect_log("❌ OnClientConnect hook failed");
        }
    } else {
        write_report("❌ OnClientConnect address not found");
        write_connect_log("❌ OnClientConnect address not found");
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
        OBFUSCATE("Button_Inject IP"),
        OBFUSCATE("Category_🔗 Connect Methods (UI Thread)"),
        OBFUSCATE("Button_Method 1: StartClient"),
        OBFUSCATE("Button_Method 2: LocalJoin"),
        OBFUSCATE("Category_🔧 Hooks"),
        OBFUSCATE("Button_Install OnClientConnect Hook"),
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
        
        case 2: {
            connect_method1(env);
            break;
        }
        
        case 3: {
            connect_method2(env);
            break;
        }
        
        case 4: {
            install_connect_hooks();
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
    
    // منتظر instance
    for (int i = 0; i < 20 && g_gtaInstance == nullptr; i++) {
        sleep(1);
    }
    
    if (g_gtaInstance != nullptr) {
        g_gtaReady = true;
        write_report("✅ GtaMenuControl ready");
        g_targetIP = get_ip();
        inject_ip_to_input(g_targetIP);
    } else {
        write_report("⚠️ GtaMenuControl not ready after 20s");
    }
    
    write_report("✅ hack done");
    write_debug("✅ hack_thread finished");
}

// ======================== تابع ورودی ========================
__attribute__((constructor))
void lib_main() {
    create_directory();
    install_crash_handler();
    write_report("🚀 lib_main called");
    write_debug("🚀 lib_main called");
    std::thread(hack_thread).detach();
}