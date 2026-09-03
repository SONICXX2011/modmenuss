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
#include "KittyMemory/KittyMemory.hpp"

#define targetLibName OBFUSCATE("libil2cpp.so")

// ======================== آفست‌های تأیید شده (همه از دامپ) ========================
#define OFFSET_GTA_GET_INSTANCE         0xF570C4
#define OFFSET_GTA_START                0xF571A4
#define OFFSET_MENU_BUTTONS             0x30          // GtaMenuControl.menuButtons
#define OFFSET_BUTTON_ONCLICK           0x100         // Button.m_OnClick
#define OFFSET_BUTTON_INTERACTABLE      0xD8          // Selectable.m_Interactable
#define OFFSET_OBJECT_GET_NAME          0x1E7CE6C     // UnityEngine.Object.get_name()
#define OFFSET_IL2CPP_STRING_NEW        0xE40BF0      // il2cpp_string_new()
#define OFFSET_GAMEOBJECT_NAME          0x20          // GameObject.m_Name (String)
#define OFFSET_ONPOINTERCLICK           0x1F04A60     // UnityEngine.UI.Button.OnPointerClick

#define MAX_BUTTONS 30
#define MAX_WAIT 30

// ======================== مسیرهای لاگ ========================
static std::string g_basePath = "/storage/emulated/0/Download/lac/";
static std::string g_buttonsLog = g_basePath + "buttons_list.txt";
static std::string g_clickLog = g_basePath + "click_log.txt";
static std::string g_crashLog = g_basePath + "crash_log.txt";
static std::string g_debugLog = g_basePath + "mod_debug.txt";

static bool g_crashHandlerInstalled = false;
static bool g_gameReady = false;
static bool g_hooksInstalled = false;
static void* g_gtaInstance = nullptr;

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

// ======================== توابع امنیت حافظه ========================
static bool is_valid_address(void* addr) {
    if (!addr) return false;
    uintptr_t ptr = (uintptr_t)addr;
    if (ptr < 0x1000) return false;
    if (ptr > 0x7FFFFFFFFFFFFF) return false;
    auto map = KittyMemory::getAddressMap(addr);
    return map.readable;
}

// ======================== توابع IL2CPP امن ========================
static void* create_mono_string(const char* str) {
    if (!str) return nullptr;
    typedef void* (*il2cpp_string_new_t)(const char*);
    il2cpp_string_new_t il2cpp_string_new = 
        (il2cpp_string_new_t)getAbsoluteAddress("libil2cpp.so", OBFUSCATE("0xE40BF0"));
    if (!il2cpp_string_new) return nullptr;
    return il2cpp_string_new(str);
}

static std::string mono_to_string_safe(void* monoStr) {
    if (!monoStr || !is_valid_address(monoStr)) return "";
    typedef int32_t (*len_t)(void*);
    typedef uint16_t* (*chars_t)(void*);
    len_t get_len = (len_t)getAbsoluteAddress("libil2cpp.so", "il2cpp_string_length");
    chars_t get_chars = (chars_t)getAbsoluteAddress("libil2cpp.so", "il2cpp_string_chars");
    if (!get_len || !get_chars) return "";
    int len = get_len(monoStr);
    if (len <= 0 || len > 65536) return "";
    uint16_t* chars = get_chars(monoStr);
    if (!chars || !is_valid_address(chars)) return "";
    std::string result;
    result.reserve(len);
    for (int i = 0; i < len; i++) {
        result += (char)(chars[i] & 0xFF);
    }
    return result;
}

static void* get_gta_instance_safe() {
    if (g_gtaInstance && is_valid_address(g_gtaInstance)) {
        return g_gtaInstance;
    }
    typedef void* (*get_instance_t)();
    get_instance_t get_Instance = (get_instance_t)getAbsoluteAddress("libil2cpp.so", OBFUSCATE("0xF570C4"));
    if (!get_Instance) return nullptr;
    void* instance = get_Instance();
    if (instance && is_valid_address(instance)) {
        g_gtaInstance = instance;
        return instance;
    }
    return nullptr;
}

