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

// ======================== لیست آفست‌های احتمالی m_Text ========================
static uintptr_t g_mTextOffsets[] = {
    0x20, 0x28, 0x30, 0x38, 0x40, 0x48, 0x50, 0x58, 0x60, 0x68,
    0x70, 0x78, 0x80, 0x88, 0x90, 0x98, 0xA0, 0xA8, 0xB0, 0xB8,
    0xC0, 0xC8, 0xD0, 0xD8, 0xE0, 0xE8, 0xF0, 0xF8, 0x100, 0x108,
    0x110, 0x118, 0x120, 0x128, 0x130, 0x138, 0x140, 0x148, 0x150, 0x158,
    0x160, 0x168, 0x170, 0x178, 0x180, 0x188, 0x190, 0x198, 0x1A0, 0x1A8,
    0x1B0, 0x1B8, 0x1C0, 0x1C8, 0x1D0, 0x1D8, 0x1E0, 0x1E8, 0x1F0, 0x1F8,
    0x200, 0x208, 0x210, 0x218, 0x220, 0x228, 0x230, 0x238, 0x240, 0x248,
    0x250, 0x258, 0x260, 0x268, 0x270, 0x278, 0x280, 0x288, 0x290, 0x298,
    0x2A0, 0x2A8, 0x2B0, 0x2B8, 0x2C0, 0x2C8, 0x2D0, 0x2D8, 0x2E0, 0x2E8,
    0x2F0, 0x2F8, 0x300, 0x308, 0x310, 0x318, 0x320, 0x328, 0x330, 0x338,
    0x340, 0x348, 0x350, 0x358, 0x360, 0x368, 0x370, 0x378, 0x380, 0x388,
    0x390, 0x398, 0x3A0, 0x3A8, 0x3B0, 0x3B8, 0x3C0, 0x3C8, 0x3D0, 0x3D8,
    0x3E0, 0x3E8, 0x3F0, 0x3F8, 0x400
};
static int g_mTextOffsetsCount = sizeof(g_mTextOffsets) / sizeof(g_mTextOffsets[0]);

// ======================== مسیرها ========================
static std::string g_basePath = "/storage/emulated/0/Download/lac/";
static std::string g_debugLog = g_basePath + "mod_debug.txt";
static std::string g_lastIPFile = g_basePath + "last_ip.txt";
static std::string g_ipResultLog = g_basePath + "ip_result.txt";
static std::string g_crashLog = g_basePath + "crash_log.txt";
static std::string g_reportLog = g_basePath + "report.txt";

// ======================== متغیرها ========================
static std::string g_targetIP = "";
static bool g_crashHandlerInstalled = false;
static void* g_gtaMenuInstance = nullptr;
static bool g_gtaMenuReady = false;
static uintptr_t g_foundOffset = 0;

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

