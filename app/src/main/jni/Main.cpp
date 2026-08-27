#include <string>
#include <jni.h>
#include <unistd.h>
#include <fstream>
#include <thread>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <cstring>
#include <signal.h>
#include <sys/stat.h>
#include <unwind.h>
#include <dlfcn.h>
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
#define OFFSET_IP_INPUT2 0xD8
#define OFFSET_IP_INPUT3 0x80

// ======================== مسیرها (همه توی lac) ========================
static std::string g_basePath = "/storage/emulated/0/Download/lac/";
static std::string g_debugLog = g_basePath + "mod_debug.txt";
static std::string g_lastIPFile = g_basePath + "last_ip.txt";
static std::string g_captureLog = g_basePath + "capture_log.txt";
static std::string g_ipResultLog = g_basePath + "ip_result.txt";
static std::string g_crashLog = g_basePath + "crash_log.txt";
static std::string g_offsetLog = g_basePath + "offset_data.txt";
static std::string g_memoryDump = g_basePath + "memory_dump.txt";

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
    f << "\n📋 Registers:\n";
    f << "  pc: 0x" << std::hex << sc->pc << "\n";
    f << "  lr: 0x" << std::hex << sc->regs[30] << "\n";
    f << "  sp: 0x" << std::hex << sc->sp << "\n";
    for (int i = 0; i < 29; i++) {
        f << "  x" << i << ": 0x" << std::hex << sc->regs[i] << "\n";
    }
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
}

// ======================== کپچر کامل ========================
static void capture_everything() {
    try {
        write_log(g_captureLog, "========== CAPTURE START ==========");
        write_log(g_captureLog, "Time: " + get_time());

        uintptr_t baseAddr = getLibraryAddress("libil2cpp.so");
        write_log(g_captureLog, "libil2cpp.so base: 0x" + std::to_string(baseAddr));

        if (baseAddr == 0) {
            write_log(g_captureLog, "❌ libil2cpp.so not loaded!");
            return;
        }

        // آفست‌های IP
        uintptr_t offsets[] = {OFFSET_IP_INPUT, OFFSET_IP_INPUT2, OFFSET_IP_INPUT3};
        const char* offsetNames[] = {"GtaMenuControl.ipInput (0xC0)", "MenuControl.ipInput (0xD8)", "ServerListAccess.ipAddressInput (0x80)"};

        for (int i = 0; i < 3; i++) {
            uintptr_t addr = baseAddr + offsets[i];
            uintptr_t ptr = 0;
            memcpy(&ptr, (void*)addr, sizeof(uintptr_t));

            write_log(g_captureLog, "Offset " + std::string(offsetNames[i]) + " at 0x" + std::to_string(addr) + " → pointer: 0x" + std::to_string(ptr));

            if (ptr != 0) {
                char buffer[256] = {0};
                memcpy(buffer, (void*)ptr, 50);
                write_log(g_captureLog, "   Value: \"" + std::string(buffer) + "\"");
            }
        }

        write_log(g_captureLog, "========== CAPTURE END ==========");
    } catch (...) {
        write_log(g_crashLog, "⚠️ Exception in capture_everything!");
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

        uintptr_t baseAddr = getLibraryAddress("libil2cpp.so");
        if (baseAddr == 0) {
            write_log(g_ipResultLog, "❌ libil2cpp.so not loaded!");
            return;
        }

        uintptr_t addr = baseAddr + OFFSET_IP_INPUT;
        uintptr_t ptr = 0;
        memcpy(&ptr, (void*)addr, sizeof(uintptr_t));

        write_log(g_ipResultLog, "🔄 Injecting IP: " + g_targetIP);
        write_log(g_ipResultLog, "   pointer: 0x" + std::to_string(ptr));

        if (ptr != 0) {
            KittyMemory::memWrite((void*)ptr, g_targetIP.c_str(), g_targetIP.length());
            write_log(g_ipResultLog, "✅ IP written at 0x" + std::to_string(ptr));
        } else {
            write_log(g_ipResultLog, "❌ pointer is null!");
        }
    } catch (...) {
        write_log(g_ipResultLog, "⚠️ Exception in inject_ip_to_game!");
    }
}

// ======================== ذخیره IP ========================
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

// ======================== منو ========================
jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    jobjectArray ret;
    const char *features[] = {
        OBFUSCATE("Category_📡 Tools"),
        OBFUSCATE("InputText_Enter IP"),           // featNum: 0
        OBFUSCATE("Button_Inject IP"),             // featNum: 1
        OBFUSCATE("Button_Show IP"),               // featNum: 2
        OBFUSCATE("Button_📡 Capture All"),        // featNum: 3
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

        case 3:
            write_log(g_captureLog, "🔘 Capture button pressed");
            capture_everything();
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