static std::string get_button_name_safe(void* btn) {
    if (!btn || !is_valid_address(btn)) return "";
    void* namePtr = *(void**)((uintptr_t)btn + OFFSET_GAMEOBJECT_NAME);
    if (namePtr && is_valid_address(namePtr)) {
        std::string name = mono_to_string_safe(namePtr);
        if (!name.empty()) return name;
    }
    typedef void* (*get_name_t)(void*);
    get_name_t get_name = (get_name_t)getAbsoluteAddress("libil2cpp.so", OBFUSCATE("0x1E7CE6C"));
    if (get_name) {
        void* monoStr = get_name(btn);
        if (monoStr && is_valid_address(monoStr)) {
            return mono_to_string_safe(monoStr);
        }
    }
    return "";
}

// ======================== پیدا کردن همه Button‌ها با JNI (بدون آفست) ========================
static void FindAllButtonsWithJNI() {
    write_log(g_buttonsLog, "\n========== FIND ALL BUTTONS WITH JNI ==========");
    write_log(g_buttonsLog, "Time: " + get_time());
    
    JNIEnv* env = nullptr;
    JavaVM* vm = nullptr;
    jsize count;
    
    // گرفتن JavaVM
    if (JNI_GetCreatedJavaVMs(&vm, 1, &count) != JNI_OK || count == 0) {
        write_log(g_buttonsLog, "❌ Failed to get JavaVM");
        write_log(g_buttonsLog, "========== DONE ==========\n");
        return;
    }
    vm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (!env) {
        write_log(g_buttonsLog, "❌ Failed to get JNIEnv");
        write_log(g_buttonsLog, "========== DONE ==========\n");
        return;
    }
    
    try {
        // 1. گرفتن کلاس UnityEngine.UI.Button
        jclass buttonClass = env->FindClass("UnityEngine.UI.Button");
        if (!buttonClass) {
            env->ExceptionClear();
            write_log(g_buttonsLog, "❌ UnityEngine.UI.Button class not found!");
            write_log(g_buttonsLog, "========== DONE ==========\n");
            return;
        }
        write_log(g_buttonsLog, "✅ UnityEngine.UI.Button class found");
        
        // 2. گرفتن کلاس UnityEngine.Resources
        jclass resourcesClass = env->FindClass("UnityEngine.Resources");
        if (!resourcesClass) {
            env->ExceptionClear();
            write_log(g_buttonsLog, "❌ UnityEngine.Resources class not found!");
            write_log(g_buttonsLog, "========== DONE ==========\n");
            return;
        }
        write_log(g_buttonsLog, "✅ UnityEngine.Resources class found");
        
        // 3. گرفتن متد FindObjectsOfTypeAll
        jmethodID findObjectsMethod = env->GetStaticMethodID(
            resourcesClass,
            "FindObjectsOfTypeAll",
            "(Ljava/lang/Class;)[Ljava/lang/Object;"
        );
        if (!findObjectsMethod) {
            env->ExceptionClear();
            write_log(g_buttonsLog, "❌ FindObjectsOfTypeAll method not found!");
            write_log(g_buttonsLog, "========== DONE ==========\n");
            return;
        }
        write_log(g_buttonsLog, "✅ FindObjectsOfTypeAll method found");
        
        // 4. صدا زدن FindObjectsOfTypeAll
        jobjectArray buttonsArray = (jobjectArray)env->CallStaticObjectMethod(
            resourcesClass,
            findObjectsMethod,
            buttonClass
        );
        if (!buttonsArray) {
            env->ExceptionClear();
            write_log(g_buttonsLog, "❌ FindObjectsOfTypeAll returned null!");
            write_log(g_buttonsLog, "========== DONE ==========\n");
            return;
        }
        write_log(g_buttonsLog, "✅ FindObjectsOfTypeAll called successfully");
        
        // 5. تعداد دکمه‌ها
        jsize buttonCount = env->GetArrayLength(buttonsArray);
        write_log(g_buttonsLog, "📊 Total buttons found: " + std::to_string(buttonCount));
        
        // 6. حلقه زدن روی دکمه‌ها
        int found = 0;
        for (int i = 0; i < buttonCount && i < 100; i++) {
            jobject btnObj = env->GetObjectArrayElement(buttonsArray, i);
            if (!btnObj) continue;
            
            // گرفتن اسم دکمه با JNI
            jclass btnClass = env->GetObjectClass(btnObj);
            jmethodID getNameMethod = env->GetMethodID(btnClass, "get_name", "()Ljava/lang/String;");
            if (!getNameMethod) {
                env->ExceptionClear();
                env->DeleteLocalRef(btnObj);
                continue;
            }
            
            jstring nameStr = (jstring)env->CallObjectMethod(btnObj, getNameMethod);
            if (!nameStr) {
                env->ExceptionClear();
                env->DeleteLocalRef(btnObj);
                continue;
            }
            
            const char* nameCStr = env->GetStringUTFChars(nameStr, nullptr);
            if (nameCStr) {
                std::string name(nameCStr);
                env->ReleaseStringUTFChars(nameStr, nameCStr);
                
                write_log(g_buttonsLog, "  ─────────────────────────────");
                write_log(g_buttonsLog, "  📌 Button[" + std::to_string(i) + "]");
                write_log(g_buttonsLog, "     Name: " + name);
                write_log(g_buttonsLog, "     JNI Object: 0x" + std::to_string((uintptr_t)btnObj));
                
                if (name == "CONNECT" || name == "Connect") {
                    write_log(g_buttonsLog, "     ⭐ THIS IS THE CONNECT BUTTON!");
                }
                found++;
            }
            
            env->DeleteLocalRef(nameStr);
            env->DeleteLocalRef(btnObj);
        }
        
        write_log(g_buttonsLog, "\n✅ Total buttons logged: " + std::to_string(found));
        env->DeleteLocalRef(buttonsArray);
        
    } catch (const std::exception& e) {
        write_log(g_buttonsLog, "❌ Exception: " + std::string(e.what()));
    } catch (...) {
        write_log(g_buttonsLog, "❌ Unknown exception!");
    }
    
    write_log(g_buttonsLog, "========== DONE ==========\n");
}

