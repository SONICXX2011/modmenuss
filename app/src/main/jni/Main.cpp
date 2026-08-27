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
#include <iomanip>
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

// ======================== آفست‌ها ========================
#define OFFSET_GTA_START  0xF571A4      // GtaMenuControl.Start()
#define OFFSET_IP_INPUT   0xC0          // GtaMenuControl.ipInput

// ======================== مسیرها ========================
static std::string g_basePath = "/storage/emulated/0/Download/lac/";
static std::string g_debugLog = g_basePath + "mod_debug.txt";
static std::string g_lastIPFile = g_basePath + "last_ip.txt";
static std::string g_ipResultLog = g_basePath + "ip_result.txt";
static std::string g_crashLog = g_basePath + "crash_log.txt";
static std::string g_dumpLog = g_basePath + "inputfield_dump.txt";

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
    write_debug("🎯 GtaMenuControl.Start() called!");
    
    if (instance != nullptr) {
        g_gtaMenuInstance = instance;
        g_gtaMenuReady = true;
        write_debug("✅ GtaMenuControl instance saved: 0x" + std::to_string((uintptr_t)instance));
        
        g_targetIP = load_ip();
        if (!g_targetIP.empty()) {
            write_debug("📌 Loaded IP: " + g_targetIP);
        }
    }
    
    if (orig_GtaMenuStart) {
        orig_GtaMenuStart(instance);
    }
}

// ======================== دامپ کامل InputField ========================
static void dump_inputfield() {
    if (!g_gtaMenuReady || g_gtaMenuInstance == nullptr) {
        write_debug("❌ GtaMenuControl not ready yet!");
        return;
    }

    try {
        uintptr_t baseAddr = getLibraryAddress("libil2cpp.so");
        if (baseAddr == 0) {
            write_debug("❌ libil2cpp.so not loaded!");
            return;
        }

        // گرفتن ipInput pointer
        uintptr_t gtaPtr = (uintptr_t)g_gtaMenuInstance;
        uintptr_t inputFieldPtr = 0;
        uintptr_t ipInputAddr = gtaPtr + OFFSET_IP_INPUT;
        
        if (!safe_mem_read(ipInputAddr, &inputFieldPtr, sizeof(uintptr_t))) {
            write_debug("❌ Cannot read ipInput!");
            return;
        }
        
        write_debug("✅ ipInput pointer: 0x" + std::to_string(inputFieldPtr));
        
        if (inputFieldPtr == 0) {
            write_debug("❌ ipInput is null!");
            return;
        }

        write_log(g_dumpLog, "========== INPUTFIELD DUMP ==========");
        write_log(g_dumpLog, "Time: " + get_time());
        write_log(g_dumpLog, "InputField pointer: 0x" + std::to_string(inputFieldPtr));
        write_log(g_dumpLog, "----------------------------------------");

        // دامپ 0x200 بایت
        for (int offset = 0; offset < 0x200; offset += 16) {
            unsigned char data[16];
            if (!safe_mem_read(inputFieldPtr + offset, data, 16)) continue;

            std::ostringstream line;
            line << "+0x" << std::setw(3) << std::setfill('0') << std::hex << offset << ": ";
            for (int i = 0; i < 16; i++) {
                line << std::setw(2) << std::setfill('0') << (int)data[i] << " ";
            }
            line << " ";
            for (int i = 0; i < 16; i++) {
                char c = (data[i] >= 32 && data[i] <= 126) ? (char)data[i] : '.';
                line << c;
            }

            bool hasData = false;
            for (int i = 0; i < 16; i++) {
                if (data[i] != 0) { hasData = true; break; }
            }

            if (hasData) {
                write_log(g_dumpLog, line.str());
            }
        }

        // پیدا کردن string pointer ها
        write_log(g_dumpLog, "\n🔍 String pointers found:");
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
                        write_log(g_dumpLog, "+0x" + std::to_string(offset) + ": 0x" + std::to_string(ptr) + " → \"" + std::string(buffer) + "\"");
                    }
                }
            }
        }

        write_log(g_dumpLog, "========== DUMP END ==========");
        write_debug("✅ InputField dump saved to inputfield_dump.txt");

    } catch (...) {
        write_debug("⚠️ Exception in dump_inputfield!");
    }
}

