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
#define OFFSET_NETWORK_ADDR         0x50          // NetworkManager.networkAddress
#define OFFSET_START_CLIENT         0x1AACCB4     // NetworkManager.StartClient()
#define OFFSET_START_HOST           0x1AADC24     // NetworkManager.StartHost()

// ======================== مسیرها ========================
static std::string g_basePath = "/storage/emulated/0/Download/lac/";
static std::string g_debugLog = g_basePath + "mod_debug.txt";
static std::string g_ipResultLog = g_basePath + "ip_result.txt";
static std::string g_crashLog = g_basePath + "crash_log.txt";
static std::string g_reportLog = g_basePath + "full_report.txt";
static std::string g_connectLog = g_basePath + "connect_log.txt";

static bool g_crashHandlerInstalled = false;
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

// ======================== پیدا کردن CustomNetworkManager با JNI ========================
static void* find_custom_network_manager(JNIEnv* env) {
    if (env == nullptr) {
        write_connect_log("❌ JNIEnv is null!");
        return nullptr;
    }
    
    write_connect_log("🔍 Searching for CustomNetworkManager...");
    
    try {
        // روش 1: FindObjectOfType از UnityEngine.Object
        jclass objClass = env->FindClass("UnityEngine/Object");
        if (objClass == nullptr) {
            env->ExceptionClear();
            write_connect_log("❌ UnityEngine.Object class not found");
            return nullptr;
        }
        write_connect_log("✅ UnityEngine.Object class found");
        
        jmethodID findObj = env->GetStaticMethodID(objClass, "FindObjectOfType", "(Ljava/lang/Class;)Ljava/lang/Object;");
        if (findObj == nullptr) {
            env->ExceptionClear();
            write_connect_log("❌ FindObjectOfType method not found");
            env->DeleteLocalRef(objClass);
            return nullptr;
        }
        write_connect_log("✅ FindObjectOfType method found");
        
        // پیدا کردن کلاس CustomNetworkManager
        jclass nmClass = env->FindClass("CustomNetworkManager");
        if (nmClass == nullptr) {
            env->ExceptionClear();
            write_connect_log("❌ CustomNetworkManager class not found, trying NetworkManager...");
            nmClass = env->FindClass("NetworkManager");
            if (nmClass == nullptr) {
                env->ExceptionClear();
                write_connect_log("❌ NetworkManager class not found!");
                env->DeleteLocalRef(objClass);
                return nullptr;
            }
        }
        write_connect_log("✅ CustomNetworkManager/NetworkManager class found");
        
        // صدا زدن FindObjectOfType
        jobject nmObj = env->CallStaticObjectMethod(objClass, findObj, nmClass);
        env->DeleteLocalRef(objClass);
        env->DeleteLocalRef(nmClass);
        
        if (nmObj == nullptr) {
            write_connect_log("❌ CustomNetworkManager instance not found!");
            return nullptr;
        }
        
        void* nmInstance = (void*)nmObj;
        write_connect_log("✅ CustomNetworkManager instance: 0x" + std::to_string((uintptr_t)nmInstance));
        return nmInstance;
        
    } catch (const std::exception& e) {
        write_connect_log("❌ Exception: " + std::string(e.what()));
        write_crash("⚠️ Exception: " + std::string(e.what()));
        return nullptr;
    } catch (...) {
        write_connect_log("❌ Unknown exception!");
        write_crash("⚠️ Unknown exception!");
        return nullptr;
    }
}

// ======================== پیدا کردن CustomNetworkManager با KittyMemory ========================
static void* find_custom_network_manager_memory() {
    write_connect_log("🔍 Searching for CustomNetworkManager in memory...");
    
    auto maps = KittyMemory::getAllMaps();
    for (auto &map : maps) {
        if (!map.readable) continue;
        // جستجوی الگوی CustomNetworkManager
        uintptr_t found = KittyScanner::findDataFirst(map.startAddress, map.endAddress, "CustomNetworkManager", strlen("CustomNetworkManager"));
        if (found != 0) {
            write_connect_log("✅ Found 'CustomNetworkManager' string at: 0x" + std::to_string(found));
            // پیدا کردن instance نزدیک آن (معمولاً در 0x20-0x100 بعد از string)
            for (uintptr_t addr = found - 0x100; addr < found + 0x100; addr += 8) {
                void* potential = *(void**)addr;
                if (potential != nullptr && is_valid_address(potential)) {
                    // امتحان کردن با جستجوی اسم کلاس
                    // اینجا نمیتونیم بفهمیم دقیقاً CustomNetworkManager هست یا نه
                    // پس بهتره از این روش صرف نظر کنیم
                }
            }
        }
    }
    return nullptr;
}

// ======================== اتصال به سرور با CustomNetworkManager ========================
static void connect_to_server(JNIEnv* env) {
    write_connect_log("\n========== CONNECTING TO SERVER ==========");
    write_connect_log("Time: " + get_time());
    write_report("🔘 Connect button pressed");
    write_result("🔘 Connect button pressed");
    
    try {
        // 1. بارگذاری IP
        if (g_targetIP.empty()) {
            g_targetIP = load_ip();
            if (g_targetIP.empty()) {
                g_targetIP = std::string(DEFAULT_IP) + ":" + std::string(DEFAULT_PORT);
                save_ip(g_targetIP);
                write_connect_log("📌 Using default IP: " + g_targetIP);
            }
        }
        write_connect_log("📡 Target IP: " + g_targetIP);
        write_report("📡 Target IP: " + g_targetIP);
        
        // 2. پیدا کردن CustomNetworkManager
        void* nmInstance = find_custom_network_manager(env);
        if (nmInstance == nullptr) {
            write_connect_log("❌ CustomNetworkManager not found!");
            write_report("❌ CustomNetworkManager not found!");
            write_result("❌ CustomNetworkManager not found!");
            return;
        }
        write_connect_log("✅ CustomNetworkManager instance: 0x" + std::to_string((uintptr_t)nmInstance));
        
        // 3. تنظیم networkAddress
        void* nmAddr = (void*)((uintptr_t)nmInstance + OFFSET_NETWORK_ADDR);
        void** nmAddrPtr = (void**)nmAddr;
        if (nmAddrPtr == nullptr || !is_valid_address(nmAddrPtr)) {
            write_connect_log("❌ networkAddress pointer invalid");
            return;
        }
        
        typedef void* (*il2cpp_string_new_t)(const char*);
        il2cpp_string_new_t il2cpp_string_new = 
            (il2cpp_string_new_t)getAbsoluteAddress("libil2cpp.so", "il2cpp_string_new");
        if (il2cpp_string_new == nullptr) {
            write_connect_log("❌ il2cpp_string_new not found");
            return;
        }
        
        void* monoString = il2cpp_string_new(g_targetIP.c_str());
        if (monoString == nullptr) {
            write_connect_log("❌ Failed to create mono string");
            return;
        }
        
        *nmAddrPtr = monoString;
        write_connect_log("✅ networkAddress set to: " + g_targetIP);
        write_report("✅ networkAddress set to: " + g_targetIP);
        
        // 4. صدا زدن StartClient
        void* startClientAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0x1AACCB4"));
        if (startClientAddr == nullptr || !is_valid_address(startClientAddr)) {
            write_connect_log("❌ StartClient address not found!");
            write_report("❌ StartClient address not found!");
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
            // IP رو توی فیلد ذخیره کن (برای Connect)
            save_ip(g_targetIP);
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

    // ====== بارگذاری IP ======
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