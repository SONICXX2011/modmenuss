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

// ======================== آفست‌ها ========================
#define OFFSET_GET_INSTANCE         0xF570C4
#define OFFSET_GTA_START            0xF571A4
#define OFFSET_IP_INPUT             0xC0
#define OFFSET_INPUTFIELD_M_TEXT    0x180
#define OFFSET_NETWORK_MANAGER      0x258
#define OFFSET_NETWORK_ADDR         0x50
#define OFFSET_LOCAL_JOIN           0xF5B844

// ======================== مسیرها ========================
static std::string g_basePath = "/storage/emulated/0/Download/lac/";
static std::string g_debugLog = g_basePath + "mod_debug.txt";
static std::string g_ipResultLog = g_basePath + "ip_result.txt";
static std::string g_crashLog = g_basePath + "crash_log.txt";
static std::string g_reportLog = g_basePath + "full_report.txt";
static std::string g_connectLog = g_basePath + "connect_log.txt";

static bool g_crashHandlerInstalled = false;
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
    if (il2cpp_string_new == nullptr) return nullptr;
    return il2cpp_string_new(str);
}

// ======================== گرفتن GtaMenuControl ========================
static void* get_gta_instance() {
    if (g_gtaInstance != nullptr && is_valid_address(g_gtaInstance)) {
        return g_gtaInstance;
    }
    
    typedef void* (*get_instance_t)();
    get_instance_t get_Instance = (get_instance_t)getAbsoluteAddress("libil2cpp.so", "GtaMenuControl.get_Instance");
    if (get_Instance == nullptr) return nullptr;
    
    void* instance = get_Instance();
    if (instance != nullptr && is_valid_address(instance)) {
        g_gtaInstance = instance;
        return instance;
    }
    return nullptr;
}

// ======================== تزریق IP ========================
static void inject_ip_to_input(const std::string& ip) {
    void* instance = get_gta_instance();
    if (instance == nullptr) return;
    
    void* ipInput = *(void**)((uintptr_t)instance + OFFSET_IP_INPUT);
    if (ipInput == nullptr || !is_valid_address(ipInput)) return;
    
    void** mTextPtr = (void**)((uintptr_t)ipInput + OFFSET_INPUTFIELD_M_TEXT);
    if (mTextPtr == nullptr) return;
    
    void* monoString = create_mono_string(ip.c_str());
    if (monoString == nullptr) return;
    
    *mTextPtr = monoString;
    write_report("✅ IP injected: " + ip);
    write_result("✅ IP injected: " + ip);
}

