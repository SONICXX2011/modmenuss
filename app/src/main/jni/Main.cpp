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

// ======================== IP پیش‌فرض ========================
#define DEFAULT_IP "5.57.37.224"
#define DEFAULT_PORT "9876"

// ======================== آفست‌ها (همه از دامپ) ========================
#define OFFSET_GET_INSTANCE         0xF570C4      // GtaMenuControl.get_Instance()
#define OFFSET_GTA_START            0xF571A4      // GtaMenuControl.Start()
#define OFFSET_LOCAL_JOIN           0xF5B844      // GtaMenuControl.LocalJoin()
#define OFFSET_IP_INPUT             0xC0          // GtaMenuControl.ipInput
#define OFFSET_INPUTFIELD_M_TEXT    0x180         // InputField.m_Text
#define OFFSET_GTA_CHAR_SELECT      0x40          // GtaMenuControl._charSelect
#define OFFSET_CHAR_CURRENT         0x38          // GtaCharacterSelect.currentChar
#define OFFSET_CHAR_NAME_INPUT      0x48          // GtaCharacterSelect.nameInput
#define OFFSET_ON_CLIENT_CONNECT    0xF7FA8C      // CustomNetworkManager.OnClientConnect()
#define OFFSET_ON_SERVER_DISCONNECT 0xF8011C      // CustomNetworkManager.OnServerDisconnect()

// ======================== مسیرها ========================
static std::string g_basePath = "/storage/emulated/0/Download/lac/";
static std::string g_debugLog = g_basePath + "mod_debug.txt";
static std::string g_ipResultLog = g_basePath + "ip_result.txt";
static std::string g_crashLog = g_basePath + "crash_log.txt";
static std::string g_reportLog = g_basePath + "full_report.txt";
static std::string g_connectLog = g_basePath + "connect_log.txt";
static std::string g_playerInfoLog = g_basePath + "player_info.txt";
static std::string g_hookLog = g_basePath + "hook_log.txt";

// ======================== متغیرها ========================
static bool g_crashHandlerInstalled = false;
static void* g_gtaInstance = nullptr;
static bool g_gtaReady = false;
static bool g_hookLocalJoinInstalled = false;
static bool g_hookConnectInstalled = false;
static bool g_hookDisconnectInstalled = false;
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

static void write_player_info(const std::string& msg) {
    write_log(g_playerInfoLog, msg);
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

// ======================== تبدیل MonoString ========================
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
    if (chars == nullptr) {
        write_debug("❌ chars is null");
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
    if (get_Instance == nullptr) {
        write_debug("❌ GtaMenuControl.get_Instance not found");
        return nullptr;
    }
    
    void* instance = get_Instance();
    if (instance != nullptr && is_valid_address(instance)) {
        g_gtaInstance = instance;
        write_debug("✅ GtaMenuControl instance: 0x" + std::to_string((uintptr_t)instance));
        return instance;
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
        write_debug("❌ m_Text pointer invalid");
        return "";
    }
    
    void* monoString = *mTextPtr;
    if (monoString == nullptr) {
        write_debug("❌ MonoString is null");
        return "";
    }
    
    return mono_string_to_utf8(monoString);
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
        write_report("❌ ipInput is null");
        return;
    }
    
    void** mTextPtr = (void**)((uintptr_t)ipInput + OFFSET_INPUTFIELD_M_TEXT);
    if (mTextPtr == nullptr) {
        write_report("❌ m_Text pointer invalid");
        return;
    }
    
    typedef void* (*il2cpp_string_new_t)(const char*);
    il2cpp_string_new_t il2cpp_string_new = 
        (il2cpp_string_new_t)getAbsoluteAddress("libil2cpp.so", "il2cpp_string_new");
    if (il2cpp_string_new == nullptr) {
        write_report("❌ il2cpp_string_new not found");
        return;
    }
    
    void* monoString = il2cpp_string_new(ip.c_str());
    if (monoString == nullptr) {
        write_report("❌ Failed to create mono string");
        return;
    }
    
    *mTextPtr = monoString;
    write_report("✅ IP injected: " + ip);
    write_result("✅ IP injected: " + ip);
}

