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
#define OFFSET_LOCAL_JOIN           0xF5B844      // GtaMenuControl.LocalJoin()
#define OFFSET_IP_INPUT             0xC0          // GtaMenuControl.ipInput
#define OFFSET_INPUTFIELD_M_TEXT    0x180         // InputField.m_Text

// ======================== مسیرها ========================
static std::string g_basePath = "/storage/emulated/0/Download/lac/";
static std::string g_debugLog = g_basePath + "mod_debug.txt";
static std::string g_ipResultLog = g_basePath + "ip_result.txt";
static std::string g_crashLog = g_basePath + "crash_log.txt";
static std::string g_reportLog = g_basePath + "full_report.txt";
static std::string g_connectLog = g_basePath + "connect_log.txt";
static std::string g_jniLog = g_basePath + "jni_log.txt";

// ======================== متغیرها ========================
static bool g_crashHandlerInstalled = false;
static void* g_gtaInstance = nullptr;
static bool g_gtaReady = false;
static bool g_hookInstalled = false;

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

static void write_debug(const std::string& msg) {
    write_log(g_debugLog, msg);
    LOGI("[Debug] %s", msg.c_str());
}

static void write_connect_log(const std::string& msg) {
    write_log(g_connectLog, msg);
}

static void write_jni_log(const std::string& msg) {
    write_log(g_jniLog, msg);
}

