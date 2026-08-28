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

#define targetLibName OBFUSCATE("libil2cpp.so")

// ======================== آفست‌ها (همه از دامپ) ========================
#define OFFSET_GET_INSTANCE     0xF570C4      // GtaMenuControl.get_Instance()
#define OFFSET_GTA_START        0xF571A4      // GtaMenuControl.Start()
#define OFFSET_IP_INPUT         0xC0          // GtaMenuControl.ipInput
#define OFFSET_MENU_BUTTONS     0x30          // GtaMenuControl.menuButtons
#define OFFSET_M_TEXT_COMPONENT 0x108         // InputField.m_TextComponent
#define OFFSET_M_TEXT           0x180         // InputField.m_Text
#define OFFSET_SET_TEXT_RTL     0x1034114     // RtlText.set_text
#define OFFSET_INTERACTABLE     0xD8          // Selectable.m_Interactable

// ======================== IP پیش‌فرض ========================
#define DEFAULT_IP "5.57.37.224"
#define DEFAULT_PORT "9876"

// ======================== مسیرها ========================
static std::string g_basePath = "/storage/emulated/0/Download/lac/";
static std::string g_debugLog = g_basePath + "mod_debug.txt";
static std::string g_lastIPFile = g_basePath + "last_ip.txt";
static std::string g_ipResultLog = g_basePath + "ip_result.txt";
static std::string g_crashLog = g_basePath + "crash_log.txt";
static std::string g_reportLog = g_basePath + "full_report.txt";

// ======================== متغیرها ========================
static std::string g_targetIP = "";
static bool g_crashHandlerInstalled = false;
static void* g_gtaInstance = nullptr;
static bool g_gtaReady = false;
static bool g_buttonsDisabled = false;

// ======================== اعلان اولیه توابع (FORWARD DECLARATIONS) ========================
static void connect_to_server(JNIEnv* env, jobject obj);
static void disable_menu_buttons();

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

// ======================== گرفتن MonoString از string ========================
static void* create_mono_string(const char* str) {
    if (str == nullptr) return nullptr;
    
    typedef void* (*il2cpp_string_new_t)(const char* str);
    il2cpp_string_new_t il2cpp_string_new = (il2cpp_string_new_t)getAbsoluteAddress("libil2cpp.so", "il2cpp_string_new");
    
    if (il2cpp_string_new == nullptr) {
        write_report("❌ il2cpp_string_new not found!");
        return nullptr;
    }
    
    return il2cpp_string_new(str);
}

// ======================== تزریق IP به ipInput ========================
static bool inject_ip_to_ipInput(const std::string& ip) {
    write_report("\n========== INJECT IP TO IPINPUT ==========");
    write_report("Time: " + get_time());
    write_report("Target IP: " + ip);

    if (!g_gtaReady || g_gtaInstance == nullptr) {
        write_report("❌ GtaMenuControl not ready!");
        write_log(g_ipResultLog, "❌ GtaMenuControl not ready!");
        return false;
    }

    try {
        // ====== 1. گرفتن ipInput از instance ======
        write_report("Step 1: Getting ipInput from GtaMenuControl");
        void* ipInputField = *(void**)((uintptr_t)g_gtaInstance + OFFSET_IP_INPUT);
        
        if (ipInputField == nullptr) {
            write_report("❌ ipInput is null!");
            write_log(g_ipResultLog, "❌ ipInput is null!");
            return false;
        }
        write_report("✅ ipInput: 0x" + std::to_string((uintptr_t)ipInputField));

        // ====== 2. گرفتن m_TextComponent از ipInput ======
        write_report("Step 2: Getting m_TextComponent from InputField");
        void* textComponent = *(void**)((uintptr_t)ipInputField + OFFSET_M_TEXT_COMPONENT);
        
        if (textComponent == nullptr) {
            write_report("❌ m_TextComponent is null!");
            write_log(g_ipResultLog, "❌ m_TextComponent is null!");
            return false;
        }
        write_report("✅ m_TextComponent: 0x" + std::to_string((uintptr_t)textComponent));

        // ====== 3. تزریق IP با RtlText.set_text ======
        write_report("Step 3: Injecting IP with RtlText.set_text");
        typedef void (*set_text_rtl_t)(void* textComponent, void* monoString);
        set_text_rtl_t set_text_rtl = (set_text_rtl_t)getAbsoluteAddress("libil2cpp.so", "RtlText.set_text");
        
        if (set_text_rtl == nullptr) {
            write_report("❌ RtlText.set_text not found!");
            write_log(g_ipResultLog, "❌ RtlText.set_text not found!");
            return false;
        }
        
        void* monoString = create_mono_string(ip.c_str());
        if (monoString == nullptr) {
            write_report("❌ Failed to create mono string!");
            write_log(g_ipResultLog, "❌ Failed to create mono string!");
            return false;
        }
        
        set_text_rtl(textComponent, monoString);
        write_report("✅ IP injected via RtlText.set_text");
        write_log(g_ipResultLog, "✅ IP injected: " + ip);
        write_report("========== INJECT COMPLETE ==========\n");
        return true;

    } catch (const std::exception& e) {
        write_report("❌ Exception: " + std::string(e.what()));
        write_log(g_crashLog, "⚠️ Exception: " + std::string(e.what()));
        return false;
    } catch (...) {
        write_report("❌ Unknown exception!");
        write_log(g_crashLog, "⚠️ Unknown exception!");
        return false;
    }
}

