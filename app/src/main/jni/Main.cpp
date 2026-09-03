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
#include <cstdlib>
#include "Includes/Logger.h"
#include "Includes/obfuscate.h"
#include "Includes/Utils.hpp"
#include "Menu/Menu.hpp"
#include "Menu/Jni.hpp"
#include "Includes/Macros.h"
#include "dobby.h"
#include "KittyMemory/KittyMemory.hpp"

#define targetLibName OBFUSCATE("libil2cpp.so")

// ======================== آفست‌های دقیق ========================
#define OFFSET_GTA_GET_INSTANCE     0xF570C4      // GtaMenuControl.get_Instance() ✅
#define OFFSET_GTA_START            0xF571A4      // GtaMenuControl.Start() ❌ (نمیشه استفاده کرد)
#define OFFSET_IP_INPUT             0xC0          // GtaMenuControl.ipInput
#define OFFSET_INPUTFIELD_M_TEXT    0x180         // InputField.m_Text
#define OFFSET_NETWORK_MANAGER      0x258         // GtaMenuControl._networkManager
#define OFFSET_NETWORK_ADDR         0x50          // NetworkManager.networkAddress
#define OFFSET_IL2CPP_STRING_NEW    0xE40BF0      // il2cpp_string_new()

// ====== آفست‌های AutoLAN / CanvasHUD (دکمه‌های LAN) ======
#define OFFSET_AUTOLAN_SINGLETON    0x1AF107C     // AutoLANNetworkManager.get_singleton()
#define OFFSET_AUTOLAN_CANVASHUD    0xB0          // AutoLANNetworkManager.canvasHUD
#define OFFSET_CANVASHUD_BUTTONCLIENT 0x60       // CanvasHUD.buttonClient
#define OFFSET_CANVASHUD_INPUTFIELD 0x80          // CanvasHUD.inputFieldAddress
#define OFFSET_BUTTON_ONCLICK       0x100         // Button.m_OnClick
#define OFFSET_BUTTON_INTERACTABLE  0xD8          // Selectable.m_Interactable

// ======================== IP پیش‌فرض ========================
#define DEFAULT_IP "5.57.37.224"
#define DEFAULT_PORT "9876"

// ======================== مسیرهای لاگ ========================
static std::string g_basePath = "/storage/emulated/0/Download/lac/";
static std::string g_debugLog = g_basePath + "mod_debug.txt";
static std::string g_ipResultLog = g_basePath + "ip_result.txt";
static std::string g_crashLog = g_basePath + "crash_log.txt";
static std::string g_reportLog = g_basePath + "full_report.txt";
static std::string g_connectLog = g_basePath + "connect_log.txt";
static std::string g_lastIPFile = g_basePath + "last_ip.txt";

// ======================== متغیرهای سراسری ========================
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

static std::string mono_string_to_utf8(void* monoString) {
    if (monoString == nullptr) return "";
    if (!is_valid_address(monoString)) return "";
    
    typedef int32_t (*il2cpp_string_length_t)(void* str);
    il2cpp_string_length_t il2cpp_string_length = 
        (il2cpp_string_length_t)getAbsoluteAddress("libil2cpp.so", OBFUSCATE("il2cpp_string_length"));
    if (il2cpp_string_length == nullptr) {
        write_connect_log("❌ il2cpp_string_length not found");
        return "";
    }
    
    int length = il2cpp_string_length(monoString);
    if (length <= 0 || length > 65536) return "";
    
    typedef uint16_t* (*il2cpp_string_chars_t)(void* str);
    il2cpp_string_chars_t il2cpp_string_chars = 
        (il2cpp_string_chars_t)getAbsoluteAddress("libil2cpp.so", OBFUSCATE("il2cpp_string_chars"));
    if (il2cpp_string_chars == nullptr) {
        write_connect_log("❌ il2cpp_string_chars not found");
        return "";
    }
    
    uint16_t* chars = il2cpp_string_chars(monoString);
    if (chars == nullptr || !is_valid_address(chars)) return "";
    
    std::string result;
    result.reserve(length);
    for (int i = 0; i < length; i++) {
        result += (char)(chars[i] & 0xFF);
    }
    return result;
}