// ======================== گرفتن GtaCharacterSelect instance ========================
static void* get_char_select_instance() {
    void* instance = get_gta_instance();
    if (instance == nullptr) return nullptr;
    
    void* charSelect = *(void**)((uintptr_t)instance + OFFSET_GTA_CHAR_SELECT);
    if (charSelect == nullptr || !is_valid_address(charSelect)) {
        write_debug("❌ GtaCharacterSelect is null");
        return nullptr;
    }
    return charSelect;
}

// ======================== گرفتن اطلاعات پلیر ========================
static void get_player_info() {
    write_player_info("\n========== PLAYER INFO ==========");
    write_player_info("Time: " + get_time());
    write_report("🔘 Get Player Info pressed");
    
    try {
        void* charSelect = get_char_select_instance();
        if (charSelect == nullptr) {
            write_player_info("❌ Cannot get GtaCharacterSelect instance");
            write_report("❌ Cannot get GtaCharacterSelect instance");
            return;
        }
        
        // گرفتن اسم از nameInput
        void* nameInput = *(void**)((uintptr_t)charSelect + OFFSET_CHAR_NAME_INPUT);
        std::string username = "";
        if (nameInput != nullptr && is_valid_address(nameInput)) {
            void** mTextPtr = (void**)((uintptr_t)nameInput + OFFSET_INPUTFIELD_M_TEXT);
            if (mTextPtr != nullptr && *mTextPtr != nullptr) {
                username = mono_string_to_utf8(*mTextPtr);
            }
        }
        
        // گرفتن کاراکتر ID
        int charId = -1;
        int* currentCharPtr = (int*)((uintptr_t)charSelect + OFFSET_CHAR_CURRENT);
        if (currentCharPtr != nullptr && is_valid_address(currentCharPtr)) {
            charId = *currentCharPtr;
        }
        
        std::string result = "👤 Username: " + (username.empty() ? "(empty)" : username) + 
                            " | 🎭 Character ID: " + (charId < 0 ? "(unknown)" : std::to_string(charId));
        
        write_player_info("✅ " + result);
        write_report("✅ " + result);
        write_result("✅ " + result);
        
    } catch (const std::exception& e) {
        write_player_info("❌ Exception: " + std::string(e.what()));
        write_crash("⚠️ Player info exception: " + std::string(e.what()));
    } catch (...) {
        write_player_info("❌ Unknown exception");
        write_crash("⚠️ Unknown player info exception");
    }
    write_player_info("========== DONE ==========\n");
}

// ======================== هوک LocalJoin ========================
void (*orig_LocalJoin)(void *instance);
void hook_LocalJoin(void *instance) {
    std::string ip = get_ip_from_input();
    write_connect_log("========== [HOOK] LocalJoin TRIGGERED ==========");
    write_connect_log("Time: " + get_time());
    write_connect_log("📡 IP: " + ip);
    write_hook_log("🟢 LocalJoin called - IP: " + ip);
    write_report("🟢 [HOOK] LocalJoin called - IP: " + ip);
    __android_log_print(ANDROID_LOG_INFO, "LAC_Mod", "🟢 [HOOK] LocalJoin called - IP: %s", ip.c_str());
    
    if (orig_LocalJoin) {
        orig_LocalJoin(instance);
        write_connect_log("✅ Original LocalJoin executed");
    } else {
        write_connect_log("❌ orig_LocalJoin is null!");
    }
    write_connect_log("========== [HOOK] FINISHED ==========\n");
}

// ======================== نصب هوک LocalJoin ========================
static void install_hook_localjoin() {
    if (g_hookLocalJoinInstalled) {
        write_report("⚠️ LocalJoin hook already installed");
        return;
    }
    
    write_report("🔧 Installing LocalJoin hook...");
    write_hook_log("🔧 Installing LocalJoin hook...");
    
#if defined(__aarch64__)
    void* localJoinAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0xF5B844"));
    if (localJoinAddr != nullptr && is_valid_address(localJoinAddr)) {
        int res = DobbyHook(localJoinAddr, (dobby_dummy_func_t)hook_LocalJoin, (dobby_dummy_func_t*)&orig_LocalJoin);
        if (res == 0) {
            g_hookLocalJoinInstalled = true;
            write_report("✅ LocalJoin hooked at 0x" + std::to_string((uintptr_t)localJoinAddr));
            write_hook_log("✅ LocalJoin hooked at 0x" + std::to_string((uintptr_t)localJoinAddr));
            __android_log_print(ANDROID_LOG_INFO, "LAC_Mod", "✅ LocalJoin hook installed!");
        } else {
            write_report("❌ LocalJoin hook failed! error: " + std::to_string(res));
            write_hook_log("❌ LocalJoin hook failed! error: " + std::to_string(res));
        }
    } else {
        write_report("❌ LocalJoin address not found!");
        write_hook_log("❌ LocalJoin address not found!");
    }
#else
    write_report("❌ Not ARM64");
#endif
}