// ======================== کلیک روی CONNECT با JNI ========================
static void ClickConnectWithJNI() {
    write_log(g_clickLog, "\n========== CLICK CONNECT WITH JNI ==========");
    write_log(g_clickLog, "Time: " + get_time());
    
    JNIEnv* env = nullptr;
    JavaVM* vm = nullptr;
    jsize count;
    
    if (JNI_GetCreatedJavaVMs(&vm, 1, &count) != JNI_OK || count == 0) {
        write_log(g_clickLog, "❌ Failed to get JavaVM");
        write_log(g_clickLog, "========== DONE ==========\n");
        return;
    }
    vm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (!env) {
        write_log(g_clickLog, "❌ Failed to get JNIEnv");
        write_log(g_clickLog, "========== DONE ==========\n");
        return;
    }
    
    try {
        jclass buttonClass = env->FindClass("UnityEngine.UI.Button");
        if (!buttonClass) {
            env->ExceptionClear();
            write_log(g_clickLog, "❌ Button class not found!");
            write_log(g_clickLog, "========== DONE ==========\n");
            return;
        }
        
        jclass resourcesClass = env->FindClass("UnityEngine.Resources");
        jmethodID findObjectsMethod = env->GetStaticMethodID(
            resourcesClass, "FindObjectsOfTypeAll", "(Ljava/lang/Class;)[Ljava/lang/Object;"
        );
        if (!findObjectsMethod) {
            env->ExceptionClear();
            write_log(g_clickLog, "❌ FindObjectsOfTypeAll method not found!");
            write_log(g_clickLog, "========== DONE ==========\n");
            return;
        }
        
        jobjectArray buttonsArray = (jobjectArray)env->CallStaticObjectMethod(
            resourcesClass, findObjectsMethod, buttonClass
        );
        if (!buttonsArray) {
            env->ExceptionClear();
            write_log(g_clickLog, "❌ No buttons found!");
            write_log(g_clickLog, "========== DONE ==========\n");
            return;
        }
        
        jsize buttonCount = env->GetArrayLength(buttonsArray);
        bool foundConnect = false;
        
        for (int i = 0; i < buttonCount; i++) {
            jobject btnObj = env->GetObjectArrayElement(buttonsArray, i);
            if (!btnObj) continue;
            
            jclass btnClass = env->GetObjectClass(btnObj);
            jmethodID getNameMethod = env->GetMethodID(btnClass, "get_name", "()Ljava/lang/String;");
            if (!getNameMethod) {
                env->ExceptionClear();
                env->DeleteLocalRef(btnObj);
                continue;
            }
            
            jstring nameStr = (jstring)env->CallObjectMethod(btnObj, getNameMethod);
            if (!nameStr) {
                env->ExceptionClear();
                env->DeleteLocalRef(btnObj);
                continue;
            }
            
            const char* nameCStr = env->GetStringUTFChars(nameStr, nullptr);
            if (nameCStr) {
                std::string name(nameCStr);
                env->ReleaseStringUTFChars(nameStr, nameCStr);
                
                if (name == "CONNECT" || name == "Connect") {
                    write_log(g_clickLog, "✅ Found CONNECT button!");
                    write_log(g_clickLog, "   JNI Object: 0x" + std::to_string((uintptr_t)btnObj));
                    
                    // کلیک با JNI: صدا زدن onClick
                    jfieldID onClickField = env->GetFieldID(btnClass, "m_OnClick", "LUnityEngine/UI/Button$ButtonClickedEvent;");
                    if (onClickField) {
                        jobject onClickObj = env->GetObjectField(btnObj, onClickField);
                        if (onClickObj) {
                            jclass unityEventClass = env->FindClass("UnityEngine/Events/UnityEvent");
                            jmethodID invokeMethod = env->GetMethodID(unityEventClass, "Invoke", "()V");
                            if (invokeMethod) {
                                write_log(g_clickLog, "🔄 Calling UnityEvent.Invoke() via JNI...");
                                env->CallVoidMethod(onClickObj, invokeMethod);
                                write_log(g_clickLog, "✅ Invoke() called successfully!");
                            }
                            env->DeleteLocalRef(onClickObj);
                        }
                    }
                    
                    foundConnect = true;
                    env->DeleteLocalRef(btnObj);
                    break;
                }
            }
            env->DeleteLocalRef(btnObj);
        }
        
        if (!foundConnect) {
            write_log(g_clickLog, "❌ CONNECT button not found!");
        }
        
        env->DeleteLocalRef(buttonsArray);
        
    } catch (const std::exception& e) {
        write_log(g_clickLog, "❌ Exception: " + std::string(e.what()));
    } catch (...) {
        write_log(g_clickLog, "❌ Unknown exception!");
    }
    
    write_log(g_clickLog, "========== DONE ==========\n");
}