// ======================== تزریق IP ========================
static void inject_ip_to_game() {
    if (!g_gtaMenuReady || g_gtaMenuInstance == nullptr) {
        write_debug("❌ GtaMenuControl not ready yet!");
        return;
    }

    try {
        if (g_targetIP.empty()) {
            g_targetIP = load_ip();
            if (g_targetIP.empty()) {
                write_debug("❌ No IP found!");
                return;
            }
        }

        uintptr_t baseAddr = getLibraryAddress("libil2cpp.so");
        if (baseAddr == 0) {
            write_debug("❌ libil2cpp.so not loaded!");
            return;
        }

        // گرفتن ipInput pointer
        uintptr_t gtaPtr = (uintptr_t)g_gtaMenuInstance;
        uintptr_t inputFieldPtr = 0;
        uintptr_t ipInputAddr = gtaPtr + OFFSET_IP_INPUT;
        
        if (!safe_mem_read(ipInputAddr, &inputFieldPtr, sizeof(uintptr_t))) {
            write_debug("❌ Cannot read ipInput!");
            return;
        }
        
        write_debug("✅ ipInput pointer: 0x" + std::to_string(inputFieldPtr));
        
        if (inputFieldPtr == 0) {
            write_debug("❌ ipInput is null!");
            return;
        }

        // ====== آفست‌های m_Text ======
        uintptr_t textOffsets[] = {0x180, 0x220, 0x20, 0x28, 0x30, 0x38, 0x40, 0x48, 0x50, 0x58, 0x60, 0x68, 0x70, 0x78, 0x80, 0x88, 0x90, 0x98, 0xA0, 0xA8, 0xB0, 0xB8, 0xC0, 0xC8, 0xD0, 0xD8, 0xE0, 0xE8, 0xF0, 0xF8, 0x100, 0x108, 0x110, 0x118, 0x120, 0x128, 0x130, 0x138, 0x140, 0x148, 0x150, 0x158, 0x160, 0x168, 0x170, 0x178, 0x188, 0x190, 0x198, 0x1A0, 0x1A8, 0x1B0, 0x1B8, 0x1C0, 0x1C8, 0x1D0, 0x1D8, 0x1E0, 0x1E8, 0x1F0, 0x1F8, 0x200, 0x208, 0x210, 0x218, 0x228, 0x230, 0x238, 0x240, 0x248, 0x250, 0x258, 0x260, 0x268, 0x270, 0x278, 0x280, 0x288, 0x290, 0x298, 0x2A0, 0x2A8, 0x2B0, 0x2B8, 0x2C0, 0x2C8, 0x2D0, 0x2D8, 0x2E0, 0x2E8, 0x2F0, 0x2F8, 0x300, 0x308, 0x310, 0x318, 0x320, 0x328, 0x330, 0x338, 0x340, 0x348, 0x350, 0x358, 0x360, 0x368, 0x370, 0x378, 0x380, 0x388, 0x390, 0x398, 0x3A0, 0x3A8, 0x3B0, 0x3B8, 0x3C0, 0x3C8, 0x3D0, 0x3D8, 0x3E0, 0x3E8, 0x3F0};
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
                write_debug("✅ IP written at offset 0x" + std::to_string(textOffsets[i]) + " (0x" + std::to_string(mTextPtr) + ")");
                write_log(g_ipResultLog, "✅ IP written at offset 0x" + std::to_string(textOffsets[i]) + ": " + g_targetIP);
                injected = true;
            }
        }

        if (!injected) {
            write_debug("❌ All offsets failed!");
            // دامپ بگیر تا ببینیم کجا اشتباهه
            dump_inputfield();
        }

    } catch (...) {
        write_debug("⚠️ Exception in inject_ip_to_game!");
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
        OBFUSCATE("Button_📡 Dump InputField"),    // featNum: 3
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
        case 0: // Input IP
            if (textStr != nullptr) {
                g_targetIP = textStr;
                save_ip(g_targetIP);
                write_debug("IP entered: " + g_targetIP);
                write_log(g_ipResultLog, "📝 IP entered: " + g_targetIP);
            }
            break;

        case 1: // Button_Inject IP
            write_debug("🔘 Inject button pressed");
            write_log(g_ipResultLog, "🔘 Inject button pressed");
            inject_ip_to_game();
            break;

        case 2: // Button_Show IP
            {
                std::string savedIP = load_ip();
                if (!savedIP.empty()) {
                    write_debug("📌 Show IP: " + savedIP);
                    write_log(g_ipResultLog, "📌 Show IP: " + savedIP);
                } else {
                    write_debug("❌ No saved IP!");
                }
            }
            break;

        case 3: // Button_📡 Dump InputField
            write_debug("🔘 Dump InputField button pressed");
            dump_inputfield();
            break;

        default:
            write_debug("Unknown featNum: " + std::to_string(featNum));
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
    while (!isLibraryLoaded(targetLibName) && waitCount < 30) {
        sleep(1);
        waitCount++;
    }

    if (waitCount >= 30) {
        write_debug("⏰ Timeout waiting for libil2cpp.so");
        return;
    }

    write_debug("✅ " + std::string(libName) + " loaded!");
    
    g_targetIP = load_ip();
    if (!g_targetIP.empty()) {
        write_debug("📌 Loaded IP from file: " + g_targetIP);
    }

#if defined(__aarch64__)
    void* startAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0xF571A4"));
    if (startAddr != nullptr) {
        int res = DobbyHook(startAddr, (dobby_dummy_func_t)hook_GtaMenuStart, (dobby_dummy_func_t*)&orig_GtaMenuStart);
        if (res == 0) {
            write_debug("✅ GtaMenuControl.Start() hooked at 0x" + std::to_string((uintptr_t)startAddr));
        } else {
            write_debug("❌ Failed to hook GtaMenuControl.Start()! error: " + std::to_string(res));
        }
    } else {
        write_debug("❌ GtaMenuControl.Start() address not found!");
    }
#endif

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

    write_debug("🚀 lib_main called");
    std::thread(hack_thread).detach();
}