// ======================== صدا زدن LocalJoin مستقیم ========================
static void call_local_join_direct() {
    write_connect_log("\n========== CALL LOCALJOIN DIRECT ==========");
    write_connect_log("Time: " + get_time());
    write_report("🔘 Call LocalJoin Direct pressed");
    
    try {
        void* instance = get_gta_instance();
        if (instance == nullptr || !is_valid_address(instance)) {
            write_connect_log("❌ Cannot get GtaMenuControl instance");
            write_report("❌ Cannot get GtaMenuControl instance");
            return;
        }
        
        std::string ip = get_ip_from_input();
        write_connect_log("📡 Target IP: " + ip);
        write_report("📡 Target IP: " + ip);
        
        void* localJoinAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0xF5B844"));
        if (localJoinAddr == nullptr || !is_valid_address(localJoinAddr)) {
            write_connect_log("❌ LocalJoin address not found");
            write_report("❌ LocalJoin address not found");
            return;
        }
        
        typedef void (*local_join_t)(void* instance);
        local_join_t LocalJoin = (local_join_t)localJoinAddr;
        
        write_connect_log("🔄 Calling LocalJoin()...");
        write_report("🔄 Calling LocalJoin()...");
        LocalJoin(instance);
        write_connect_log("✅ LocalJoin() called successfully");
        write_report("✅ LocalJoin() called successfully");
        write_result("✅ LocalJoin called");
        
    } catch (const std::exception& e) {
        write_connect_log("❌ Exception: " + std::string(e.what()));
        write_report("❌ Exception: " + std::string(e.what()));
        write_crash("⚠️ LocalJoin exception: " + std::string(e.what()));
    } catch (...) {
        write_connect_log("❌ Unknown exception");
        write_report("❌ Unknown exception");
        write_crash("⚠️ Unknown LocalJoin exception");
    }
    write_connect_log("========== CALL FINISHED ==========\n");
}

// ======================== هوک OnClientConnect ========================
void (*orig_OnClientConnect)(void *instance, void *conn);
void hook_OnClientConnect(void *instance, void *conn) {
    write_connect_log("========== ✅ CLIENT CONNECTED ==========");
    write_connect_log("Time: " + get_time());
    write_hook_log("✅ Client CONNECTED to server!");
    write_report("✅ Client connected to server!");
    write_result("✅ Connected!");
    __android_log_print(ANDROID_LOG_INFO, "LAC_Mod", "✅ Client CONNECTED!");
    
    if (orig_OnClientConnect) {
        orig_OnClientConnect(instance, conn);
    }
    write_connect_log("========== CONNECT FINISHED ==========\n");
}

// ======================== هوک OnServerDisconnect ========================
void (*orig_OnServerDisconnect)(void *instance, void *conn);
void hook_OnServerDisconnect(void *instance, void *conn) {
    write_connect_log("========== ❌ CLIENT DISCONNECTED ==========");
    write_connect_log("Time: " + get_time());
    write_hook_log("❌ Client DISCONNECTED from server!");
    write_report("❌ Client disconnected!");
    write_result("❌ Disconnected!");
    __android_log_print(ANDROID_LOG_INFO, "LAC_Mod", "❌ Client DISCONNECTED!");
    
    if (orig_OnServerDisconnect) {
        orig_OnServerDisconnect(instance, conn);
    }
    write_connect_log("========== DISCONNECT FINISHED ==========\n");
}