// ======================== هوک OnPointerClick ========================
void (*orig_OnPointerClick)(void* button, void* eventData);
void hook_OnPointerClick(void* button, void* eventData) {
    if (g_gameReady && button && is_valid_address(button)) {
        std::string name = get_button_name_safe(button);
        if (!name.empty()) {
            write_log(g_clickLog, "🖱️ Button clicked: " + name);
            write_log(g_clickLog, "   Address: 0x" + std::to_string((uintptr_t)button));
        }
    }
    if (orig_OnPointerClick) {
        orig_OnPointerClick(button, eventData);
    }
}

// ======================== هوک GtaMenuControl.Start ========================
void (*orig_GtaMenuStart)(void *instance);
void hook_GtaMenuStart(void *instance) {
    if (instance && is_valid_address(instance)) {
        g_gtaInstance = instance;
        if (!g_gameReady) {
            g_gameReady = true;
            write_log(g_buttonsLog, "✅ Game ready! (GtaMenuControl.Start hooked)");
            write_debug("✅ Game ready!");
        }
    }
    if (orig_GtaMenuStart) {
        orig_GtaMenuStart(instance);
    }
}

// ======================== نصب هوک‌ها ========================
static void install_hooks_with_delay() {
    write_log(g_buttonsLog, "⏳ Waiting for game to load...");
    
    int waitCount = 0;
    while (!isLibraryLoaded(targetLibName) && waitCount < MAX_WAIT) {
        sleep(1);
        waitCount++;
    }
    
    if (waitCount >= MAX_WAIT) {
        write_log(g_buttonsLog, "❌ Timeout!");
        return;
    }
    write_log(g_buttonsLog, "✅ libil2cpp.so loaded");
    
    waitCount = 0;
    while (waitCount < MAX_WAIT) {
        void* instance = get_gta_instance_safe();
        if (instance && is_valid_address(instance)) {
            g_gtaInstance = instance;
            g_gameReady = true;
            write_log(g_buttonsLog, "✅ GtaMenuControl instance ready!");
            break;
        }
        sleep(1);
        waitCount++;
    }
    
#if defined(__aarch64__)
    void* onPointerClickAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0x1F04A60"));
    if (onPointerClickAddr && is_valid_address(onPointerClickAddr)) {
        DobbyHook(onPointerClickAddr, (dobby_dummy_func_t)hook_OnPointerClick, (dobby_dummy_func_t*)&orig_OnPointerClick);
        write_log(g_buttonsLog, "✅ OnPointerClick hooked");
    }
    
    void* startAddr = getAbsoluteAddress(targetLibName, OBFUSCATE("0xF571A4"));
    if (startAddr && is_valid_address(startAddr)) {
        DobbyHook(startAddr, (dobby_dummy_func_t)hook_GtaMenuStart, (dobby_dummy_func_t*)&orig_GtaMenuStart);
        write_log(g_buttonsLog, "✅ GtaMenuControl.Start() hooked");
    }
#endif
    
    g_hooksInstalled = true;
    write_log(g_buttonsLog, "✅ Mod ready!");
}

