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

// ======================== آفست‌ها ========================
#define OFFSET_GTA_START  0xF571A4      // GtaMenuControl.Start()
#define OFFSET_IP_INPUT   0xC0          // GtaMenuControl.ipInput
#define OFFSET_M_TEXT     0x180         // InputField.m_Text
#define OFFSET_M_TEXT_ALT 0x220         // TMP_InputField.m_Text

// ======================== مسیرها ========================
static std::string g_basePath = "/storage/emulated/0/Download/lac/";
static std::string g_debugLog = g_basePath + "mod_debug.txt";
static std::string g_lastIPFile = g_basePath + "last_ip.txt";
static std::string g_ipResultLog = g_basePath + "ip_result.txt";
static std::string g_crashLog = g_basePath + "crash_log.txt";
static std::string g_reportLog = g_basePath + "injection_report.txt";

// ======================== متغیرها ========================
static std::string g_targetIP = "";
static bool g_crashHandlerInstalled = false;
static void* g_gtaMenuInstance = nullptr;
static bool g_gtaMenuReady = false;

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
        write_debug("IP saved: " + ip);
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

// ======================== کرش‌گیر ========================
static void crash_handler(int sig, siginfo_t *info, void *context) {
    std::ofstream f(g_crashLog, std::ios::app);
    if (!f.is_open()) return;
    f << "\n========================================\n";
    f << "💥 CRASH at " << get_time() << "\n";
    f << "Signal: " << sig << " (" << strsignal(sig) << ")\n";
    f << "Fault address: " << info->si_addr << "\n";
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
    write_debug("✅ Crash handler installed");
}

// ======================== خوندن امن حافظه ========================
static bool safe_mem_read(uintptr_t addr, void* buffer, size_t len) {
    if (addr == 0) return false;
    auto map = KittyMemory::getAddressMap((void*)addr);
    if (!map.readable) return false;
    if (addr < map.startAddress || addr + len > map.endAddress) return false;
    memcpy(buffer, (void*)addr, len);
    return true;
}

// ======================== هوک روی GtaMenuControl.Start ========================
void (*orig_GtaMenuStart)(void *instance);
void hook_GtaMenuStart(void *instance) {
    write_report("========== HOOK TRIGGERED ==========");
    write_report("GtaMenuControl.Start() called at " + get_time());
    write_debug("🎯 GtaMenuControl.Start() called!");
    
    if (instance != nullptr) {
        g_gtaMenuInstance = instance;
        g_gtaMenuReady = true;
        write_report("✅ GtaMenuControl instance saved: 0x" + std::to_string((uintptr_t)instance));
        write_debug("✅ GtaMenuControl instance saved: 0x" + std::to_string((uintptr_t)instance));
        
        g_targetIP = load_ip();
        if (!g_targetIP.empty()) {
            write_report("📌 Loaded IP from file: " + g_targetIP);
            write_debug("📌 Loaded IP: " + g_targetIP);
        } else {
            write_report("⚠️ No IP found in file");
        }
    } else {
        write_report("❌ instance is null!");
    }
    write_report("====================================\n");
    
    if (orig_GtaMenuStart) {
        orig_GtaMenuStart(instance);
    }
}

