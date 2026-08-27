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

// ======================== آفست‌ها (همگی از دامپ دقیق) ========================
#define OFFSET_GTA_START  0xF571A4      // GtaMenuControl.Start()
#define OFFSET_IP_INPUT   0xC0          // GtaMenuControl.ipInput
#define OFFSET_SET_TEXT   0x205DA44     // InputField.set_text (آفست درست از دامپ)

// ======================== مسیرهای ذخیره‌سازی ========================
static std::string g_basePath = "/storage/emulated/0/Download/lac/";
static std::string g_debugLog = g_basePath + "mod_debug.txt";
static std::string g_lastIPFile = g_basePath + "last_ip.txt";
static std::string g_ipResultLog = g_basePath + "ip_result.txt";
static std::string g_crashLog = g_basePath + "crash_log.txt";

// ======================== متغیرهای سراسری ========================
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

// ======================== تزریق IP با set_text (JNI) ========================
static void inject_ip_to_game(JNIEnv* env, jobject obj) {
    // چک اولیه
    if (env == nullptr || obj == nullptr) {
        write_log(g_ipResultLog, "❌ env or obj is null!");
        return;
    }

    if (!g_gtaMenuReady || g_gtaMenuInstance == nullptr) {
        write_debug("❌ GtaMenuControl not ready yet!");
        write_log(g_ipResultLog, "❌ GtaMenuControl not ready yet!");
        return;
    }

    try {
        // بارگذاری IP
        if (g_targetIP.empty()) {
            g_targetIP = load_ip();
            if (g_targetIP.empty()) {
                write_debug("❌ No IP found!");
                write_log(g_ipResultLog, "❌ No IP found!");
                return;
            }
        }

        // گرفتن base address
        uintptr_t baseAddr = getLibraryAddress("libil2cpp.so");
        if (baseAddr == 0) {
            write_debug("❌ libil2cpp.so not loaded!");
            write_log(g_ipResultLog, "❌ libil2cpp.so not loaded!");
            return;
        }

        // گرفتن ipInput pointer
        uintptr_t gtaPtr = (uintptr_t)g_gtaMenuInstance;
        uintptr_t inputFieldPtr = 0;
        uintptr_t ipInputAddr = gtaPtr + OFFSET_IP_INPUT;
        
        if (!safe_mem_read(ipInputAddr, &inputFieldPtr, sizeof(uintptr_t))) {
            write_debug("❌ Cannot read ipInput!");
            write_log(g_ipResultLog, "❌ Cannot read ipInput!");
            return;
        }
        
        write_debug("✅ ipInput pointer: 0x" + std::to_string(inputFieldPtr));
        write_log(g_ipResultLog, "✅ ipInput pointer: 0x" + std::to_string(inputFieldPtr));
        
        if (inputFieldPtr == 0) {
            write_debug("❌ ipInput is null!");
            write_log(g_ipResultLog, "❌ ipInput is null!");
            return;
        }

        // گرفتن آدرس set_text با آفست درست
        void* setTextAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0x205DA44"));
        if (setTextAddr == nullptr) {
            write_debug("❌ set_text address not found!");
            write_log(g_ipResultLog, "❌ set_text address not found!");
            return;
        }

        write_debug("✅ set_text address: 0x" + std::to_string((uintptr_t)setTextAddr));
        write_log(g_ipResultLog, "✅ set_text address: 0x" + std::to_string((uintptr_t)setTextAddr));

        // تعریف تابع set_text
        typedef void (*SetTextFunc)(void* instance, void* monoString);
        SetTextFunc setText = (SetTextFunc)setTextAddr;

        // تبدیل IP به jstring (monoString در Unity)
        jstring jip = env->NewStringUTF(g_targetIP.c_str());
        if (jip == nullptr) {
            write_debug("❌ Failed to create jstring!");
            write_log(g_ipResultLog, "❌ Failed to create jstring!");
            return;
        }

        // صدا زدن set_text
        setText((void*)inputFieldPtr, (void*)jip);
        env->DeleteLocalRef(jip);

        // بررسی خطاهای JNI
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            write_debug("⚠️ Exception occurred during set_text call!");
            write_log(g_ipResultLog, "⚠️ Exception during set_text!");
        } else {
            write_debug("✅ IP injected via set_text: " + g_targetIP);
            write_log(g_ipResultLog, "✅ IP injected via set_text: " + g_targetIP);
        }

    } catch (const std::exception& e) {
        write_debug("⚠️ Exception: " + std::string(e.what()));
        write_log(g_crashLog, "⚠️ Exception: " + std::string(e.what()));
    } catch (...) {
        write_debug("⚠️ Unknown exception!");
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
            inject_ip_to_game(env, obj);
            break;

        case 2: // Button_Show IP
            {
                std::string savedIP = load_ip();
                if (!savedIP.empty()) {
                    write_debug("📌 Show IP: " + savedIP);
                    write_log(g_ipResultLog, "📌 Show IP: " + savedIP);
                } else {
                    write_debug("❌ No saved IP!");
                    write_log(g_ipResultLog, "❌ No saved IP!");
                }
            }
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
    // هوک روی GtaMenuControl.Start
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