// ======================== دیسیبل کردن دکمه‌ها (NOP کردن) ========================
static void disable_menu_buttons() {
    if (g_buttonsDisabled) {
        write_report("⚠️ Buttons already disabled, skipping...");
        return;
    }
    
    write_report("\n========== DISABLE MENU BUTTONS (NOP) ==========");
    write_report("Time: " + get_time());
    
    if (g_gtaInstance == nullptr) {
        write_report("❌ GtaMenuControl instance is null!");
        return;
    }
    
    try {
        // گرفتن آرایه دکمه‌ها
        void** menuButtons = *(void***)((uintptr_t)g_gtaInstance + OFFSET_MENU_BUTTONS);
        if (menuButtons == nullptr) {
            write_report("❌ menuButtons is null!");
            return;
        }
        write_report("✅ menuButtons array: 0x" + std::to_string((uintptr_t)menuButtons));
        
        // دکمه‌هایی که باید فعال بمونن (LAN=0, LAN=1, SETTINGS=4)
        int keepActive[] = {0, 1, 4};
        int keepCount = 3;
        
        // تعداد کل دکمه‌ها (از اسکرین شات 8 تاست)
        int totalButtons = 8;
        int disabledCount = 0;
        
        for (int i = 0; i < totalButtons; i++) {
            // چک کن آیا این دکمه باید فعال بمونه؟
            bool shouldKeep = false;
            for (int j = 0; j < keepCount; j++) {
                if (keepActive[j] == i) {
                    shouldKeep = true;
                    break;
                }
            }
            
            if (shouldKeep) {
                write_report("   🔵 Keeping active: index " + std::to_string(i));
                continue;
            }
            
            void* button = menuButtons[i];
            if (button == nullptr) {
                write_report("   ⚠️ Button index " + std::to_string(i) + " is null!");
                continue;
            }
            
            // غیرفعال کردن دکمه (m_Interactable = false) = NOP کردن
            bool* interactable = (bool*)((uintptr_t)button + OFFSET_INTERACTABLE);
            *interactable = false;
            disabledCount++;
            write_report("   ❌ Disabled (NOP) button index: " + std::to_string(i));
        }
        
        g_buttonsDisabled = true;
        write_report("✅ " + std::to_string(disabledCount) + " buttons disabled (NOP), LAN and SETTINGS kept active!");
        write_report("========== DISABLE COMPLETE ==========\n");
        
    } catch (const std::exception& e) {
        write_report("❌ Exception: " + std::string(e.what()));
        write_log(g_crashLog, "⚠️ Exception: " + std::string(e.what()));
    } catch (...) {
        write_report("❌ Unknown exception!");
        write_log(g_crashLog, "⚠️ Unknown exception!");
    }
}