// ======================== تزریق IP (فقط با KittyMemory، بدون JNI) ========================
static void inject_ip_to_game(JNIEnv* env, jobject obj) {
    write_report("\n========== INJECTION ATTEMPT ==========");
    write_report("Time: " + get_time());
    write_report("Target IP: " + g_targetIP);
    
    if (!g_gtaMenuReady || g_gtaMenuInstance == nullptr) {
        write_report("❌ GtaMenuControl not ready yet");
        write_log(g_ipResultLog, "❌ GtaMenuControl not ready yet!");
        return;
    }

    try {
        if (g_targetIP.empty()) {
            g_targetIP = load_ip();
            if (g_targetIP.empty()) {
                write_report("❌ No IP found in file!");
                write_log(g_ipResultLog, "❌ No IP found!");
                return;
            }
            write_report("📌 IP loaded from file: " + g_targetIP);
        }

        uintptr_t baseAddr = getLibraryAddress("libil2cpp.so");
        if (baseAddr == 0) {
            write_report("❌ libil2cpp.so not loaded!");
            write_log(g_ipResultLog, "❌ libil2cpp.so not loaded!");
            return;
        }

        // ====== 1. گرفتن ipInput از instance (با آفست) ======
        write_report("Step 1: Reading ipInput pointer from GtaMenuControl instance");
        uintptr_t gtaPtr = (uintptr_t)g_gtaMenuInstance;
        uintptr_t inputFieldPtr = 0;
        uintptr_t ipInputAddr = gtaPtr + OFFSET_IP_INPUT;
        
        if (!safe_mem_read(ipInputAddr, &inputFieldPtr, sizeof(uintptr_t))) {
            write_report("❌ Cannot read ipInput at 0x" + std::to_string(ipInputAddr));
            write_log(g_ipResultLog, "❌ Cannot read ipInput!");
            return;
        }
        
        write_report("✅ ipInput pointer: 0x" + std::to_string(inputFieldPtr));
        
        if (inputFieldPtr == 0) {
            write_report("❌ ipInput is null!");
            write_log(g_ipResultLog, "❌ ipInput is null!");
            return;
        }

        // ====== 2. نوشتن IP با m_Text offsets ======
        write_report("\n--- Trying m_Text offsets ---");
        uintptr_t textOffsets[] = {OFFSET_M_TEXT, OFFSET_M_TEXT_ALT, 0x20, 0x28, 0x30, 0x38, 0x40, 0x48, 0x50, 0x58, 0x60, 0x68, 0x70, 0x78, 0x80, 0x88, 0x90, 0x98, 0xA0, 0xA8, 0xB0, 0xB8, 0xC0, 0xC8, 0xD0, 0xD8, 0xE0, 0xE8, 0xF0, 0xF8, 0x100, 0x108, 0x110, 0x118, 0x120, 0x128, 0x130, 0x138, 0x140, 0x148, 0x150, 0x158, 0x160, 0x168, 0x170, 0x178};
        bool injected = false;

        for (int i = 0; i < sizeof(textOffsets)/sizeof(textOffsets[0]) && !injected; i++) {
            uintptr_t mTextAddr = inputFieldPtr + textOffsets[i];
            uintptr_t mTextPtr = 0;
            
            if (!safe_mem_read(mTextAddr, &mTextPtr, sizeof(uintptr_t))) {
                write_report("   Offset 0x" + std::to_string(textOffsets[i]) + ": cannot read pointer");
                continue;
            }
            
            if (mTextPtr == 0) {
                write_report("   Offset 0x" + std::to_string(textOffsets[i]) + ": pointer is null");
                continue;
            }

            // چک کردن اینکه حافظه قابل نوشتن است
            auto map = KittyMemory::getAddressMap((void*)mTextPtr);
            if (!map.writeable) {
                // تلاش برای تغییر protection
                if (KittyMemory::memProtect((void*)mTextPtr, g_targetIP.length() + 1, PROT_READ | PROT_WRITE) != 0) {
                    write_report("   Offset 0x" + std::to_string(textOffsets[i]) + ": cannot make writable");
                    continue;
                }
                write_report("   Offset 0x" + std::to_string(textOffsets[i]) + ": protection changed");
            }

            // نوشتن IP
            if (KittyMemory::memWrite((void*)mTextPtr, g_targetIP.c_str(), g_targetIP.length() + 1)) {
                write_report("✅ IP successfully written at offset 0x" + std::to_string(textOffsets[i]) + " (0x" + std::to_string(mTextPtr) + ")");
                write_log(g_ipResultLog, "✅ IP written at offset 0x" + std::to_string(textOffsets[i]) + ": " + g_targetIP);
                injected = true;
            } else {
                write_report("   Offset 0x" + std::to_string(textOffsets[i]) + ": write failed");
            }
        }

        if (!injected) {
            write_report("❌ All m_Text offsets failed!");
            write_log(g_ipResultLog, "❌ All m_Text offsets failed!");
        }

        write_report("========== INJECTION COMPLETE ==========\n");

    } catch (const std::exception& e) {
        write_report("❌ Exception: " + std::string(e.what()));
        write_log(g_crashLog, "⚠️ Exception: " + std::string(e.what()));
    } catch (...) {
        write_report("❌ Unknown exception caught");
        write_log(g_crashLog, "⚠️ Unknown exception!");
    }
}

