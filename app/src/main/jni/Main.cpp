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

// ======================== آفست‌ها ========================
#define OFFSET_GTA_MENU_START   0xF571A4      // GtaMenuControl.Start()
#define OFFSET_NETWORK_ADDR     0x50          // Mirror.NetworkManager.networkAddress

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
    write_report("✅ Crash handler installed");
}

// ======================== هوک روی GtaMenuControl.Start (اختیاری) ========================
void (*orig_GtaMenuStart)(void *instance);
void hook_GtaMenuStart(void *instance) {
    write_report("========== GtaMenuControl.Start() HOOKED ==========");
    write_report("Time: " + get_time());
    if (instance != nullptr) {
        write_report("✅ GtaMenuControl instance: 0x" + std::to_string((uintptr_t)instance));
    } else {
        write_report("❌ instance is null!");
    }
    write_report("====================================================\n");
    if (orig_GtaMenuStart) {
        orig_GtaMenuStart(instance);
    }
}

// ======================== اتصال به سرور ========================
static void connect_to_server(JNIEnv* env, jobject obj) {
    write_report("\n========== CONNECT ATTEMPT ==========");
    write_report("Time: " + get_time());
    write_report("Target IP: " + g_targetIP);

    if (env == nullptr) {
        write_report("❌ JNIEnv is null!");
        return;
    }

    try {
        // بارگذاری IP
        if (g_targetIP.empty()) {
            g_targetIP = load_ip();
            if (g_targetIP.empty()) {
                write_report("📌 No saved IP, using default: " DEFAULT_IP ":" DEFAULT_PORT);
                g_targetIP = DEFAULT_IP ":" DEFAULT_PORT;
                save_ip(g_targetIP);
            } else {
                write_report("📌 Loaded IP from file: " + g_targetIP);
            }
        }

        // ====== پیدا کردن Mirror.NetworkManager ======
        write_report("Step 1: Finding Mirror.NetworkManager...");
        jclass nmClass = env->FindClass("Mirror.NetworkManager");
        if (nmClass == nullptr) {
            env->ExceptionClear();
            write_report("⚠️ Mirror.NetworkManager not found, trying NetworkManager...");
            nmClass = env->FindClass("NetworkManager");
            if (nmClass == nullptr) {
                env->ExceptionClear();
                write_report("❌ NetworkManager class not found!");
                write_log(g_ipResultLog, "❌ NetworkManager class not found!");
                return;
            }
        }
        write_report("✅ NetworkManager class found");

        // ====== پیدا کردن UnityEngine.Object ======
        jclass objClass = env->FindClass("UnityEngine.Object");
        if (objClass == nullptr) {
            env->ExceptionClear();
            write_report("❌ UnityEngine.Object class not found!");
            write_log(g_ipResultLog, "❌ UnityEngine.Object class not found!");
            return;
        }

        jmethodID findObj = env->GetStaticMethodID(objClass, "FindObjectOfType", "(Ljava/lang/Class;)Ljava/lang/Object;");
        if (findObj == nullptr) {
            env->ExceptionClear();
            write_report("❌ FindObjectOfType method not found!");
            write_log(g_ipResultLog, "❌ FindObjectOfType method not found!");
            return;
        }

        // ====== پیدا کردن instance ======
        jobject nm = env->CallStaticObjectMethod(objClass, findObj, nmClass);
        if (nm == nullptr) {
            write_report("❌ NetworkManager instance not found!");
            write_log(g_ipResultLog, "❌ NetworkManager instance not found!");
            return;
        }
        write_report("✅ NetworkManager instance found: 0x" + std::to_string((uintptr_t)nm));

        // ====== تغییر networkAddress (آفست 0x50) ======
        write_report("Step 2: Setting networkAddress...");
        jfieldID addrField = env->GetFieldID(nmClass, "networkAddress", "Ljava/lang/String;");
        if (addrField == nullptr) {
            env->ExceptionClear();
            write_report("❌ networkAddress field not found!");
            write_log(g_ipResultLog, "❌ networkAddress field not found!");
            return;
        }

        jstring jip = env->NewStringUTF(g_targetIP.c_str());
        if (jip == nullptr) {
            write_report("❌ Failed to create jstring!");
            write_log(g_ipResultLog, "❌ Failed to create jstring!");
            return;
        }

        env->SetObjectField(nm, addrField, jip);
        env->DeleteLocalRef(jip);
        write_report("✅ networkAddress set to: " + g_targetIP);
        write_log(g_ipResultLog, "✅ networkAddress set to: " + g_targetIP);

        // ====== صدا زدن StartClient ======
        write_report("Step 3: Calling StartClient...");
        jmethodID startClient = env->GetMethodID(nmClass, "StartClient", "()V");
        if (startClient == nullptr) {
            env->ExceptionClear();
            write_report("⚠️ StartClient not found, trying StartHost...");
            startClient = env->GetMethodID(nmClass, "StartHost", "()V");
            if (startClient == nullptr) {
                env->ExceptionClear();
                write_report("❌ StartClient/StartHost method not found!");
                write_log(g_ipResultLog, "❌ StartClient/StartHost method not found!");
                return;
            }
            write_report("🔄 Calling StartHost");
            write_log(g_ipResultLog, "🔄 Calling StartHost");
        } else {
            write_report("🔄 Calling StartClient");
            write_log(g_ipResultLog, "🔄 Calling StartClient");
        }

        env->CallVoidMethod(nm, startClient);

        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            write_report("⚠️ Exception during StartClient/StartHost!");
            write_log(g_ipResultLog, "⚠️ Exception during StartClient/StartHost!");
        } else {
            write_report("✅ StartClient/StartHost called successfully");
            write_log(g_ipResultLog, "✅ StartClient/StartHost called successfully");
        }

        write_report("========== CONNECT COMPLETE ==========\n");

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
        OBFUSCATE("Category_🌐 Network"),
        OBFUSCATE("InputText_IP"),                 // featNum: 0
        OBFUSCATE("Button_Connect"),               // featNum: 1
        OBFUSCATE("Button_Show IP"),               // featNum: 2
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
            write_report("🔘 Connect button pressed");
            write_log(g_ipResultLog, "🔘 Connect button pressed");
            connect_to_server(env, obj);
            break;

        case 2:
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

    // ====== هوک روی GtaMenuControl.Start (اختیاری) ======
#if defined(__aarch64__)
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
#endif

    // ====== بارگذاری IP ======
    g_targetIP = load_ip();
    if (g_targetIP.empty()) {
        write_report("📌 No saved IP found, using default: " DEFAULT_IP ":" DEFAULT_PORT);
        g_targetIP = DEFAULT_IP ":" DEFAULT_PORT;
        save_ip(g_targetIP);
    } else {
        write_report("📌 Loaded IP from file: " + g_targetIP);
    }
    write_debug("📌 Current IP: " + g_targetIP);

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