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

#define targetLibName OBFUSCATE("libil2cpp.so")

// ======================== آفست‌ها ========================
#define OFFSET_IP_INPUT  0xC0

// ======================== مسیرها ========================
static std::string g_basePath = "/storage/emulated/0/Download/lac/";
static std::string g_debugLog = g_basePath + "mod_debug.txt";
static std::string g_lastIPFile = g_basePath + "last_ip.txt";
static std::string g_captureLog = g_basePath + "capture_log.txt";
static std::string g_crashLog = g_basePath + "crash_log.txt";
static std::string g_inputfieldDump = g_basePath + "inputfield_dump.txt";

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

// ======================== دامپ کامل InputField (فقط خوندن) ========================
static void dump_inputfield() {
    try {
        write_log(g_inputfieldDump, "========== INPUTFIELD DUMP ==========");
        write_log(g_inputfieldDump, "Time: " + get_time());

        uintptr_t baseAddr = getLibraryAddress("libil2cpp.so");
        if (baseAddr == 0) {
            write_log(g_inputfieldDump, "❌ libil2cpp.so not loaded!");
            return;
        }

        // گرفتن پوینتر ipInput
        uintptr_t addr = baseAddr + OFFSET_IP_INPUT;
        uintptr_t inputFieldPtr = 0;
        memcpy(&inputFieldPtr, (void*)addr, sizeof(uintptr_t));

        write_log(g_inputfieldDump, "InputField pointer: 0x" + std::to_string(inputFieldPtr));

        if (inputFieldPtr == 0) {
            write_log(g_inputfieldDump, "❌ InputField pointer is null!");
            return;
        }

        // ====== دامپ 0x200 بایت از InputField ======
        write_log(g_inputfieldDump, "\n📦 Memory dump (0x0 - 0x200):");
        write_log(g_inputfieldDump, "----------------------------------------");

        for (int offset = 0; offset < 0x200; offset += 16) {
            unsigned char data[16];
            memcpy(data, (void*)(inputFieldPtr + offset), 16);

            // ساخت خط با stringstream
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

            // فقط خطوطی که داده غیرصفر دارند ذخیره کن
            bool hasData = false;
            for (int i = 0; i < 16; i++) {
                if (data[i] != 0) { hasData = true; break; }
            }

            if (hasData) {
                write_log(g_inputfieldDump, line.str());
            }
        }

        // ====== پیدا کردن pointer به string ======
        write_log(g_inputfieldDump, "\n🔍 Looking for string pointers:");
        write_log(g_inputfieldDump, "----------------------------------------");

        for (int offset = 0; offset < 0x200; offset += 8) {
            uintptr_t ptr = 0;
            memcpy(&ptr, (void*)(inputFieldPtr + offset), sizeof(uintptr_t));

            if (ptr != 0) {
                // چک کردن اینکه آیا این pointer به یه رشته معتبر اشاره میکنه
                char buffer[50] = {0};
                memcpy(buffer, (void*)ptr, 49);

                bool isString = true;
                int strLen = 0;
                for (int i = 0; i < 49 && buffer[i] != 0; i++) {
                    if (buffer[i] < 32 || buffer[i] > 126) {
                        isString = false;
                        break;
                    }
                    strLen++;
                }

                if (isString && strLen > 1) {
                    std::ostringstream line;
                    line << "+0x" << std::setw(3) << std::setfill('0') << std::hex << offset << ": 0x" << std::to_string(ptr) << " → \"" << std::string(buffer) << "\"";
                    write_log(g_inputfieldDump, line.str());
                }
            }
        }

        write_log(g_inputfieldDump, "========== DUMP END ==========");
        write_log(g_debugLog, "✅ InputField dump saved to inputfield_dump.txt");

    } catch (...) {
        write_log(g_crashLog, "⚠️ Exception in dump_inputfield!");
    }
}

// ======================== منو ========================
jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    jobjectArray ret;
    const char *features[] = {
        OBFUSCATE("Category_📡 Tools"),
        OBFUSCATE("InputText_Enter IP"),           // featNum: 0
        OBFUSCATE("Button_Show IP"),               // featNum: 1
        OBFUSCATE("Button_📡 Dump InputField"),    // featNum: 2
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
                write_log(g_captureLog, "📝 IP entered: " + g_targetIP);
            }
            break;

        case 1:
            {
                std::string savedIP = load_ip();
                if (!savedIP.empty()) {
                    write_log(g_captureLog, "📌 Show IP: " + savedIP);
                }
            }
            break;

        case 2:
            write_log(g_captureLog, "🔘 Dump InputField button pressed");
            dump_inputfield();
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