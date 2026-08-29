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
#define OFFSET_GTA_CHAR_SELECT      0x40          // GtaMenuControl._charSelect
#define OFFSET_CHAR_CURRENT         0x38          // GtaCharacterSelect.currentChar
#define OFFSET_CHAR_NAME_INPUT      0x48          // GtaCharacterSelect.nameInput
#define OFFSET_INPUTFIELD_M_TEXT    0x180         // InputField.m_Text
#define OFFSET_LOCAL_JOIN           0xF5B844      // GtaMenuControl.LocalJoin()
#define OFFSET_LOCAL_HOST           0xF5B7E0      // GtaMenuControl.LocalHost()
#define OFFSET_IP_INPUT             0xC0          // GtaMenuControl.ipInput
#define OFFSET_NETWORK_ADDR         0x50          // NetworkManager.networkAddress
#define OFFSET_START_CLIENT         0x1AACCB4     // NetworkManager.StartClient()

// ======================== مسیرها ========================
static std::string g_basePath = "/storage/emulated/0/Download/lac/";
static std::string g_debugLog = g_basePath + "mod_debug.txt";
static std::string g_ipResultLog = g_basePath + "ip_result.txt";
static std::string g_crashLog = g_basePath + "crash_log.txt";
static std::string g_reportLog = g_basePath + "full_report.txt";
static std::string g_playerInfoLog = g_basePath + "player_info.txt";
static std::string g_connectLog = g_basePath + "connect_log.txt";
static std::string g_hookLog = g_basePath + "hook_log.txt";
static std::string g_callLog = g_basePath + "call_log.txt";
static std::string g_networkLog = g_basePath + "network_log.txt";

// ======================== متغیرها ========================
static bool g_crashHandlerInstalled = false;
static void* g_gtaInstance = nullptr;
static bool g_gtaReady = false;
static bool g_hookInstalled = false;
static bool g_isConnecting = false;

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

static void write_player_info(const std::string& msg) {
    write_log(g_playerInfoLog, msg);
}

static void write_connect_log(const std::string& msg) {
    write_log(g_connectLog, msg);
}

static void write_hook_log(const std::string& msg) {
    write_log(g_hookLog, msg);
}

static void write_call_log(const std::string& msg) {
    write_log(g_callLog, msg);
}

