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

// ======================== پیدا کردن m_Text درست ========================
static void find_correct_mtext(JNIEnv* env, jobject obj) {
    if (!g_gtaMenuReady || g_gtaMenuInstance == nullptr) {
        write_report("❌ GtaMenuControl not ready yet");
        return;
    }

    try {
        uintptr_t baseAddr = getLibraryAddress("libil2cpp.so");
        if (baseAddr == 0) {
            write_report("❌ libil2cpp.so not loaded!");
            return;
        }

        // گرفتن ipInput pointer
        uintptr_t gtaPtr = (uintptr_t)g_gtaMenuInstance;
        uintptr_t inputFieldPtr = 0;
        uintptr_t ipInputAddr = gtaPtr + OFFSET_IP_INPUT;
        
        if (!safe_mem_read(ipInputAddr, &inputFieldPtr, sizeof(uintptr_t))) {
            write_report("❌ Cannot read ipInput!");
            return;
        }
        
        write_report("✅ ipInput pointer: 0x" + std::to_string(inputFieldPtr));
        
        if (inputFieldPtr == 0) {
            write_report("❌ ipInput is null!");
            return;
        }

        // ====== دامپ کامل InputField برای پیدا کردن m_Text ======
        write_report("\n📦 Dumping InputField object:");
        for (int offset = 0; offset < 0x200; offset += 8) {
            uintptr_t ptr = 0;
            if (!safe_mem_read(inputFieldPtr + offset, &ptr, sizeof(uintptr_t))) continue;
            
            if (ptr != 0) {
                char buffer[50] = {0};
                if (safe_mem_read(ptr, buffer, 49)) {
                    bool isString = true;
                    for (int i = 0; i < 49 && buffer[i] != 0; i++) {
                        if (buffer[i] < 32 || buffer[i] > 126) {
                            isString = false;
                            break;
                        }
                    }
                    if (isString) {
                        write_report("   +0x" + std::to_string(offset) + ": 0x" + std::to_string(ptr) + " → \"" + std::string(buffer) + "\"");
                    }
                }
            }
        }

        write_report("========== DUMP COMPLETE ==========\n");

    } catch (...) {
        write_report("❌ Exception in find_correct_mtext!");
    }
}

// ======================== تزریق IP ========================
static void inject_ip_to_game(JNIEnv* env, jobject obj) {
    if (!g_gtaMenuReady || g_gtaMenuInstance == nullptr) {
        write_report("❌ GtaMenuControl not ready yet");
        return;
    }

    try {
        if (g_targetIP.empty()) {
            g_targetIP = load_ip();
            if (g_targetIP.empty()) {
                write_report("❌ No IP found!");
                return;
            }
        }

        uintptr_t baseAddr = getLibraryAddress("libil2cpp.so");
        if (baseAddr == 0) {
            write_report("❌ libil2cpp.so not loaded!");
            return;
        }

        // گرفتن ipInput pointer
        uintptr_t gtaPtr = (uintptr_t)g_gtaMenuInstance;
        uintptr_t inputFieldPtr = 0;
        uintptr_t ipInputAddr = gtaPtr + OFFSET_IP_INPUT;
        
        if (!safe_mem_read(ipInputAddr, &inputFieldPtr, sizeof(uintptr_t))) {
            write_report("❌ Cannot read ipInput!");
            return;
        }
        
        write_report("✅ ipInput pointer: 0x" + std::to_string(inputFieldPtr));
        
        if (inputFieldPtr == 0) {
            write_report("❌ ipInput is null!");
            return;
        }

        // ====== پیدا کردن m_Text با آفست‌های مختلف ======
        uintptr_t textOffsets[] = {0x20, 0x28, 0x30, 0x38, 0x40, 0x48, 0x50, 0x58, 0x60, 0x68, 0x70, 0x78, 0x80, 0x88, 0x90, 0x98, 0xA0, 0xA8, 0xB0, 0xB8, 0xC0, 0xC8, 0xD0, 0xD8, 0xE0, 0xE8, 0xF0, 0xF8, 0x100, 0x108, 0x110, 0x118, 0x120, 0x128, 0x130, 0x138, 0x140, 0x148, 0x150, 0x158, 0x160, 0x168, 0x170, 0x178, 0x180, 0x188, 0x190, 0x198, 0x1A0, 0x1A8, 0x1B0, 0x1B8, 0x1C0, 0x1C8, 0x1D0, 0x1D8, 0x1E0, 0x1E8, 0x1F0, 0x1F8, 0x200, 0x208, 0x210, 0x218, 0x220};
        bool injected = false;

        for (int i = 0; i < sizeof(textOffsets)/sizeof(textOffsets[0]) && !injected; i++) {
            uintptr_t mTextAddr = inputFieldPtr + textOffsets[i];
            uintptr_t mTextPtr = 0;
            
            if (!safe_mem_read(mTextAddr, &mTextPtr, sizeof(uintptr_t))) continue;
            if (mTextPtr == 0) continue;

            // چک کردن اینکه آیا pointer به رشته معتبر اشاره میکنه
            char test[10] = {0};
            if (!safe_mem_read(mTextPtr, test, 5)) continue;
            
            bool isString = true;
            for (int j = 0; j < 5 && test[j] != 0; j++) {
                if (test[j] < 32 || test[j] > 126) { isString = false; break; }
            }
            if (!isString) continue;

            // نوشتن IP
            if (KittyMemory::memWrite((void*)mTextPtr, g_targetIP.c_str(), g_targetIP.length() + 1)) {
                write_report("✅ IP written at offset 0x" + std::to_string(textOffsets[i]) + " (0x" + std::to_string(mTextPtr) + ")");
                write_log(g_ipResultLog, "✅ IP written at offset 0x" + std::to_string(textOffsets[i]) + ": " + g_targetIP);
                injected = true;
            }
        }

        if (!injected) {
            write_report("❌ All offsets failed!");
            find_correct_mtext(env, obj);
        }

    } catch (...) {
        write_report("❌ Exception in inject_ip_to_game!");
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
            }
            break;

        case 1:
            write_report("🔘 Inject button pressed");
            inject_ip_to_game(env, obj);
            break;

        case 2:
            {
                std::string savedIP = load_ip();
                if (!savedIP.empty()) {
                    write_report("📌 Show IP: " + savedIP);
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

void hack_thread() {
    const char* libName = "libil2cpp.so";
    int waitCount = 0;

    while (!isLibraryLoaded(targetLibName) && waitCount < 30) {
        sleep(1);
        waitCount++;
    }

    if (waitCount >= 30) return;

#if defined(__aarch64__)
    void* startAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0xF571A4"));
    if (startAddr != nullptr) {
        DobbyHook(startAddr, (dobby_dummy_func_t)hook_GtaMenuStart, (dobby_dummy_func_t*)&orig_GtaMenuStart);
    }
#endif
}

__attribute__((constructor))
void lib_main() {
    create_directory();
    install_crash_handler();
    std::thread(hack_thread).detach();
}