// ======================== گرفتن GtaMenuControl instance ========================
static void* get_gta_instance() {
    // اول از حافظه کش بخون
    if (g_gtaInstance != nullptr && is_valid_address(g_gtaInstance)) {
        return g_gtaInstance;
    }
    
    // استفاده از get_Instance (آفست 0xF570C4) - این کار میکنه!
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
        write_connect_log("✅ GtaMenuControl instance via get_Instance: 0x" + std::to_string((uintptr_t)instance));
        return instance;
    }
    return nullptr;
}

// ======================== تنظیم IP در NetworkManager ========================
static bool set_ip_in_network_manager(const std::string& ip) {
    void* instance = get_gta_instance();
    if (instance == nullptr) {
        write_connect_log("❌ Cannot get instance for NetworkManager");
        return false;
    }
    
    void* nm = *(void**)((uintptr_t)instance + OFFSET_NETWORK_MANAGER);
    if (nm == nullptr || !is_valid_address(nm)) {
        write_connect_log("❌ NetworkManager is null or invalid");
        return false;
    }
    
    void** addrPtr = (void**)((uintptr_t)nm + OFFSET_NETWORK_ADDR);
    if (!is_valid_pointer(addrPtr)) {
        write_connect_log("❌ networkAddress pointer invalid");
        return false;
    }
    
    void* monoStr = create_mono_string(ip.c_str());
    if (monoStr == nullptr) {
        write_connect_log("❌ Failed to create mono string");
        return false;
    }
    
    *addrPtr = monoStr;
    write_connect_log("✅ networkAddress set to: " + ip);
    return true;
}

// ======================== تزریق IP به ipInput (UI) ========================
static bool inject_ip_to_input(const std::string& ip) {
    void* instance = get_gta_instance();
    if (instance == nullptr) {
        write_report("❌ Cannot get instance for IP injection");
        return false;
    }
    
    void* ipInput = *(void**)((uintptr_t)instance + OFFSET_IP_INPUT);
    if (ipInput == nullptr || !is_valid_address(ipInput)) {
        write_report("❌ ipInput is null");
        return false;
    }
    
    void** mTextPtr = (void**)((uintptr_t)ipInput + OFFSET_INPUTFIELD_M_TEXT);
    if (mTextPtr == nullptr || !is_valid_pointer(mTextPtr)) {
        write_report("❌ m_Text pointer invalid");
        return false;
    }
    
    void* monoString = create_mono_string(ip.c_str());
    if (monoString == nullptr) {
        write_report("❌ Failed to create mono string");
        return false;
    }
    
    *mTextPtr = monoString;
    write_report("✅ IP injected to UI: " + ip);
    return true;
}

// ======================== گرفتن AutoLANNetworkManager ========================
static void* get_autolan_manager() {
    typedef void* (*get_singleton_t)();
    get_singleton_t get_singleton = (get_singleton_t)getAbsoluteAddress("libil2cpp.so", OBFUSCATE("0x1AF107C"));
    if (get_singleton == nullptr) {
        write_connect_log("❌ AutoLANNetworkManager.get_singleton not found");
        return nullptr;
    }
    void* nm = get_singleton();
    if (nm == nullptr || !is_valid_address(nm)) {
        write_connect_log("❌ AutoLANNetworkManager is null");
        return nullptr;
    }
    write_connect_log("✅ AutoLANNetworkManager: 0x" + std::to_string((uintptr_t)nm));
    return nm;
}

// ======================== گرفتن CanvasHUD ========================
static void* get_canvas_hud() {
    void* autolan = get_autolan_manager();
    if (autolan == nullptr) return nullptr;
    
    void* hud = *(void**)((uintptr_t)autolan + OFFSET_AUTOLAN_CANVASHUD);
    if (hud == nullptr || !is_valid_address(hud)) {
        write_connect_log("❌ CanvasHUD is null");
        return nullptr;
    }
    write_connect_log("✅ CanvasHUD: 0x" + std::to_string((uintptr_t)hud));
    return hud;
}

// ======================== گرفتن IP از inputFieldAddress (CanvasHUD) ========================
static std::string get_ip_from_canvas_input() {
    void* hud = get_canvas_hud();
    if (hud == nullptr) return "";
    
    void* inputField = *(void**)((uintptr_t)hud + OFFSET_CANVASHUD_INPUTFIELD);
    if (inputField == nullptr || !is_valid_address(inputField)) {
        write_connect_log("❌ CanvasHUD.inputFieldAddress is null");
        return "";
    }
    
    void** mTextPtr = (void**)((uintptr_t)inputField + OFFSET_INPUTFIELD_M_TEXT);
    if (mTextPtr == nullptr || !is_valid_pointer(mTextPtr)) {
        write_connect_log("❌ CanvasHUD inputField m_Text invalid");
        return "";
    }
    
    void* monoString = *mTextPtr;
    if (monoString == nullptr) {
        write_connect_log("❌ CanvasHUD inputField MonoString is null");
        return "";
    }
    
    return mono_string_to_utf8(monoString);
}