// ======================== تست همه آفست‌های m_Text ========================
static void find_and_inject_mtext() {
    write_report("\n========== FINDING m_Text OFFSET ==========");
    write_report("Time: " + get_time());
    write_report("Target IP: " + g_targetIP);
    
    if (!g_gtaMenuReady || g_gtaMenuInstance == nullptr) {
        write_report("❌ GtaMenuControl not ready yet!");
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

        // گرفتن ipInput
        write_report("Step 1: Getting ipInput from GtaMenuControl instance");
        uintptr_t gtaPtr = (uintptr_t)g_gtaMenuInstance;
        uintptr_t inputFieldPtr = 0;
        uintptr_t ipInputAddr = gtaPtr + OFFSET_IP_INPUT;
        
        if (!safe_mem_read(ipInputAddr, &inputFieldPtr, sizeof(uintptr_t))) {
            write_report("❌ Cannot read ipInput at 0x" + std::to_string(ipInputAddr));
            write_log(g_ipResultLog, "❌ Cannot read ipInput!");
            return;
        }
        
        write_report("✅ ipInput pointer: 0x" + std::to_string(inputFieldPtr));
        write_log(g_ipResultLog, "✅ ipInput pointer: 0x" + std::to_string(inputFieldPtr));
        
        if (inputFieldPtr == 0) {
            write_report("❌ ipInput is null!");
            write_log(g_ipResultLog, "❌ ipInput is null!");
            return;
        }

        // ====== تست همه آفست‌ها ======
        write_report("\n🔍 Testing " + std::to_string(g_mTextOffsetsCount) + " offsets for m_Text:");
        write_report("----------------------------------------");
        
        bool found = false;
        uintptr_t successfulOffset = 0;
        uintptr_t successfulPtr = 0;

        for (int i = 0; i < g_mTextOffsetsCount && !found; i++) {
            uintptr_t offset = g_mTextOffsets[i];
            uintptr_t mTextAddr = inputFieldPtr + offset;
            uintptr_t mTextPtr = 0;
            
            if (!safe_mem_read(mTextAddr, &mTextPtr, sizeof(uintptr_t))) {
                write_report("  Offset 0x" + std::to_string(offset) + ": cannot read pointer");
                continue;
            }
            
            if (mTextPtr == 0) {
                write_report("  Offset 0x" + std::to_string(offset) + ": pointer is null");
                continue;
            }

            // چک کردن اینکه آیا pointer به رشته معتبر اشاره میکنه
            char test[10] = {0};
            if (!safe_mem_read(mTextPtr, test, 5)) {
                write_report("  Offset 0x" + std::to_string(offset) + ": cannot read memory at 0x" + std::to_string(mTextPtr));
                continue;
            }
            
            bool isString = true;
            for (int j = 0; j < 5 && test[j] != 0; j++) {
                if (test[j] < 32 || test[j] > 126) {
                    isString = false;
                    break;
                }
            }
            
            if (!isString) {
                write_report("  Offset 0x" + std::to_string(offset) + ": pointer 0x" + std::to_string(mTextPtr) + " is not a valid string");
                continue;
            }

            // چک کردن اینکه حافظه قابل نوشتن است
            auto map = KittyMemory::getAddressMap((void*)mTextPtr);
            if (!map.writeable) {
                // تلاش برای تغییر protection
                if (KittyMemory::memProtect((void*)mTextPtr, g_targetIP.length() + 1, PROT_READ | PROT_WRITE) != 0) {
                    write_report("  Offset 0x" + std::to_string(offset) + ": cannot make writable");
                    continue;
                }
                write_report("  Offset 0x" + std::to_string(offset) + ": protection changed to RW");
            }

            // نوشتن IP
            if (KittyMemory::memWrite((void*)mTextPtr, g_targetIP.c_str(), g_targetIP.length() + 1)) {
                write_report("✅ SUCCESS at offset 0x" + std::to_string(offset) + " (0x" + std::to_string(mTextPtr) + ")");
                write_log(g_ipResultLog, "✅ IP written at offset 0x" + std::to_string(offset) + ": " + g_targetIP);
                successfulOffset = offset;
                successfulPtr = mTextPtr;
                g_foundOffset = offset;
                found = true;
            } else {
                write_report("  Offset 0x" + std::to_string(offset) + ": write failed");
            }
        }

        if (found) {
            write_report("\n🎯 SUCCESSFUL OFFSET FOUND: 0x" + std::to_string(successfulOffset));
            write_report("   Pointer: 0x" + std::to_string(successfulPtr));
            write_report("   IP: " + g_targetIP);
        } else {
            write_report("\n❌ No working m_Text offset found!");
            write_log(g_ipResultLog, "❌ No working m_Text offset found!");
        }

        write_report("========== SCAN COMPLETE ==========\n");

    } catch (const std::exception& e) {
        write_report("❌ Exception: " + std::string(e.what()));
        write_log(g_crashLog, "⚠️ Exception: " + std::string(e.what()));
    } catch (...) {
        write_report("❌ Unknown exception caught");
        write_log(g_crashLog, "⚠️ Unknown exception!");
    }
}

