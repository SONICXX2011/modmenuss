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

// ======================== آفست‌ها (همگی از دامپ و توسعه‌دهنده) ========================
#define OFFSET_GTA_START  0xF571A4      // GtaMenuControl.Start()
#define OFFSET_IP_INPUT   0xC0          // GtaMenuControl.ipInput
#define OFFSET_SET_TEXT_DEV 0x1FF6794   // InputField.set_text (توسعه‌دهنده)
#define OFFSET_SET_TEXT_DUMP 0x205DA44  // InputField.set_text (دامپ)
#define OFFSET_M_TEXT     0x328         // InputField.m_Text (از دامپ واقعی)

// ======================== مسیرهای ذخیره‌سازی ========================
static std::string g_basePath = "/storage/emulated/0/Download/lac/";
static std::string g_debugLog = g_basePath + "mod_debug.txt";
static std::string g_lastIPFile = g_basePath + "last_ip.txt";
static std::string g_ipResultLog = g_basePath + "ip_result.txt";
static std::string g_crashLog = g_basePath + "crash_log.txt";
static std::string g_fullReport = g_basePath + "full_report.txt";

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

static void write_report(const std::string& msg) {
    write_log(g_fullReport, msg);
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

// ======================== کرش‌گیر جامع ========================
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

// ======================== تزریق IP (ترکیبی از همه روش‌ها) ========================
static void inject_ip_to_game(JNIEnv* env, jobject obj) {
    write_report("\n========== INJECTION ATTEMPT ==========");
    write_report("Time: " + get_time());
    write_report("Target IP: " + g_targetIP);
    
    // چک اولیه
    if (env == nullptr) {
        write_report("❌ env is null!");
        return;
    }
    if (obj == nullptr) {
        write_report("❌ obj is null!");
        return;
    }

    if (!g_gtaMenuReady || g_gtaMenuInstance == nullptr) {
        write_report("❌ GtaMenuControl not ready yet (instance not captured)");
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

        // ====== 1. گرفتن ipInput از instance ======
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

        // ====== 2. روش اول: set_text با آفست توسعه‌دهنده ======
        write_report("\n--- Method 1: set_text (Developer offset 0x1FF6794) ---");
        void* setTextDevAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0x1FF6794"));
        if (setTextDevAddr != nullptr) {
            write_report("✅ set_text address (dev): 0x" + std::to_string((uintptr_t)setTextDevAddr));
            typedef void (*SetTextFunc)(void* instance, void* monoString);
            SetTextFunc setText = (SetTextFunc)setTextDevAddr;
            
            jstring jip = env->NewStringUTF(g_targetIP.c_str());
            if (jip != nullptr) {
                write_report("🔄 Calling set_text (dev) with jstring");
                setText((void*)inputFieldPtr, (void*)jip);
                env->DeleteLocalRef(jip);
                
                if (env->ExceptionCheck()) {
                    env->ExceptionDescribe();
                    env->ExceptionClear();
                    write_report("⚠️ Exception occurred during set_text (dev) call");
                    write_log(g_ipResultLog, "⚠️ Exception during set_text (dev)!");
                } else {
                    write_report("✅ set_text (dev) call completed without exception");
                    write_log(g_ipResultLog, "✅ IP injected via set_text (dev): " + g_targetIP);
                    // اگر این روش کار کرد، برمیگردیم
                    write_report("========== INJECTION COMPLETE (Method 1) ==========\n");
                    return;
                }
            } else {
                write_report("❌ Failed to create jstring for Method 1");
            }
        } else {
            write_report("❌ set_text (dev) address not found!");
        }

        // ====== 3. روش دوم: set_text با آفست دامپ ======
        write_report("\n--- Method 2: set_text (Dump offset 0x205DA44) ---");
        void* setTextDumpAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0x205DA44"));
        if (setTextDumpAddr != nullptr) {
            write_report("✅ set_text address (dump): 0x" + std::to_string((uintptr_t)setTextDumpAddr));
            typedef void (*SetTextFunc)(void* instance, void* monoString);
            SetTextFunc setText = (SetTextFunc)setTextDumpAddr;
            
            jstring jip = env->NewStringUTF(g_targetIP.c_str());
            if (jip != nullptr) {
                write_report("🔄 Calling set_text (dump) with jstring");
                setText((void*)inputFieldPtr, (void*)jip);
                env->DeleteLocalRef(jip);
                
                if (env->ExceptionCheck()) {
                    env->ExceptionDescribe();
                    env->ExceptionClear();
                    write_report("⚠️ Exception occurred during set_text (dump) call");
                    write_log(g_ipResultLog, "⚠️ Exception during set_text (dump)!");
                } else {
                    write_report("✅ set_text (dump) call completed without exception");
                    write_log(g_ipResultLog, "✅ IP injected via set_text (dump): " + g_targetIP);
                    write_report("========== INJECTION COMPLETE (Method 2) ==========\n");
                    return;
                }
            } else {
                write_report("❌ Failed to create jstring for Method 2");
            }
        } else {
            write_report("❌ set_text (dump) address not found!");
        }

        // ====== 4. روش سوم: m_Text (آفست 0x328) ======
        write_report("\n--- Method 3: m_Text (offset 0x328) ---");
        uintptr_t mTextAddr = inputFieldPtr + OFFSET_M_TEXT;
        uintptr_t mTextPtr = 0;
        
        if (safe_mem_read(mTextAddr, &mTextPtr, sizeof(uintptr_t)) && mTextPtr != 0) {
            write_report("✅ m_Text pointer: 0x" + std::to_string(mTextPtr));
            write_log(g_ipResultLog, "✅ m_Text pointer: 0x" + std::to_string(mTextPtr));
            
            // چک کردن اینکه حافظه قابل نوشتن است
            auto map = KittyMemory::getAddressMap((void*)mTextPtr);
            if (!map.writeable) {
                write_report("⚠️ m_Text memory is not writable, trying to change protection");
                if (KittyMemory::memProtect((void*)mTextPtr, g_targetIP.length() + 1, PROT_READ | PROT_WRITE) == 0) {
                    write_report("✅ Memory protection changed to RW");
                } else {
                    write_report("❌ Failed to change memory protection");
                    // ادامه نمی‌دیم چون نمی‌تونیم بنویسیم
                    write_report("========== INJECTION FAILED ==========\n");
                    return;
                }
            }
            
            // نوشتن IP
            if (KittyMemory::memWrite((void*)mTextPtr, g_targetIP.c_str(), g_targetIP.length() + 1)) {
                write_report("✅ IP successfully written to m_Text at 0x" + std::to_string(mTextPtr));
                write_log(g_ipResultLog, "✅ IP written via m_Text: " + g_targetIP);
                write_report("========== INJECTION COMPLETE (Method 3) ==========\n");
                return;
            } else {
                write_report("❌ Failed to write to m_Text");
                write_log(g_ipResultLog, "❌ Failed to write to m_Text");
            }
        } else {
            write_report("❌ Cannot read m_Text pointer at offset 0x" + std::to_string(OFFSET_M_TEXT));
            write_log(g_ipResultLog, "❌ Cannot read m_Text pointer");
        }

        // ====== 5. روش چهارم: text (آفست 0x180) ======
        write_report("\n--- Method 4: text (offset 0x180) ---");
        uintptr_t textAddr = inputFieldPtr + 0x180;
        uintptr_t textPtr = 0;
        
        if (safe_mem_read(textAddr, &textPtr, sizeof(uintptr_t)) && textPtr != 0) {
            write_report("✅ text pointer: 0x" + std::to_string(textPtr));
            auto map = KittyMemory::getAddressMap((void*)textPtr);
            if (!map.writeable) {
                if (KittyMemory::memProtect((void*)textPtr, g_targetIP.length() + 1, PROT_READ | PROT_WRITE) == 0) {
                    write_report("✅ Memory protection changed to RW");
                } else {
                    write_report("❌ Failed to change memory protection");
                    write_report("========== INJECTION FAILED ==========\n");
                    return;
                }
            }
            
            if (KittyMemory::memWrite((void*)textPtr, g_targetIP.c_str(), g_targetIP.length() + 1)) {
                write_report("✅ IP successfully written to text at 0x" + std::to_string(textPtr));
                write_log(g_ipResultLog, "✅ IP written via text: " + g_targetIP);
                write_report("========== INJECTION COMPLETE (Method 4) ==========\n");
                return;
            } else {
                write_report("❌ Failed to write to text");
                write_log(g_ipResultLog, "❌ Failed to write to text");
            }
        } else {
            write_report("❌ Cannot read text pointer at offset 0x180");
            write_log(g_ipResultLog, "❌ Cannot read text pointer");
        }

        // ====== اگر هیچ روشی کار نکرد ======
        write_report("\n❌ All injection methods failed!");
        write_log(g_ipResultLog, "❌ All injection methods failed!");
        write_report("========== INJECTION FAILED ==========\n");

    } catch (const std::exception& e) {
        write_report("❌ Exception: " + std::string(e.what()));
        write_log(g_crashLog, "⚠️ Exception in inject_ip_to_game: " + std::string(e.what()));
    } catch (...) {
        write_report("❌ Unknown exception caught");
        write_log(g_crashLog, "⚠️ Unknown exception in inject_ip_to_game!");
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
                write_report("📝 IP entered via menu: " + g_targetIP);
                write_log(g_ipResultLog, "📝 IP entered: " + g_targetIP);
            }
            break;

        case 1: // Button_Inject IP
            write_report("🔘 Inject button pressed by user");
            write_log(g_ipResultLog, "🔘 Inject button pressed");
            inject_ip_to_game(env, obj);
            break;

        case 2: // Button_Show IP
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
    // هوک روی GtaMenuControl.Start
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
        write_report("❌ GtaMenuControl.Start() address not found at offset 0xF571A4");
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

    std::ofstream report(g_fullReport);
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