static void write_result(const std::string& msg) {
    write_log(g_ipResultLog, msg);
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
    write_debug("✅ Crash handler installed");
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
    
    if (il2cpp_string_length == nullptr) {
        write_debug("❌ il2cpp_string_length not found");
        return "";
    }
    
    int length = il2cpp_string_length(monoString);
    if (length <= 0 || length > 65536) {
        write_debug("❌ Invalid string length: " + std::to_string(length));
        return "";
    }
    
    typedef uint16_t* (*il2cpp_string_chars_t)(void* str);
    il2cpp_string_chars_t il2cpp_string_chars = 
        (il2cpp_string_chars_t)getAbsoluteAddress("libil2cpp.so", "il2cpp_string_chars");
    
    if (il2cpp_string_chars == nullptr) {
        write_debug("❌ il2cpp_string_chars not found");
        return "";
    }
    
    uint16_t* chars = il2cpp_string_chars(monoString);
    if (chars == nullptr || !is_valid_address(chars)) {
        write_debug("❌ Cannot read string chars");
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
static void* get_gta_instance() {
    if (g_gtaInstance != nullptr && is_valid_address(g_gtaInstance)) {
        return g_gtaInstance;
    }
    
    typedef void* (*get_instance_t)();
    get_instance_t get_Instance = (get_instance_t)getAbsoluteAddress("libil2cpp.so", "GtaMenuControl.get_Instance");
    
    if (get_Instance != nullptr) {
        void* instance = get_Instance();
        if (instance != nullptr && is_valid_address(instance)) {
            g_gtaInstance = instance;
            write_debug("✅ Got GtaMenuControl instance via get_Instance: 0x" + std::to_string((uintptr_t)instance));
            return instance;
        }
    }
    
    return nullptr;
}

// ======================== گرفتن IP از ipInput ========================
static std::string get_ip_from_input() {
    void* instance = get_gta_instance();
    if (instance == nullptr) {
        write_debug("❌ Cannot get GtaMenuControl instance for IP");
        return "";
    }
    
    void* ipInput = *(void**)((uintptr_t)instance + OFFSET_IP_INPUT);
    if (ipInput == nullptr || !is_valid_address(ipInput)) {
        write_debug("❌ ipInput is null or invalid");
        return "";
    }
    
    void** mTextPtr = (void**)((uintptr_t)ipInput + OFFSET_INPUTFIELD_M_TEXT);
    if (mTextPtr == nullptr || !is_valid_address(mTextPtr)) {
        write_debug("❌ m_Text pointer is invalid");
        return "";
    }
    
    void* monoString = *mTextPtr;
    if (monoString == nullptr) {
        write_debug("❌ MonoString is null");
        return "";
    }
    
    return mono_string_to_utf8(monoString);
}

// ======================== روش 1: هوک روی LocalJoin (برای لاگ گرفتن) ========================
void (*orig_LocalJoin)(void *instance);
void hook_LocalJoin(void *instance) {
    std::string currentTime = get_time();
    std::string ip = get_ip_from_input();
    
    write_connect_log("========== [HOOK] LocalJoin TRIGGERED ==========");
    write_connect_log("Time: " + currentTime);
    write_connect_log("📡 IP: " + ip);
    write_report("🟢 [HOOK] LocalJoin called - IP: " + ip);
    write_result("🟢 [HOOK] LocalJoin called - IP: " + ip);
    __android_log_print(ANDROID_LOG_INFO, "LAC_Mod", "🟢 [HOOK] LocalJoin called - IP: %s", ip.c_str());
    
    if (orig_LocalJoin) {
        orig_LocalJoin(instance);
        write_connect_log("✅ Original LocalJoin executed");
        __android_log_print(ANDROID_LOG_INFO, "LAC_Mod", "✅ Original LocalJoin executed");
    } else {
        write_connect_log("❌ orig_LocalJoin is null!");
        __android_log_print(ANDROID_LOG_ERROR, "LAC_Mod", "❌ orig_LocalJoin is null!");
    }
    
    write_connect_log("========== [HOOK] FINISHED ==========\n");
}

// ======================== نصب هوک LocalJoin ========================
static void install_hook() {
    if (g_hookInstalled) {
        write_report("⚠️ Hook already installed");
        write_connect_log("⚠️ Hook already installed");
        return;
    }
    
    write_report("🔧 Installing LocalJoin hook...");
    write_connect_log("🔧 Installing LocalJoin hook...");
    
#if defined(__aarch64__)
    void* localJoinAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0xF5B844"));
    if (localJoinAddr != nullptr && is_valid_address(localJoinAddr)) {
        int res = DobbyHook(localJoinAddr, (dobby_dummy_func_t)hook_LocalJoin, (dobby_dummy_func_t*)&orig_LocalJoin);
        if (res == 0) {
            g_hookInstalled = true;
            write_report("✅ LocalJoin() hooked at 0x" + std::to_string((uintptr_t)localJoinAddr));
            write_connect_log("✅ Hook installed at 0x" + std::to_string((uintptr_t)localJoinAddr));
            __android_log_print(ANDROID_LOG_INFO, "LAC_Mod", "✅ LocalJoin hook installed!");
        } else {
            write_report("❌ Hook failed! error: " + std::to_string(res));
            write_connect_log("❌ Hook failed! error: " + std::to_string(res));
            __android_log_print(ANDROID_LOG_ERROR, "LAC_Mod", "❌ LocalJoin hook failed!");
        }
    } else {
        write_report("❌ LocalJoin address not found!");
        write_connect_log("❌ LocalJoin address not found!");
        __android_log_print(ANDROID_LOG_ERROR, "LAC_Mod", "❌ LocalJoin address not found!");
    }
#else
    write_report("❌ Not ARM64 architecture!");
    write_connect_log("❌ Not ARM64 architecture!");
#endif
}

// ======================== روش 2: صدا زدن LocalJoin از طریق JNI ========================
static void call_local_join_jni(JNIEnv* env) {
    if (env == nullptr) {
        write_jni_log("❌ JNIEnv is null!");
        write_report("❌ JNIEnv is null!");
        write_result("❌ JNIEnv is null!");
        return;
    }
    
    write_jni_log("\n========== CALL LOCALJOIN VIA JNI ==========");
    write_jni_log("Time: " + get_time());
    write_connect_log("\n========== CALL LOCALJOIN VIA JNI ==========");
    write_connect_log("Time: " + get_time());
    write_report("🔘 Call LocalJoin (JNI) pressed");
    write_result("🔘 Call LocalJoin (JNI) pressed");
    
    try {
        // 1. گرفتن IP
        std::string ip = get_ip_from_input();
        write_jni_log("📡 Target IP: " + ip);
        write_connect_log("📡 Target IP: " + ip);
        write_report("📡 Target IP: " + ip);
        
        // 2. پیدا کردن کلاس GtaMenuControl
        jclass gtaClass = env->FindClass("GtaMenuControl");
        if (gtaClass == nullptr) {
            env->ExceptionClear();
            write_jni_log("❌ GtaMenuControl class not found!");
            write_connect_log("❌ GtaMenuControl class not found!");
            write_report("❌ GtaMenuControl class not found!");
            return;
        }
        write_jni_log("✅ GtaMenuControl class found");
        write_connect_log("✅ GtaMenuControl class found");
        
        // 3. پیدا کردن متد get_Instance (static)
        jmethodID getInstance = env->GetStaticMethodID(gtaClass, "get_Instance", "()LGtaMenuControl;");
        if (getInstance == nullptr) {
            env->ExceptionClear();
            write_jni_log("❌ get_Instance method not found!");
            write_connect_log("❌ get_Instance method not found!");
            write_report("❌ get_Instance method not found!");
            return;
        }
        write_jni_log("✅ get_Instance method found");
        
        // 4. گرفتن instance
        jobject gtaObj = env->CallStaticObjectMethod(gtaClass, getInstance);
        if (gtaObj == nullptr) {
            write_jni_log("❌ GtaMenuControl instance is null!");
            write_connect_log("❌ GtaMenuControl instance is null!");
            write_report("❌ GtaMenuControl instance is null!");
            return;
        }
        write_jni_log("✅ GtaMenuControl instance obtained");
        
        // 5. پیدا کردن متد LocalJoin
        jmethodID localJoin = env->GetMethodID(gtaClass, "LocalJoin", "()V");
        if (localJoin == nullptr) {
            env->ExceptionClear();
            write_jni_log("❌ LocalJoin method not found!");
            write_connect_log("❌ LocalJoin method not found!");
            write_report("❌ LocalJoin method not found!");
            env->DeleteLocalRef(gtaObj);
            return;
        }
        write_jni_log("✅ LocalJoin method found");
        
        // 6. صدا زدن LocalJoin
        write_jni_log("🔄 Calling LocalJoin() via JNI...");
        write_connect_log("🔄 Calling LocalJoin() via JNI...");
        write_report("🔄 Calling LocalJoin() via JNI...");
        env->CallVoidMethod(gtaObj, localJoin);
        
        // 7. چک کردن Exception
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            write_jni_log("⚠️ Exception during LocalJoin call!");
            write_connect_log("⚠️ Exception during LocalJoin call!");
            write_report("⚠️ Exception during LocalJoin call!");
        } else {
            write_jni_log("✅ LocalJoin() called successfully via JNI!");
            write_connect_log("✅ LocalJoin() called successfully via JNI!");
            write_report("✅ LocalJoin() called successfully via JNI!");
            write_result("✅ LocalJoin called via JNI");
        }
        
        env->DeleteLocalRef(gtaObj);
        env->DeleteLocalRef(gtaClass);
        
    } catch (const std::exception& e) {
        write_jni_log("❌ Exception: " + std::string(e.what()));
        write_connect_log("❌ Exception: " + std::string(e.what()));
        write_report("❌ Exception: " + std::string(e.what()));
        write_log(g_crashLog, "⚠️ JNI Exception: " + std::string(e.what()));
    } catch (...) {
        write_jni_log("❌ Unknown exception!");
        write_connect_log("❌ Unknown exception!");
        write_report("❌ Unknown exception!");
        write_log(g_crashLog, "⚠️ Unknown JNI exception!");
    }
    
    write_jni_log("========== JNI CALL FINISHED ==========\n");
    write_connect_log("========== JNI CALL FINISHED ==========\n");
}

// ======================== روش 3: تست همه روش‌ها ========================
static void test_all_methods(JNIEnv* env) {
    write_connect_log("\n\n========== TEST ALL METHODS ==========");
    write_connect_log("Time: " + get_time());
    write_report("\n🔘 TEST ALL METHODS pressed");
    write_result("🔘 TEST ALL METHODS pressed");
    
    write_connect_log("\n--- Method 1: Hook ---");
    if (g_hookInstalled) {
        write_connect_log("✅ Hook is already installed. Click Connect button in game to test.");
        write_report("✅ Hook is installed. Click Connect button in game to test.");
    } else {
        write_connect_log("⚠️ Hook not installed. Installing...");
        install_hook();
    }
    
    write_connect_log("\n--- Method 2: Call via JNI ---");
    call_local_join_jni(env);
    
    write_connect_log("\n✅ All tests completed!");
    write_report("✅ All tests completed!");
    write_result("✅ All tests completed!");
    write_connect_log("========== TEST ALL FINISHED ==========\n");
}

// ======================== هوک روی GtaMenuControl.Start (برای گرفتن instance) ========================
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
        OBFUSCATE("Category_🔗 Connection Tests"),
        OBFUSCATE("Button_1. Install Hook (LocalJoin)"),
        OBFUSCATE("Button_2. Call LocalJoin (JNI)"),
        OBFUSCATE("Button_3. TEST ALL METHODS"),
        OBFUSCATE("RichTextView_📁 Logs: /sdcard/Download/lac/"),
        OBFUSCATE("RichTextView_   - connect_log.txt (Hook & Call)"),
        OBFUSCATE("RichTextView_   - jni_log.txt (JNI Details)"),
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
        case 0:
            write_report("🔘 Install Hook pressed");
            write_result("🔘 Install Hook pressed");
            install_hook();
            break;
            
        case 1:
            write_report("🔘 Call LocalJoin (JNI) pressed");
            write_result("🔘 Call LocalJoin (JNI) pressed");
            call_local_join_jni(env);
            break;
            
        case 2:
            write_report("🔘 TEST ALL METHODS pressed");
            write_result("🔘 TEST ALL METHODS pressed");
            test_all_methods(env);
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
    // ====== نصب هوک روی Start ======
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
    for (int i = 0; i < 20 && g_gtaInstance == nullptr; i++) {
        sleep(1);
    }
    
    if (g_gtaInstance != nullptr) {
        write_report("✅ Got GtaMenuControl instance after wait");
    } else {
        g_gtaInstance = get_gta_instance();
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