// ======================== تزریق IP با آفست پیدا شده ========================
static void inject_ip_to_game() {
    if (g_foundOffset == 0) {
        write_report("⚠️ No offset found yet, running scan first...");
        find_and_inject_mtext();
        return;
    }

    write_report("\n========== INJECTION WITH FOUND OFFSET ==========");
    write_report("Time: " + get_time());
    write_report("Using offset: 0x" + std::to_string(g_foundOffset));
    write_report("Target IP: " + g_targetIP);
    
    if (!g_gtaMenuReady || g_gtaMenuInstance == nullptr) {
        write_report("❌ GtaMenuControl not ready yet!");
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
        }

        uintptr_t baseAddr = getLibraryAddress("libil2cpp.so");
        if (baseAddr == 0) {
            write_report("❌ libil2cpp.so not loaded!");
            write_log(g_ipResultLog, "❌ libil2cpp.so not loaded!");
            return;
        }

        uintptr_t gtaPtr = (uintptr_t)g_gtaMenuInstance;
        uintptr_t inputFieldPtr = 0;
        uintptr_t ipInputAddr = gtaPtr + OFFSET_IP_INPUT;
        
        if (!safe_mem_read(ipInputAddr, &inputFieldPtr, sizeof(uintptr_t))) {
            write_report("❌ Cannot read ipInput!");
            write_log(g_ipResultLog, "❌ Cannot read ipInput!");
            return;
        }
        
        write_report("✅ ipInput pointer: 0x" + std::to_string(inputFieldPtr));
        
        if (inputFieldPtr == 0) {
            write_report("❌ ipInput is null!");
            write_log(g_ipResultLog, "❌ ipInput is null!");
            return;
        }

        uintptr_t mTextAddr = inputFieldPtr + g_foundOffset;
        uintptr_t mTextPtr = 0;
        
        if (!safe_mem_read(mTextAddr, &mTextPtr, sizeof(uintptr_t))) {
            write_report("❌ Cannot read m_Text pointer!");
            write_log(g_ipResultLog, "❌ Cannot read m_Text pointer!");
            return;
        }
        
        write_report("✅ m_Text pointer: 0x" + std::to_string(mTextPtr));
        
        if (mTextPtr == 0) {
            write_report("❌ m_Text is null!");
            write_log(g_ipResultLog, "❌ m_Text is null!");
            return;
        }

        auto map = KittyMemory::getAddressMap((void*)mTextPtr);
        if (!map.writeable) {
            if (KittyMemory::memProtect((void*)mTextPtr, g_targetIP.length() + 1, PROT_READ | PROT_WRITE) != 0) {
                write_report("❌ Cannot make m_Text writable!");
                write_log(g_ipResultLog, "❌ Cannot make m_Text writable!");
                return;
            }
            write_report("✅ Memory protection changed to RW");
        }

        if (KittyMemory::memWrite((void*)mTextPtr, g_targetIP.c_str(), g_targetIP.length() + 1)) {
            write_report("✅ IP successfully written to m_Text at 0x" + std::to_string(mTextPtr));
            write_log(g_ipResultLog, "✅ IP written: " + g_targetIP);
        } else {
            write_report("❌ Failed to write to m_Text");
            write_log(g_ipResultLog, "❌ Failed to write to m_Text");
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
        OBFUSCATE("Button_🔍 Find m_Text"),        // featNum: 1
        OBFUSCATE("Button_Inject IP"),             // featNum: 2
        OBFUSCATE("Button_Show IP"),               // featNum: 3
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
                write_report("📝 IP entered via menu: " + g_targetIP);
                write_log(g_ipResultLog, "📝 IP entered: " + g_targetIP);
            }
            break;

        case 1: // Button_🔍 Find m_Text
            write_report("🔍 Find m_Text button pressed by user");
            write_log(g_ipResultLog, "🔍 Find m_Text button pressed");
            find_and_inject_mtext();
            break;

        case 2: // Button_Inject IP
            write_report("🔘 Inject button pressed by user");
            write_log(g_ipResultLog, "🔘 Inject button pressed");
            inject_ip_to_game();
            break;

        case 3: // Button_Show IP
            {
                std::string savedIP = load_ip();
                if (!savedIP.empty()) {
                    write_report("📌 Show IP requested: " + savedIP);
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
    const char* libName = "libil2cpp.so";
    int waitCount = 0;

    write_report("⏳ Waiting for libil2cpp.so to load...");
    write_debug("⏳ Waiting for " + std::string(libName) + "...");
    while (!isLibraryLoaded(targetLibName) && waitCount < 30) {
        sleep(1);
        waitCount++;
    }

    if (waitCount >= 30) {
        write_report("❌ Timeout waiting for libil2cpp.so");
        write_debug("⏰ Timeout waiting for libil2cpp.so");
        return;
    }

    write_report("✅ libil2cpp.so loaded successfully");
    write_debug("✅ " + std::string(libName) + " loaded!");
    
    g_targetIP = load_ip();
    if (!g_targetIP.empty()) {
        write_report("📌 Pre-loaded IP from file: " + g_targetIP);
        write_debug("📌 Loaded IP from file: " + g_targetIP);
    }

#if defined(__aarch64__)
    void* startAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0xF571A4"));
    if (startAddr != nullptr) {
        int res = DobbyHook(startAddr, (dobby_dummy_func_t)hook_GtaMenuStart, (dobby_dummy_func_t*)&orig_GtaMenuStart);
        if (res == 0) {
            write_report("✅ GtaMenuControl.Start() hooked at 0x" + std::to_string((uintptr_t)startAddr));
            write_debug("✅ GtaMenuControl.Start() hooked at 0x" + std::to_string((uintptr_t)startAddr));
        } else {
            write_report("❌ Failed to hook GtaMenuControl.Start()! error: " + std::to_string(res));
            write_debug("❌ Failed to hook GtaMenuControl.Start()! error: " + std::to_string(res));
        }
    } else {
        write_report("❌ GtaMenuControl.Start() address not found!");
        write_debug("❌ GtaMenuControl.Start() address not found!");
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
        report << "========== INJECTION REPORT ==========\n";
        report << "Started at: " << get_time() << "\n";
        report << "======================================\n\n";
        report.close();
    }

    write_report("🚀 lib_main called - mod loading");
    write_debug("🚀 lib_main called");
    std::thread(hack_thread).detach();
}