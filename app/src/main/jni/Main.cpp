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
#define OFFSET_IP_INPUT  0xC0

// ======================== مسیرها ========================
static std::string g_basePath = "/storage/emulated/0/Download/lac/";
static std::string g_debugLog = g_basePath + "mod_debug.txt";
static std::string g_lastIPFile = g_basePath + "last_ip.txt";
static std::string g_ipResultLog = g_basePath + "ip_result.txt";
static std::string g_crashLog = g_basePath + "crash_log.txt";
static std::string g_dumpLog = g_basePath + "object_dump.txt";

// ======================== متغیرها ========================
static std::string g_targetIP = "";
static bool g_crashHandlerInstalled = false;

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

// ======================== دامپ کامل آبجکت ========================
static void dump_object(uintptr_t objPtr) {
    if (objPtr == 0) return;
    
    write_log(g_dumpLog, "\n========== OBJECT DUMP ==========");
    write_log(g_dumpLog, "Object pointer: 0x" + std::to_string(objPtr));
    write_log(g_dumpLog, "Time: " + get_time());
    write_log(g_dumpLog, "----------------------------------------");
    
    // دامپ 0x200 بایت
    for (int offset = 0; offset < 0x200; offset += 16) {
        unsigned char data[16];
        if (!safe_mem_read(objPtr + offset, data, 16)) {
            continue;
        }
        
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
        if (!safe_mem_read(objPtr + offset, &ptr, sizeof(uintptr_t))) continue;
        
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
                    write_log(g_dumpLog, "+0x" + std::to_string(offset) + " → 0x" + std::to_string(ptr) + " → \"" + std::string(buffer) + "\"");
                }
            }
        }
    }
    
    write_log(g_dumpLog, "========== DUMP END ==========");
}

// ======================== تزریق IP با دامپ ========================
static void inject_ip_to_game() {
    try {
        if (g_targetIP.empty()) {
            g_targetIP = load_ip();
            if (g_targetIP.empty()) {
                write_log(g_ipResultLog, "❌ No IP found!");
                return;
            }
        }

        uintptr_t baseAddr = getLibraryAddress("libil2cpp.so");
        if (baseAddr == 0) {
            write_log(g_ipResultLog, "❌ libil2cpp.so not loaded!");
            return;
        }

        // گرفتن پوینتر ipInput
        uintptr_t addr = baseAddr + OFFSET_IP_INPUT;
        uintptr_t inputFieldPtr = 0;
        if (!safe_mem_read(addr, &inputFieldPtr, sizeof(uintptr_t))) {
            write_log(g_ipResultLog, "❌ Cannot read ipInput pointer!");
            return;
        }

        write_log(g_ipResultLog, "🔄 Injecting IP: " + g_targetIP);
        write_log(g_ipResultLog, "   InputField pointer: 0x" + std::to_string(inputFieldPtr));

        if (inputFieldPtr == 0) {
            write_log(g_ipResultLog, "❌ InputField pointer is null!");
            return;
        }

        // ====== دامپ کامل آبجکت ======
        dump_object(inputFieldPtr);
        write_log(g_ipResultLog, "📦 Object dumped to object_dump.txt");

        // ====== اسکن آفست‌های احتمالی ======
        uintptr_t textOffsets[] = {
            0x20, 0x28, 0x30, 0x38, 0x40, 0x48, 0x50,
            0x58, 0x60, 0x68, 0x70, 0x78, 0x80, 0x88,
            0x90, 0x98, 0xA0, 0xA8, 0xB0, 0xB8, 0xC0,
            0x180, 0x108, 0x110, 0x148, 0x150, 0x158
        };
        bool injected = false;

        for (int i = 0; i < sizeof(textOffsets)/sizeof(textOffsets[0]) && !injected; i++) {
            uintptr_t textPtrAddr = inputFieldPtr + textOffsets[i];
            uintptr_t textPtr = 0;
            if (!safe_mem_read(textPtrAddr, &textPtr, sizeof(uintptr_t))) continue;

            if (textPtr != 0) {
                auto map = KittyMemory::getAddressMap((void*)textPtr);
                if (map.writeable) {
                    KittyMemory::memWrite((void*)textPtr, g_targetIP.c_str(), g_targetIP.length() + 1);
                    write_log(g_ipResultLog, "✅ IP written at offset +0x" + std::to_string(textOffsets[i]) + " (0x" + std::to_string(textPtr) + ")");
                    injected = true;
                }
            }
        }

        if (!injected) {
            write_log(g_ipResultLog, "❌ Could not find writable m_Text field!");
        }

    } catch (...) {
        write_log(g_crashLog, "⚠️ Exception in inject_ip_to_game!");
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
            }
            break;

        case 1:
            write_log(g_ipResultLog, "🔘 Inject button pressed");
            inject_ip_to_game();
            break;

        case 2:
            {
                std::string savedIP = load_ip();
                if (!savedIP.empty()) {
                    write_log(g_ipResultLog, "📌 Show IP: " + savedIP);
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
    while (!isLibraryLoaded(targetLibName) && waitCount < 30) {
        sleep(1);
        waitCount++;
    }

    if (waitCount >= 30) {
        write_debug("⏰ Timeout");
        return;
    }

    write_debug("✅ " + std::string(libName) + " loaded!");
    g_targetIP = load_ip();
    if (!g_targetIP.empty()) {
        write_debug("📌 Loaded IP: " + g_targetIP);
    }
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