// ======================== ======== اتصال با JNI (بدون کرش) ======== ========================
static void connect_with_jni(JNIEnv* env) {
    if (env == nullptr) {
        write_connect_log("❌ JNIEnv is null!");
        return;
    }
    
    write_connect_log("\n========== CONNECT WITH JNI ==========");
    write_connect_log("Time: " + get_time());
    write_report("🔘 Connect (JNI) button pressed");
    write_result("🔘 Connect (JNI) button pressed");
    
    try {
        // 1. گرفتن IP
        std::string ip = get_ip();
        write_connect_log("📡 Target IP: " + ip);
        write_report("📡 Target IP: " + ip);
        
        // 2. پیدا کردن GtaMenuControl با ClassLoader
        jclass unityPlayerClass = env->FindClass("com/unity3d/player/UnityPlayer");
        if (unityPlayerClass == nullptr) {
            env->ExceptionClear();
            write_connect_log("❌ UnityPlayer class not found!");
            return;
        }
        
        jfieldID currentActivityField = env->GetStaticFieldID(unityPlayerClass, "currentActivity", "Landroid/app/Activity;");
        jobject activity = env->GetStaticObjectField(unityPlayerClass, currentActivityField);
        if (activity == nullptr) {
            write_connect_log("❌ Activity is null!");
            env->DeleteLocalRef(unityPlayerClass);
            return;
        }
        write_connect_log("✅ Activity obtained");
        
        jclass activityCls = env->GetObjectClass(activity);
        jmethodID getClassLoader = env->GetMethodID(activityCls, "getClassLoader", "()Ljava/lang/ClassLoader;");
        jobject classLoader = env->CallObjectMethod(activity, getClassLoader);
        if (classLoader == nullptr) {
            write_connect_log("❌ ClassLoader is null!");
            env->DeleteLocalRef(activityCls);
            env->DeleteLocalRef(activity);
            env->DeleteLocalRef(unityPlayerClass);
            return;
        }
        write_connect_log("✅ ClassLoader obtained");
        
        jclass classLoaderCls = env->FindClass("java/lang/ClassLoader");
        jmethodID loadClass = env->GetMethodID(classLoaderCls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
        jstring className = env->NewStringUTF("GtaMenuControl");
        jclass gtaClass = (jclass)env->CallObjectMethod(classLoader, loadClass, className);
        env->DeleteLocalRef(className);
        
        if (gtaClass == nullptr) {
            env->ExceptionClear();
            write_connect_log("❌ GtaMenuControl class not found!");
            env->DeleteLocalRef(classLoaderCls);
            env->DeleteLocalRef(classLoader);
            env->DeleteLocalRef(activityCls);
            env->DeleteLocalRef(activity);
            env->DeleteLocalRef(unityPlayerClass);
            return;
        }
        write_connect_log("✅ GtaMenuControl class found");
        
        // 3. گرفتن instance با get_Instance
        jmethodID getInstance = env->GetStaticMethodID(gtaClass, "get_Instance", "()LGtaMenuControl;");
        if (getInstance == nullptr) {
            env->ExceptionClear();
            write_connect_log("❌ get_Instance method not found!");
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
            write_connect_log("❌ GtaMenuControl instance is null!");
            env->DeleteLocalRef(gtaClass);
            env->DeleteLocalRef(classLoaderCls);
            env->DeleteLocalRef(classLoader);
            env->DeleteLocalRef(activityCls);
            env->DeleteLocalRef(activity);
            env->DeleteLocalRef(unityPlayerClass);
            return;
        }
        write_connect_log("✅ GtaMenuControl instance obtained");
        
        // 4. تنظیم IP در ipInput (از طریق JNI)
        jfieldID ipInputField = env->GetFieldID(gtaClass, "ipInput", "LUnityEngine/UI/InputField;");
        if (ipInputField == nullptr) {
            env->ExceptionClear();
            write_connect_log("⚠️ ipInput field not found, skipping");
        } else {
            jobject ipInput = env->GetObjectField(gtaObj, ipInputField);
            if (ipInput != nullptr) {
                // پیدا کردن InputField.m_Text
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
        }
        
        // 5. صدا زدن LocalJoin
        jmethodID localJoinMethod = env->GetMethodID(gtaClass, "LocalJoin", "()V");
        if (localJoinMethod == nullptr) {
            env->ExceptionClear();
            write_connect_log("❌ LocalJoin method not found!");
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
            write_connect_log("⚠️ Exception during LocalJoin call!");
            write_report("⚠️ Exception during LocalJoin call!");
        } else {
            write_connect_log("✅ LocalJoin() called successfully!");
            write_report("✅ LocalJoin() called successfully!");
            write_result("✅ Connected!");
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
    write_connect_log("========== CONNECT FINISHED ==========\n");
}

// ======================== هوک GtaMenuControl.Start ========================
void (*orig_GtaMenuStart)(void *instance);
void hook_GtaMenuStart(void *instance) {
    if (instance != nullptr && is_valid_address(instance)) {
        g_gtaInstance = instance;
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
        OBFUSCATE("Button_Connect (JNI)"),
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
            connect_with_jni(env);
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
    
#if defined(__aarch64__)
    void* startAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0xF571A4"));
    if (startAddr != nullptr) {
        DobbyHook(startAddr, (dobby_dummy_func_t)hook_GtaMenuStart, (dobby_dummy_func_t*)&orig_GtaMenuStart);
        write_report("✅ GtaMenuControl.Start() hooked");
    }
#endif
    
    for (int i = 0; i < 20 && g_gtaInstance == nullptr; i++) sleep(1);
    
    g_targetIP = load_ip();
    if (g_targetIP.empty()) {
        g_targetIP = std::string(DEFAULT_IP) + ":" + std::string(DEFAULT_PORT);
        save_ip(g_targetIP);
        write_report("📌 Default IP set: " + g_targetIP);
    }
    
    write_report("✅ hack done");
}

__attribute__((constructor))
void lib_main() {
    create_directory();
    install_crash_handler();
    write_report("🚀 lib_main called");
    std::thread(hack_thread).detach();
}