// ======================== منو ========================
jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    jobjectArray ret;
    const char *features[] = {
        OBFUSCATE("Category_🌐 Network Tools"),
        OBFUSCATE("InputText_Enter IP"),           // featNum: 0
        OBFUSCATE("Button_Inject IP"),             // featNum: 1
        OBFUSCATE("Button_Show IP"),               // featNum: 2
        OBFUSCATE("RichTextView_Output: /sdcard/Download/lac/"),
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
                write_log(g_ipResultLog, "📝 IP entered: " + g_targetIP);
                write_report("📝 IP entered via menu: " + g_targetIP);
            }
            break;

        case 1:
            write_log(g_ipResultLog, "🔘 Inject button pressed");
            write_report("🔘 Inject button pressed by user");
            inject_ip_to_game(env, obj);
            break;

        case 2:
            {
                std::string savedIP = load_ip();
                if (!savedIP.empty()) {
                    write_log(g_ipResultLog, "📌 Show IP: " + savedIP);
                    write_report("📌 Show IP requested: " + savedIP);
                } else {
                    write_log(g_ipResultLog, "❌ No saved IP!");
                }
            }
            break;

        default:
            break;
    }

    if (textStr != nullptr) {
        env->ReleaseStringUTFChars(text, textStr);
    }
}

// ======================== ترد اصلی ========================
void hack_thread() {
    const char* libName = "libil2cpp.so";
    int waitCount = 0;

    write_debug("⏳ Waiting for " + std::string(libName) + "...");
    write_report("⏳ Waiting for libil2cpp.so to load...");
    while (!isLibraryLoaded(targetLibName) && waitCount < 30) {
        sleep(1);
        waitCount++;
    }

    if (waitCount >= 30) {
        write_debug("⏰ Timeout waiting for libil2cpp.so");
        write_report("❌ Timeout waiting for libil2cpp.so");
        return;
    }

    write_debug("✅ " + std::string(libName) + " loaded!");
    write_report("✅ libil2cpp.so loaded successfully");
    
    g_targetIP = load_ip();
    if (!g_targetIP.empty()) {
        write_debug("📌 Loaded IP from file: " + g_targetIP);
        write_report("📌 Pre-loaded IP from file: " + g_targetIP);
    }

#if defined(__aarch64__)
    // ====== هوک روی GtaMenuControl.Start ======
    void* startAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0xF571A4"));
    if (startAddr != nullptr) {
        int res = DobbyHook(startAddr, (dobby_dummy_func_t)hook_GtaMenuStart, (dobby_dummy_func_t*)&orig_GtaMenuStart);
        if (res == 0) {
            write_debug("✅ GtaMenuControl.Start() hooked at 0x" + std::to_string((uintptr_t)startAddr));
            write_report("✅ GtaMenuControl.Start() hooked at 0x" + std::to_string((uintptr_t)startAddr));
        } else {
            write_debug("❌ Failed to hook GtaMenuControl.Start()! error: " + std::to_string(res));
            write_report("❌ Failed to hook GtaMenuControl.Start()! error: " + std::to_string(res));
        }
    } else {
        write_debug("❌ GtaMenuControl.Start() address not found!");
        write_report("❌ GtaMenuControl.Start() address not found at offset 0xF571A4");
    }
#endif

    write_debug("✅ hack_thread finished");
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
        report << "========== INJECTION REPORT ==========\n";
        report << "Started at: " << get_time() << "\n";
        report << "======================================\n\n";
        report.close();
    }

    write_debug("🚀 lib_main called");
    write_report("🚀 lib_main called - mod loading");
    std::thread(hack_thread).detach();
}