// ======================== هوک روی GtaMenuControl.Start ========================
void (*orig_GtaMenuStart)(void *instance);
void hook_GtaMenuStart(void *instance) {
    write_report("========== GtaMenuControl.Start() HOOKED ==========");
    write_report("Time: " + get_time());
    
    if (instance != nullptr) {
        g_gtaInstance = instance;
        g_gtaReady = true;
        write_report("✅ GtaMenuControl instance saved: 0x" + std::to_string((uintptr_t)instance));
        write_debug("✅ GtaMenuControl instance saved");
        
        if (!g_targetIP.empty()) {
            write_report("📌 Auto-injecting saved IP: " + g_targetIP);
            if (inject_ip_to_ipInput(g_targetIP)) {
                disable_menu_buttons();
            }
        }
    } else {
        write_report("❌ instance is null!");
    }
    write_report("====================================================\n");
    
    if (orig_GtaMenuStart) {
        orig_GtaMenuStart(instance);
    }
}

// ======================== گرفتن instance با get_Instance ========================
static void* get_gta_instance() {
    typedef void* (*get_instance_t)();
    get_instance_t get_Instance = (get_instance_t)getAbsoluteAddress("libil2cpp.so", "GtaMenuControl.get_Instance");
    
    if (get_Instance == nullptr) {
        write_report("❌ GtaMenuControl.get_Instance not found!");
        return nullptr;
    }
    
    void* instance = get_Instance();
    if (instance != nullptr) {
        write_report("✅ GtaMenuControl instance via get_Instance: 0x" + std::to_string((uintptr_t)instance));
    }
    
    return instance;
}

// ======================== اتصال به سرور ========================
static void connect_to_server(JNIEnv* env, jobject obj) {
    write_report("\n========== CONNECT TO SERVER ==========");
    write_report("Time: " + get_time());
    write_report("Target IP: " + g_targetIP);

    if (env == nullptr) {
        write_report("❌ env is null!");
        return;
    }

    try {
        if (g_targetIP.empty()) {
            g_targetIP = load_ip();
            if (g_targetIP.empty()) {
                write_report("📌 No IP, using default: " DEFAULT_IP ":" DEFAULT_PORT);
                g_targetIP = DEFAULT_IP ":" DEFAULT_PORT;
                save_ip(g_targetIP);
            }
        }

        // پیدا کردن NetworkManager
        jclass nmClass = env->FindClass("Mirror.NetworkManager");
        if (nmClass == nullptr) {
            env->ExceptionClear();
            nmClass = env->FindClass("NetworkManager");
            if (nmClass == nullptr) {
                env->ExceptionClear();
                write_report("❌ NetworkManager not found!");
                write_log(g_ipResultLog, "❌ NetworkManager not found!");
                return;
            }
        }
        write_report("✅ NetworkManager class found");

        // پیدا کردن instance
        jclass objClass = env->FindClass("UnityEngine.Object");
        jmethodID findObj = env->GetStaticMethodID(objClass, "FindObjectOfType", "(Ljava/lang/Class;)Ljava/lang/Object;");
        jobject nm = env->CallStaticObjectMethod(objClass, findObj, nmClass);
        
        if (nm == nullptr) {
            write_report("❌ NetworkManager instance not found!");
            write_log(g_ipResultLog, "❌ NetworkManager instance not found!");
            return;
        }
        write_report("✅ NetworkManager instance found");

        // تغییر networkAddress
        jfieldID addrField = env->GetFieldID(nmClass, "networkAddress", "Ljava/lang/String;");
        jstring jip = env->NewStringUTF(g_targetIP.c_str());
        env->SetObjectField(nm, addrField, jip);
        env->DeleteLocalRef(jip);
        write_report("✅ networkAddress set to: " + g_targetIP);

        // StartClient
        jmethodID startClient = env->GetMethodID(nmClass, "StartClient", "()V");
        if (startClient == nullptr) {
            env->ExceptionClear();
            startClient = env->GetMethodID(nmClass, "StartHost", "()V");
            if (startClient == nullptr) {
                write_report("❌ StartClient/StartHost not found!");
                return;
            }
            write_report("🔄 Calling StartHost");
        } else {
            write_report("🔄 Calling StartClient");
        }
        
        env->CallVoidMethod(nm, startClient);
        write_report("✅ Connect called successfully!");
        write_log(g_ipResultLog, "✅ Connected to: " + g_targetIP);
        
        disable_menu_buttons();

    } catch (...) {
        write_report("❌ Exception in connect_to_server!");
    }
}