// ======================== تزریق IP به CanvasHUD.inputFieldAddress ========================
static bool inject_ip_to_canvas_input(const std::string& ip) {
    void* hud = get_canvas_hud();
    if (hud == nullptr) return false;
    
    void* inputField = *(void**)((uintptr_t)hud + OFFSET_CANVASHUD_INPUTFIELD);
    if (inputField == nullptr || !is_valid_address(inputField)) {
        write_connect_log("❌ CanvasHUD.inputFieldAddress is null");
        return false;
    }
    
    void** mTextPtr = (void**)((uintptr_t)inputField + OFFSET_INPUTFIELD_M_TEXT);
    if (mTextPtr == nullptr || !is_valid_pointer(mTextPtr)) {
        write_connect_log("❌ CanvasHUD inputField m_Text invalid");
        return false;
    }
    
    void* monoString = create_mono_string(ip.c_str());
    if (monoString == nullptr) {
        write_connect_log("❌ Failed to create mono string");
        return false;
    }
    
    *mTextPtr = monoString;
    write_connect_log("✅ IP injected to CanvasHUD inputField: " + ip);
    return true;
}

// ======================== ====== روش اتصال نهایی ====== ========================
// فقط روشی که کار میکنه: کلیک روی دکمه Connect از CanvasHUD
static void ConnectToServer() {
    write_connect_log("\n========== CONNECT TO SERVER (CanvasHUD.ButtonClient) ==========");
    write_connect_log("Time: " + get_time());
    write_report("🔘 Connect button pressed");
    write_result("🔘 Connect");
    
    try {
        // ====== 1. گرفتن IP ======
        std::string ip = get_ip_from_canvas_input();
        if (ip.empty()) {
            ip = load_ip();
            if (ip.empty()) {
                ip = std::string(DEFAULT_IP) + ":" + std::string(DEFAULT_PORT);
                save_ip(ip);
                write_connect_log("📌 Using default IP: " + ip);
            } else {
                inject_ip_to_canvas_input(ip);
                write_connect_log("📌 Loaded IP from file: " + ip);
            }
        } else {
            save_ip(ip);
            write_connect_log("📡 IP from Canvas input: " + ip);
        }
        g_targetIP = ip;
        
        // ====== 2. تنظیم IP در NetworkManager (مهم!) ======
        if (!set_ip_in_network_manager(ip)) {
            write_connect_log("⚠️ Failed to set IP in NetworkManager, but continuing...");
        }
        
        // ====== 3. تزریق IP به UI های مختلف ======
        inject_ip_to_input(ip);           // GtaMenuControl.ipInput
        inject_ip_to_canvas_input(ip);    // CanvasHUD.inputFieldAddress
        
        // ====== 4. گرفتن CanvasHUD ======
        void* hud = get_canvas_hud();
        if (hud == nullptr) {
            write_connect_log("❌ CanvasHUD not found!");
            write_result("❌ Connect failed: CanvasHUD not found");
            return;
        }
        
        // ====== 5. گرفتن buttonClient از CanvasHUD (آفست 0x60) ======
        void* connectBtn = *(void**)((uintptr_t)hud + OFFSET_CANVASHUD_BUTTONCLIENT);
        if (connectBtn == nullptr || !is_valid_address(connectBtn)) {
            write_connect_log("❌ buttonClient is null!");
            write_result("❌ Connect failed: buttonClient not found");
            return;
        }
        write_connect_log("✅ buttonClient: 0x" + std::to_string((uintptr_t)connectBtn));
        
        // ====== 6. فعال کردن دکمه (اگه غیرفعال باشه) ======
        bool* interactable = (bool*)((uintptr_t)connectBtn + OFFSET_BUTTON_INTERACTABLE);
        if (interactable != nullptr && is_valid_address(interactable)) {
            if (!*interactable) {
                *interactable = true;
                write_connect_log("✅ buttonClient was disabled, enabled it");
            }
        }
        
        // ====== 7. گرفتن onClick از دکمه (آفست 0x100) ======
        void* onClick = *(void**)((uintptr_t)connectBtn + OFFSET_BUTTON_ONCLICK);
        if (onClick == nullptr || !is_valid_address(onClick)) {
            write_connect_log("❌ onClick is null!");
            write_result("❌ Connect failed: onClick not found");
            return;
        }
        write_connect_log("✅ onClick: 0x" + std::to_string((uintptr_t)onClick));
        
        // ====== 8. صدا زدن Invoke ======
        typedef void (*invoke_t)(void*);
        invoke_t Invoke = (invoke_t)getAbsoluteAddress("libil2cpp.so", OBFUSCATE("UnityEngine.Events.UnityEvent.Invoke"));
        if (Invoke == nullptr) {
            write_connect_log("❌ UnityEvent.Invoke not found!");
            write_result("❌ Connect failed: Invoke not found");
            return;
        }
        
        write_connect_log("🔄 Calling UnityEvent.Invoke() on buttonClient...");
        Invoke(onClick);
        write_connect_log("✅ Connect button clicked successfully!");
        write_result("✅ Connected to: " + ip);
        
    } catch (const std::exception& e) {
        write_connect_log("❌ Exception: " + std::string(e.what()));
        write_crash("⚠️ Exception: " + std::string(e.what()));
        write_result("❌ Connect crashed");
    } catch (...) {
        write_connect_log("❌ Unknown exception!");
        write_crash("⚠️ Unknown exception!");
        write_result("❌ Connect crashed");
    }
    write_connect_log("========== CONNECT FINISHED ==========\n");
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
            inject_ip_to_input(ip);
            write_report("📌 Auto-injected saved IP to UI");
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
        OBFUSCATE("Button_💉 Inject IP"),
        OBFUSCATE("Button_📋 Show IP"),
        OBFUSCATE("Button_🔗 CONNECT TO SERVER"),
        OBFUSCATE("RichTextView_📁 Logs: /sdcard/Download/lac/"),
        OBFUSCATE("RichTextView_📌 Check connect_log.txt for details"),
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
            
            case 1: { // Inject IP
                write_report("🔘 Inject IP pressed");
                if (g_targetIP.empty()) {
                    g_targetIP = load_ip();
                    if (g_targetIP.empty()) {
                        g_targetIP = std::string(DEFAULT_IP) + ":" + std::string(DEFAULT_PORT);
                    }
                }
                inject_ip_to_input(g_targetIP);
                inject_ip_to_canvas_input(g_targetIP);
                write_result("✅ IP injected: " + g_targetIP);
                break;
            }
            
            case 2: { // Show IP
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
            
            case 3: { // CONNECT
                ConnectToServer();
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
    // ====== نصب هوک روی GtaMenuControl.Start (فقط برای گرفتن instance) ======
    void* startAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0xF571A4"));
    if (startAddr != nullptr && is_valid_address(startAddr)) {
        int res = DobbyHook(startAddr, (dobby_dummy_func_t)hook_GtaMenuStart, (dobby_dummy_func_t*)&orig_GtaMenuStart);
        if (res == 0) {
            write_report("✅ GtaMenuControl.Start() hooked");
        } else {
            write_report("❌ Hook failed, error: " + std::to_string(res));
        }
    }
#endif

    // ====== گرفتن instance با get_Instance ======
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

    // ====== بارگذاری IP ======
    g_targetIP = load_ip();
    if (g_targetIP.empty()) {
        g_targetIP = std::string(DEFAULT_IP) + ":" + std::string(DEFAULT_PORT);
        save_ip(g_targetIP);
        write_report("📌 Default IP set: " + g_targetIP);
    } else {
        write_report("📌 IP loaded from file: " + g_targetIP);
    }

    // ====== تزریق خودکار IP ======
    if (g_gtaInstance != nullptr) {
        inject_ip_to_input(g_targetIP);
        inject_ip_to_canvas_input(g_targetIP);
        set_ip_in_network_manager(g_targetIP);
    }

    write_report("✅ hack_thread finished");
}

// ======================== تابع ورودی ========================
__attribute__((constructor))
void lib_main() {
    create_directory();
    install_crash_handler();

    write_report("🚀 lib_main called - mod loading");
    std::thread(hack_thread).detach();
}