// ======================== منو ========================
jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    const char *features[] = {
        "Category_🔍 Button Tools (JNI)",
        "Button_📋 Find All Buttons (JNI)",
        "Button_🔄 Click CONNECT (JNI)",
        "Button_📊 Show Status",
        "RichTextView_📁 Logs: /sdcard/Download/lac/",
        "RichTextView_📌 buttons_list.txt | click_log.txt",
    };
    int total = sizeof(features) / sizeof(features[0]);
    jobjectArray ret = (jobjectArray)env->NewObjectArray(
        total, env->FindClass("java/lang/String"), env->NewStringUTF("")
    );
    for (int i = 0; i < total; i++) {
        env->SetObjectArrayElement(ret, i, env->NewStringUTF(features[i]));
    }
    return ret;
}

void Changes(JNIEnv *env, jclass clazz, jobject obj, jint featNum,
             jstring featName, jint value, jlong Lvalue, jboolean boolean, jstring text) {
    
    switch (featNum) {
        case 0:
            write_log(g_buttonsLog, "🔘 Find All Buttons (JNI) pressed");
            FindAllButtonsWithJNI();
            break;
        case 1:
            write_log(g_buttonsLog, "🔘 Click CONNECT (JNI) pressed");
            ClickConnectWithJNI();
            break;
        case 2:
            write_log(g_buttonsLog, "📊 Status");
            write_log(g_buttonsLog, "   Game ready: " + std::string(g_gameReady ? "YES" : "NO"));
            write_log(g_buttonsLog, "   Instance: 0x" + std::to_string((uintptr_t)g_gtaInstance));
            write_log(g_buttonsLog, "   Hooks installed: " + std::string(g_hooksInstalled ? "YES" : "NO"));
            break;
    }
}

// ======================== ترد اصلی ========================
void hack_thread() {
    install_hooks_with_delay();
    write_log(g_buttonsLog, "✅ hack_thread finished");
}

// ======================== تابع ورودی ========================
__attribute__((constructor))
void lib_main() {
    mkdir(g_basePath.c_str(), 0777);
    install_crash_handler();
    write_log(g_buttonsLog, "🚀 Mod loaded");
    std::thread(hack_thread).detach();
}