// ======================== منو ========================
jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    jobjectArray ret;
    const char *features[] = {
        OBFUSCATE("Category_🌐 Network Tools"),
        OBFUSCATE("InputText_Enter IP"),           // featNum: 0
        OBFUSCATE("Button_Inject & Disable"),      // featNum: 1
        OBFUSCATE("Button_Connect"),               // featNum: 2
        OBFUSCATE("Button_Show IP"),               // featNum: 3
        OBFUSCATE("RichTextView_📁 /sdcard/Download/lac/"),
    };
    int total = sizeof features / sizeof features[0];
    ret = (jobjectArray)env->NewObjectArray(total, env->FindClass(OBFUSCATE("java/lang/String")), env->NewStringUTF(""));
    for (int i = 0; i < total; i++) {
        env->SetObjectArrayElement(ret, i, env->NewStringUTF(features[i]));
    }
    return ret;
}

// ======================== تغییرات منو ========================
void Changes(JNIEnv *env, jclass clazz, jobject obj, jint featNum, jstring featName,
             jint value, jlong Lvalue, jboolean boolean, jstring text) {

    const char *textStr = nullptr;
    if (text != nullptr) {
        textStr = env->GetStringUTFChars(text, nullptr);
    }

    switch (featNum) {
        case 0:
            if (textStr != nullptr) {
                g_targetIP = textStr;
                save_ip(g_targetIP);
                write_report("📝 IP entered: " + g_targetIP);
                write_log(g_ipResultLog, "📝 IP entered: " + g_targetIP);
            }
            break;

        case 1:
            write_report("🔘 Inject & Disable button pressed");
            write_log(g_ipResultLog, "🔘 Inject & Disable button pressed");
            if (inject_ip_to_ipInput(g_targetIP)) {
                disable_menu_buttons();
            }
            break;

        case 2:
            write_report("🔘 Connect button pressed");
            write_log(g_ipResultLog, "🔘 Connect button pressed");
            if (inject_ip_to_ipInput(g_targetIP)) {
                disable_menu_buttons();
            }
            connect_to_server(env, obj);
            break;

        case 3:
            {
                std::string savedIP = load_ip();
                if (!savedIP.empty()) {
                    write_report("📌 Show IP: " + savedIP);
                    write_log(g_ipResultLog, "📌 Show IP: " + savedIP);
                } else {
                    write_report("❌ No saved IP!");
                    write_log(g_ipResultLog, "❌ No saved IP!");
                }
            }
            break;

        default:
            write_report("Unknown featNum: " + std::to_string(featNum));
            break;
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

    g_targetIP = load_ip();
    if (g_targetIP.empty()) {
        write_report("📌 No saved IP, using default: " DEFAULT_IP ":" DEFAULT_PORT);
        g_targetIP = DEFAULT_IP ":" DEFAULT_PORT;
        save_ip(g_targetIP);
    } else {
        write_report("📌 Loaded IP from file: " + g_targetIP);
    }
    write_debug("📌 Current IP: " + g_targetIP);

    write_report("🔍 Trying get_Instance()...");
    void* instance1 = get_gta_instance();
    if (instance1 != nullptr) {
        g_gtaInstance = instance1;
        g_gtaReady = true;
        write_report("✅ Got instance via get_Instance()");
        if (inject_ip_to_ipInput(g_targetIP)) {
            disable_menu_buttons();
        }
    }

#if defined(__aarch64__)
    if (!g_gtaReady) {
        write_report("🔍 Trying hook on GtaMenuControl.Start()...");
        void* startAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0xF571A4"));
        if (startAddr != nullptr) {
            int res = DobbyHook(startAddr, (dobby_dummy_func_t)hook_GtaMenuStart, (dobby_dummy_func_t*)&orig_GtaMenuStart);
            if (res == 0) {
                write_report("✅ GtaMenuControl.Start() hooked at 0x" + std::to_string((uintptr_t)startAddr));
                write_debug("✅ GtaMenuControl.Start() hooked");
            } else {
                write_report("❌ Failed to hook GtaMenuControl.Start()! error: " + std::to_string(res));
                write_debug("❌ GtaMenuControl.Start() hook failed");
            }
        } else {
            write_report("❌ GtaMenuControl.Start() address not found!");
            write_debug("❌ GtaMenuControl.Start() address not found");
        }
    }
#endif

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