// ======================== نصب هوک‌های Connection Events ========================
static void install_hooks_connection() {
    write_report("🔧 Installing connection hooks...");
    write_hook_log("🔧 Installing connection hooks...");
    
#if defined(__aarch64__)
    // OnClientConnect
    void* connectAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0xF7FA8C"));
    if (connectAddr != nullptr && is_valid_address(connectAddr)) {
        int res = DobbyHook(connectAddr, (dobby_dummy_func_t)hook_OnClientConnect, (dobby_dummy_func_t*)&orig_OnClientConnect);
        if (res == 0) {
            g_hookConnectInstalled = true;
            write_report("✅ OnClientConnect hooked at 0x" + std::to_string((uintptr_t)connectAddr));
            write_hook_log("✅ OnClientConnect hooked");
        } else {
            write_report("❌ OnClientConnect hook failed! error: " + std::to_string(res));
            write_hook_log("❌ OnClientConnect hook failed");
        }
    } else {
        write_report("❌ OnClientConnect address not found");
        write_hook_log("❌ OnClientConnect address not found");
    }
    
    // OnServerDisconnect
    void* disconnectAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0xF8011C"));
    if (disconnectAddr != nullptr && is_valid_address(disconnectAddr)) {
        int res = DobbyHook(disconnectAddr, (dobby_dummy_func_t)hook_OnServerDisconnect, (dobby_dummy_func_t*)&orig_OnServerDisconnect);
        if (res == 0) {
            g_hookDisconnectInstalled = true;
            write_report("✅ OnServerDisconnect hooked at 0x" + std::to_string((uintptr_t)disconnectAddr));
            write_hook_log("✅ OnServerDisconnect hooked");
        } else {
            write_report("❌ OnServerDisconnect hook failed! error: " + std::to_string(res));
            write_hook_log("❌ OnServerDisconnect hook failed");
        }
    } else {
        write_report("❌ OnServerDisconnect address not found");
        write_hook_log("❌ OnServerDisconnect address not found");
    }
#endif
}

// ======================== هوک GtaMenuControl.Start ========================
void (*orig_GtaMenuStart)(void *instance);
void hook_GtaMenuStart(void *instance) {
    if (instance != nullptr && is_valid_address(instance)) {
        g_gtaInstance = instance;
        g_gtaReady = true;
        write_report("✅ Hook captured GtaMenuControl instance");
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
        OBFUSCATE("Category_🌐 Network"),
        OBFUSCATE("InputText_Enter IP"),
        OBFUSCATE("Button_Inject IP"),
        OBFUSCATE("Button_Connect (LocalJoin)"),
        OBFUSCATE("Category_🔧 Hooks"),
        OBFUSCATE("Button_Install LocalJoin Hook"),
        OBFUSCATE("Button_Install Connection Hooks"),
        OBFUSCATE("Category_👤 Player Info"),
        OBFUSCATE("Button_Get Player Info"),
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
            // InputText: وارد کردن IP
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
            // Inject IP
            write_report("🔘 Inject IP pressed");
            if (g_targetIP.empty()) {
                g_targetIP = load_ip();
                if (g_targetIP.empty()) {
                    g_targetIP = std::string(DEFAULT_IP) + ":" + std::string(DEFAULT_PORT);
                }
            }
            inject_ip_to_input(g_targetIP);
            break;
        }
        
        case 2: {
            // Connect (LocalJoin)
            write_report("🔘 Connect (LocalJoin) pressed");
            write_result("🔘 Connect pressed");
            call_local_join_direct();
            break;
        }
        
        case 3: {
            // Install LocalJoin Hook
            install_hook_localjoin();
            break;
        }
        
        case 4: {
            // Install Connection Hooks
            install_hooks_connection();
            break;
        }
        
        case 5: {
            // Get Player Info
            get_player_info();
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

    // ====== بارگذاری IP ======
    g_targetIP = load_ip();
    if (g_targetIP.empty()) {
        g_targetIP = std::string(DEFAULT_IP) + ":" + std::string(DEFAULT_PORT);
        save_ip(g_targetIP);
        write_report("📌 Default IP set: " + g_targetIP);
    } else {
        write_report("📌 IP loaded from file: " + g_targetIP);
    }
    write_debug("📌 Current IP: " + g_targetIP);

#if defined(__aarch64__)
    // ====== هوک روی Start (برای گرفتن instance) ======
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

    // ====== تلاش برای گرفتن instance ======
    for (int i = 0; i < 20 && g_gtaInstance == nullptr; i++) {
        sleep(1);
    }
    
    if (g_gtaInstance != nullptr) {
        write_report("✅ Got GtaMenuControl instance after wait");
        write_debug("✅ Got instance after wait");
    } else {
        g_gtaInstance = get_gta_instance();
        if (g_gtaInstance != nullptr) {
            write_report("✅ Got GtaMenuControl instance via get_Instance");
            write_debug("✅ Got instance via get_Instance");
        }
    }

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