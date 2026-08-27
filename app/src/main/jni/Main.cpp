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

// ======================== مسیرها ========================
static std::string g_basePath = "/storage/emulated/0/Download/lac/";
static std::string g_debugLog = g_basePath + "mod_debug.txt";
static std::string g_lastIPFile = g_basePath + "last_ip.txt";
static std::string g_ipResultLog = g_basePath + "ip_result.txt";
static std::string g_crashLog = g_basePath + "crash_log.txt";
static std::string g_scanLog = g_basePath + "scan_result.txt";

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
    
    // چک با KittyMemory
    auto map = KittyMemory::getAddressMap((void*)addr);
    if (!map.readable) return false;
    
    // چک محدوده
    if (addr < map.startAddress || addr + len > map.endAddress) return false;
    
    // خوندن
    memcpy(buffer, (void*)addr, len);
    return true;
}

// ======================== اسکن امن رشته IP ========================
static void scan_for_ip_string() {
    try {
        write_log(g_scanLog, "========== SCAN FOR IP STRING ==========");
        write_log(g_scanLog, "Time: " + get_time());

        // IP رو از فایل بخون
        std::string searchStr = load_ip();
        if (searchStr.empty()) {
            write_log(g_scanLog, "❌ No IP to search for! Enter IP first.");
            return;
        }

        uintptr_t baseAddr = getLibraryAddress("libil2cpp.so");
        if (baseAddr == 0) {
            write_log(g_scanLog, "❌ libil2cpp.so not loaded!");
            return;
        }

        write_log(g_scanLog, "🔍 Searching for: \"" + searchStr + "\"");
        write_log(g_scanLog, "libil2cpp.so base: 0x" + std::to_string(baseAddr));
        write_log(g_scanLog, "----------------------------------------");

        int foundCount = 0;
        // محدوده 1 مگابایت (برای سرعت و جلوگیری از کرش)
        uintptr_t startAddr = baseAddr;
        uintptr_t endAddr = baseAddr + 0x100000; // 1MB
        
        write_log(g_scanLog, "Scanning range: 0x" + std::to_string(startAddr) + " - 0x" + std::to_string(endAddr));

        for (uintptr_t addr = startAddr; addr < endAddr; addr += 4) {
            char buffer[50] = {0};
            if (!safe_mem_read(addr, buffer, searchStr.length())) continue;
            
            if (strncmp(buffer, searchStr.c_str(), searchStr.length()) == 0) {
                foundCount++;
                write_log(g_scanLog, "✅ Found \"" + searchStr + "\" at 0x" + std::to_string(addr));
                
                // پیدا کردن pointer به این آدرس
                for (uintptr_t ptrAddr = startAddr; ptrAddr < endAddr; ptrAddr += 4) {
                    uintptr_t ptr = 0;
                    if (!safe_mem_read(ptrAddr, &ptr, sizeof(uintptr_t))) continue;
                    if (ptr == addr) {
                        write_log(g_scanLog, "   → Pointer at 0x" + std::to_string(ptrAddr) + " (offset: 0x" + std::to_string(ptrAddr - baseAddr) + ")");
                    }
                }
            }
        }

        write_log(g_scanLog, "Found " + std::to_string(foundCount) + " matches.");
        write_log(g_scanLog, "========== SCAN END ==========");
        write_log(g_debugLog, "✅ IP string scan completed. Check scan_result.txt");

    } catch (...) {
        write_log(g_crashLog, "⚠️ Exception in scan_for_ip_string!");
    }
}

// ======================== تزریق IP ========================
static void inject_ip_to_game() {
    try {
        if (g_targetIP.empty()) {
            g_targetIP = load_ip();
            if (g_targetIP.empty()) {
                write_log(g_ipResultLog, "❌ No IP found!");
                return;
            }
        }

        // فقط لاگ کن
        write_log(g_ipResultLog, "🔄 IP ready: " + g_targetIP);

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
        OBFUSCATE("Button_Show IP"),               // featNum: 1
        OBFUSCATE("Button_🔍 Scan IP String"),     // featNum: 2
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
            {
                std::string savedIP = load_ip();
                if (!savedIP.empty()) {
                    write_log(g_ipResultLog, "📌 Show IP: " + savedIP);
                }
            }
            break;

        case 2:
            write_log(g_ipResultLog, "🔍 Scan IP String button pressed");
            scan_for_ip_string();
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
        write_debug("📌 Loaded IP from file: " + g_targetIP);
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