static void write_network_log(const std::string& msg) {
    write_log(g_networkLog, msg);
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
        write_report("   ❌ il2cpp_string_length not found!");
        return "";
    }
    
    int length = il2cpp_string_length(monoString);
    if (length <= 0 || length > 65536) {
        write_report("   ❌ Invalid string length: " + std::to_string(length));
        return "";
    }
    
    typedef uint16_t* (*il2cpp_string_chars_t)(void* str);
    il2cpp_string_chars_t il2cpp_string_chars = 
        (il2cpp_string_chars_t)getAbsoluteAddress("libil2cpp.so", "il2cpp_string_chars");
    
    if (il2cpp_string_chars == nullptr) {
        write_report("   ❌ il2cpp_string_chars not found!");
        return "";
    }
    
    uint16_t* chars = il2cpp_string_chars(monoString);
    if (chars == nullptr || !is_valid_address(chars)) {
        write_report("   ❌ Cannot read string chars!");
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
static void* get_gta_menu_instance() {
    typedef void* (*get_instance_t)();
    get_instance_t get_Instance = (get_instance_t)getAbsoluteAddress("libil2cpp.so", "GtaMenuControl.get_Instance");
    
    if (get_Instance == nullptr) {
        write_report("❌ GtaMenuControl.get_Instance not found!");
        return nullptr;
    }
    
    void* instance = get_Instance();
    if (instance != nullptr && is_valid_address(instance)) {
        write_report("✅ GtaMenuControl instance: 0x" + std::to_string((uintptr_t)instance));
        return instance;
    }
    
    return nullptr;
}

// ======================== گرفتن IP از ipInput ========================
static std::string get_ip_from_input() {
    if (g_gtaInstance == nullptr || !is_valid_address(g_gtaInstance)) {
        g_gtaInstance = get_gta_menu_instance();
        if (g_gtaInstance == nullptr) {
            write_report("❌ Cannot get GtaMenuControl instance!");
            return "";
        }
    }
    
    void* ipInput = *(void**)((uintptr_t)g_gtaInstance + OFFSET_IP_INPUT);
    if (ipInput == nullptr || !is_valid_address(ipInput)) {
        write_report("❌ ipInput is null!");
        return "";
    }
    
    void** mTextPtr = (void**)((uintptr_t)ipInput + OFFSET_INPUTFIELD_M_TEXT);
    if (mTextPtr == nullptr || !is_valid_address(mTextPtr)) {
        write_report("❌ m_Text pointer is invalid!");
        return "";
    }
    
    void* monoString = *mTextPtr;
    if (monoString == nullptr) {
        write_report("❌ MonoString is null!");
        return "";
    }
    
    return mono_string_to_utf8(monoString);
}

// ======================== ======================== متدهای تست ======================== ========================

// ======================== روش 1: هوک روی LocalJoin ========================
void (*orig_LocalJoin)(void *instance);
void hook_LocalJoin(void *instance) {
    std::string currentTime = get_time();
    std::string ip = get_ip_from_input();
    
    // ====== لاگ ======
    __android_log_print(ANDROID_LOG_INFO, "LAC_Mod", 
        "🟢 HOOK: LocalJoin() CALLED at %s", currentTime.c_str());
    
    std::string logMsg = "🟢 HOOK: LocalJoin() CALLED at " + currentTime + " | IP: " + ip;
    write_hook_log("========== HOOK TRIGGERED ==========");
    write_hook_log(logMsg);
    write_hook_log("   📡 IP: " + ip);
    write_report("🟢 [HOOK] LocalJoin called - IP: " + ip);
    write_connect_log("🟢 [HOOK] LocalJoin called - IP: " + ip);
    
    // ====== اجرای تابع اصلی ======
    if (orig_LocalJoin) {
        orig_LocalJoin(instance);
        write_hook_log("   ✅ Original LocalJoin() executed");
        __android_log_print(ANDROID_LOG_INFO, "LAC_Mod", "   ✅ Original LocalJoin executed");
    } else {
        write_hook_log("   ❌ orig_LocalJoin is null!");
        __android_log_print(ANDROID_LOG_ERROR, "LAC_Mod", "   ❌ orig_LocalJoin is null!");
    }
    
    write_hook_log("========== HOOK FINISHED ==========\n");
}

// ======================== نصب هوک ========================
static bool install_hook() {
    if (g_hookInstalled) {
        write_report("⚠️ Hook already installed");
        return true;
    }
    
    write_report("🔧 Installing LocalJoin hook...");
    
#if defined(__aarch64__)
    void* localJoinAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0xF5B844"));
    if (localJoinAddr != nullptr && is_valid_address(localJoinAddr)) {
        int res = DobbyHook(localJoinAddr, (dobby_dummy_func_t)hook_LocalJoin, (dobby_dummy_func_t*)&orig_LocalJoin);
        if (res == 0) {
            g_hookInstalled = true;
            write_report("✅ LocalJoin() hooked at 0x" + std::to_string((uintptr_t)localJoinAddr));
            write_hook_log("✅ Hook installed successfully at 0x" + std::to_string((uintptr_t)localJoinAddr));
            __android_log_print(ANDROID_LOG_INFO, "LAC_Mod", "✅ LocalJoin hook installed!");
            return true;
        } else {
            write_report("❌ Failed to hook LocalJoin()! error: " + std::to_string(res));
            write_hook_log("❌ Hook failed! error: " + std::to_string(res));
            __android_log_print(ANDROID_LOG_ERROR, "LAC_Mod", "❌ LocalJoin hook failed!");
            return false;
        }
    } else {
        write_report("❌ LocalJoin() address not found!");
        write_hook_log("❌ LocalJoin() address not found!");
        __android_log_print(ANDROID_LOG_ERROR, "LAC_Mod", "❌ LocalJoin address not found!");
        return false;
    }
#else
    write_report("❌ Not ARM64 architecture!");
    return false;
#endif
}

// ======================== روش 2: صدا زدن LocalJoin ========================
static void call_local_join() {
    write_report("\n========== CALL LOCALJOIN ==========");
    write_call_log("========== CALL LOCALJOIN ==========");
    write_call_log("Time: " + get_time());
    
    try {
        if (g_gtaInstance == nullptr || !is_valid_address(g_gtaInstance)) {
            g_gtaInstance = get_gta_menu_instance();
            if (g_gtaInstance == nullptr) {
                write_report("❌ Cannot get GtaMenuControl instance!");
                write_call_log("❌ Cannot get GtaMenuControl instance!");
                return;
            }
        }
        
        std::string ip = get_ip_from_input();
        write_report("📡 Target IP: " + ip);
        write_call_log("📡 Target IP: " + ip);
        
        typedef void (*local_join_t)(void* instance);
        local_join_t LocalJoin = (local_join_t)getAbsoluteAddress("libil2cpp.so", "GtaMenuControl.LocalJoin");
        
        if (LocalJoin == nullptr) {
            write_report("❌ LocalJoin function not found!");
            write_call_log("❌ LocalJoin function not found!");
            return;
        }
        
        write_report("🔄 Calling LocalJoin()...");
        write_call_log("🔄 Calling LocalJoin()...");
        LocalJoin(g_gtaInstance);
        write_report("✅ LocalJoin() called successfully!");
        write_call_log("✅ LocalJoin() called successfully!");
        write_connect_log("🟢 [CALL] LocalJoin called - IP: " + ip);
        
    } catch (const std::exception& e) {
        write_report("❌ Exception: " + std::string(e.what()));
        write_call_log("❌ Exception: " + std::string(e.what()));
    } catch (...) {
        write_report("❌ Unknown exception!");
        write_call_log("❌ Unknown exception!");
    }
    
    write_call_log("========== CALL FINISHED ==========\n");
}

// ======================== روش 3: اتصال از طریق NetworkManager ========================
static void connect_via_network_manager(JNIEnv* env) {
    write_report("\n========== CONNECT VIA NETWORKMANAGER ==========");
    write_network_log("========== CONNECT VIA NETWORKMANAGER ==========");
    write_network_log("Time: " + get_time());
    
    if (env == nullptr) {
        write_report("❌ JNIEnv is null!");
        write_network_log("❌ JNIEnv is null!");
        return;
    }
    
    try {
        std::string ip = get_ip_from_input();
        if (ip.empty()) {
            write_report("❌ IP is empty!");
            write_network_log("❌ IP is empty!");
            return;
        }
        write_report("📡 Target IP: " + ip);
        write_network_log("📡 Target IP: " + ip);
        
        // ====== پیدا کردن NetworkManager ======
        jclass nmClass = env->FindClass("CustomNetworkManager");
        if (nmClass == nullptr) {
            env->ExceptionClear();
            nmClass = env->FindClass("NetworkManager");
            if (nmClass == nullptr) {
                env->ExceptionClear();
                write_report("❌ NetworkManager class not found!");
                write_network_log("❌ NetworkManager class not found!");
                return;
            }
        }
        write_report("✅ NetworkManager class found");
        write_network_log("✅ NetworkManager class found");
        
        // ====== پیدا کردن instance ======
        jclass objClass = env->FindClass("UnityEngine.Object");
        jmethodID findObj = env->GetStaticMethodID(objClass, "FindObjectOfType", "(Ljava/lang/Class;)Ljava/lang/Object;");
        jobject nm = env->CallStaticObjectMethod(objClass, findObj, nmClass);
        
        if (nm == nullptr) {
            write_report("❌ NetworkManager instance not found!");
            write_network_log("❌ NetworkManager instance not found!");
            return;
        }
        write_report("✅ NetworkManager instance found");
        write_network_log("✅ NetworkManager instance found");
        
        // ====== تغییر networkAddress ======
        jfieldID addrField = env->GetFieldID(nmClass, "networkAddress", "Ljava/lang/String;");
        jstring jip = env->NewStringUTF(ip.c_str());
        env->SetObjectField(nm, addrField, jip);
        env->DeleteLocalRef(jip);
        write_report("✅ networkAddress set to: " + ip);
        write_network_log("✅ networkAddress set to: " + ip);
        
        // ====== StartClient ======
        jmethodID startClient = env->GetMethodID(nmClass, "StartClient", "()V");
        if (startClient == nullptr) {
            env->ExceptionClear();
            startClient = env->GetMethodID(nmClass, "StartHost", "()V");
            if (startClient == nullptr) {
                write_report("❌ StartClient/StartHost not found!");
                write_network_log("❌ StartClient/StartHost not found!");
                return;
            }
            write_report("🔄 Calling StartHost");
            write_network_log("🔄 Calling StartHost");
        } else {
            write_report("🔄 Calling StartClient");
            write_network_log("🔄 Calling StartClient");
        }
        
        env->CallVoidMethod(nm, startClient);
        write_report("✅ StartClient/StartHost called successfully!");
        write_network_log("✅ StartClient/StartHost called successfully!");
        write_connect_log("🟢 [NetworkManager] Connected to: " + ip);
        
    } catch (const std::exception& e) {
        write_report("❌ Exception: " + std::string(e.what()));
        write_network_log("❌ Exception: " + std::string(e.what()));
    } catch (...) {
        write_report("❌ Unknown exception!");
        write_network_log("❌ Unknown exception!");
    }
    
    write_network_log("========== FINISHED ==========\n");
}

// ======================== روش 4: تست همه روش‌ها ========================
static void test_all_methods(JNIEnv* env) {
    write_report("\n\n========== TEST ALL METHODS ==========");
    write_report("Time: " + get_time());
    write_connect_log("\n\n========== TEST ALL METHODS ==========");
    write_connect_log("Time: " + get_time());
    
    write_report("🔬 Testing all connection methods...");
    write_connect_log("🔬 Testing all connection methods...");
    
    // روش 1: هوک
    write_report("\n--- Method 1: Hook ---");
    write_connect_log("\n--- Method 1: Hook ---");
    if (g_hookInstalled) {
        write_report("✅ Hook is installed. Click Connect button in game to test.");
        write_connect_log("✅ Hook is installed. Click Connect button in game to test.");
    } else {
        write_report("⚠️ Hook not installed. Installing...");
        write_connect_log("⚠️ Hook not installed. Installing...");
        install_hook();
    }
    
    // روش 2: Call LocalJoin
    write_report("\n--- Method 2: Call LocalJoin ---");
    write_connect_log("\n--- Method 2: Call LocalJoin ---");
    call_local_join();
    
    // روش 3: NetworkManager
    write_report("\n--- Method 3: NetworkManager ---");
    write_connect_log("\n--- Method 3: NetworkManager ---");
    connect_via_network_manager(env);
    
    write_report("\n✅ All tests completed!");
    write_connect_log("\n✅ All tests completed!");
    write_connect_log("========== TEST ALL FINISHED ==========\n");
    write_report("========== TEST ALL FINISHED ==========\n");
}

// ======================== هوک ========================
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
        OBFUSCATE("Button_2. Call LocalJoin"),
        OBFUSCATE("Button_3. Connect via NetworkManager"),
        OBFUSCATE("Button_4. TEST ALL METHODS"),
        OBFUSCATE("RichTextView_📁 Logs: /sdcard/Download/lac/"),
        OBFUSCATE("RichTextView_   - hook_log.txt (Hook)"),
        OBFUSCATE("RichTextView_   - call_log.txt (Call)"),
        OBFUSCATE("RichTextView_   - network_log.txt (NetworkManager)"),
        OBFUSCATE("RichTextView_   - connect_log.txt (All)"),
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
        case 0:  // نصب هوک
            write_report("\n🔘 Install Hook button pressed");
            write_result("🔘 Install Hook button pressed");
            install_hook();
            break;
            
        case 1:  // Call LocalJoin
            write_report("\n🔘 Call LocalJoin button pressed");
            write_result("🔘 Call LocalJoin button pressed");
            call_local_join();
            break;
            
        case 2:  // Connect via NetworkManager
            write_report("\n🔘 Connect via NetworkManager button pressed");
            write_result("🔘 Connect via NetworkManager button pressed");
            connect_via_network_manager(env);
            break;
            
        case 3:  // Test All Methods
            write_report("\n🔘 TEST ALL METHODS button pressed");
            write_result("🔘 TEST ALL METHODS button pressed");
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
    // ====== نصب هوک روی Start (برای گرفتن instance) ======
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
    for (int i = 0; i < 15 && g_gtaInstance == nullptr; i++) {
        sleep(1);
    }
    
    if (g_gtaInstance != nullptr) {
        write_report("✅ Got GtaMenuControl instance after wait");
    } else {
        g_gtaInstance = get_gta_menu_instance();
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