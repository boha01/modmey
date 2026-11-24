#pragma once
// Runtime-resolved Il2Cpp API via dlsym(RTLD_NOLOAD) from libil2cpp.so
// Includes full symbol set and null/bounds-checked lookup helpers.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <android/log.h>

#define IL_LOG(...) __android_log_print(ANDROID_LOG_INFO, "ModMenu", __VA_ARGS__)

struct Il2CppDomain;
struct Il2CppAssembly;
struct Il2CppImage;
struct Il2CppClass;
struct Il2CppMethod;
struct Il2CppField;
struct Il2CppString;
struct Il2CppType;
struct MethodInfo;

using il2cpp_domain_get_t                 = Il2CppDomain* (*)();
using il2cpp_domain_get_assemblies_t      = const Il2CppAssembly** (*)(const Il2CppDomain*, size_t*);
using il2cpp_assembly_get_image_t         = const Il2CppImage* (*)(const Il2CppAssembly*);
using il2cpp_class_from_name_t            = Il2CppClass* (*)(const Il2CppImage*, const char*, const char*);
using il2cpp_class_get_method_from_name_t = const Il2CppMethod* (*)(Il2CppClass*, const char*, int);
using il2cpp_class_get_methods_t          = const Il2CppMethod* (*)(Il2CppClass*, void**);
using il2cpp_method_get_name_t            = const char* (*)(const Il2CppMethod*);
using il2cpp_method_get_param_count_t     = uint32_t (*)(const Il2CppMethod*);
using il2cpp_class_get_field_from_name_t  = Il2CppField* (*)(Il2CppClass*, const char*);
using il2cpp_field_get_offset_t           = size_t (*)(Il2CppField*);
using il2cpp_field_static_set_value_t     = void (*)(Il2CppField*, void*);
using il2cpp_runtime_invoke_t             = void* (*)(const Il2CppMethod*, void*, void**, void**);
using il2cpp_thread_attach_t              = void* (*)(Il2CppDomain*);
using il2cpp_string_new_t                 = Il2CppString* (*)(const char*);
using il2cpp_object_get_class_t           = Il2CppClass* (*)(void*);
using il2cpp_object_unbox_t               = void* (*)(void*);
using il2cpp_class_get_type_t             = const Il2CppType* (*)(Il2CppClass*);

struct Il2CppApi {
    il2cpp_domain_get_t                 domain_get = nullptr;
    il2cpp_domain_get_assemblies_t      domain_get_assemblies = nullptr;
    il2cpp_assembly_get_image_t         assembly_get_image = nullptr;
    il2cpp_class_from_name_t            class_from_name = nullptr;
    il2cpp_class_get_method_from_name_t class_get_method = nullptr;
    il2cpp_class_get_methods_t          class_get_methods = nullptr;
    il2cpp_method_get_name_t            method_get_name = nullptr;
    il2cpp_method_get_param_count_t     method_get_param_count = nullptr;
    il2cpp_class_get_field_from_name_t  class_get_field = nullptr;
    il2cpp_field_get_offset_t           field_get_offset = nullptr;
    il2cpp_field_static_set_value_t     field_static_set = nullptr;
    il2cpp_runtime_invoke_t             runtime_invoke = nullptr;
    il2cpp_thread_attach_t              thread_attach = nullptr;
    il2cpp_string_new_t                 string_new = nullptr;
    il2cpp_object_get_class_t           object_get_class = nullptr;
    il2cpp_object_unbox_t               object_unbox = nullptr;
    il2cpp_class_get_type_t             class_get_type = nullptr;
    bool ok = false;

    bool init(void* handle) {
        if (!handle) return false;
        auto sym = [&](const char* n) { return dlsym(handle, n); };
        domain_get             = reinterpret_cast<il2cpp_domain_get_t>(sym("il2cpp_domain_get"));
        domain_get_assemblies  = reinterpret_cast<il2cpp_domain_get_assemblies_t>(sym("il2cpp_domain_get_assemblies"));
        assembly_get_image     = reinterpret_cast<il2cpp_assembly_get_image_t>(sym("il2cpp_assembly_get_image"));
        class_from_name        = reinterpret_cast<il2cpp_class_from_name_t>(sym("il2cpp_class_from_name"));
        class_get_method       = reinterpret_cast<il2cpp_class_get_method_from_name_t>(sym("il2cpp_class_get_method_from_name"));
        class_get_methods      = reinterpret_cast<il2cpp_class_get_methods_t>(sym("il2cpp_class_get_methods"));
        method_get_name        = reinterpret_cast<il2cpp_method_get_name_t>(sym("il2cpp_method_get_name"));
        method_get_param_count = reinterpret_cast<il2cpp_method_get_param_count_t>(sym("il2cpp_method_get_param_count"));
        class_get_field        = reinterpret_cast<il2cpp_class_get_field_from_name_t>(sym("il2cpp_class_get_field_from_name"));
        field_get_offset       = reinterpret_cast<il2cpp_field_get_offset_t>(sym("il2cpp_field_get_offset"));
        field_static_set       = reinterpret_cast<il2cpp_field_static_set_value_t>(sym("il2cpp_field_static_set_value"));
        runtime_invoke         = reinterpret_cast<il2cpp_runtime_invoke_t>(sym("il2cpp_runtime_invoke"));
        thread_attach          = reinterpret_cast<il2cpp_thread_attach_t>(sym("il2cpp_thread_attach"));
        string_new             = reinterpret_cast<il2cpp_string_new_t>(sym("il2cpp_string_new"));
        object_get_class       = reinterpret_cast<il2cpp_object_get_class_t>(sym("il2cpp_object_get_class"));
        object_unbox           = reinterpret_cast<il2cpp_object_unbox_t>(sym("il2cpp_object_unbox"));
        class_get_type         = reinterpret_cast<il2cpp_class_get_type_t>(sym("il2cpp_class_get_type"));
        ok = domain_get && domain_get_assemblies && assembly_get_image && class_from_name && class_get_method;
        IL_LOG("Il2CppApi init ok=%d", ok ? 1 : 0);
        return ok;
    }

    Il2CppClass* findClassNS(Il2CppDomain* domain, const char* ns, const char* name) {
        if (!ok || !domain || !name) return nullptr;
        size_t count = 0;
        const Il2CppAssembly** assemblies = domain_get_assemblies(domain, &count);
        if (!assemblies || count == 0 || count > 4096) return nullptr;
        const char* nsSafe = ns ? ns : "";
        for (size_t i = 0; i < count; i++) {
            if (!assemblies[i]) continue;
            const Il2CppImage* img = assembly_get_image(assemblies[i]);
            if (!img) continue;
            if (auto* k = class_from_name(img, nsSafe, name)) return k;
        }
        return nullptr;
    }

    Il2CppClass* findClass(const char* fullName) {
        if (!ok || !fullName) return nullptr;
        Il2CppDomain* domain = domain_get();
        if (!domain) return nullptr;
        const char* dot = strrchr(fullName, '.');
        if (!dot) return findClassNS(domain, "", fullName);
        char ns[256], name[128];
        size_t nsLen = static_cast<size_t>(dot - fullName);
        if (nsLen >= sizeof(ns)) nsLen = sizeof(ns) - 1;
        memcpy(ns, fullName, nsLen);
        ns[nsLen] = '\0';
        strncpy(name, dot + 1, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        return findClassNS(domain, ns, name);
    }

    static void* methodPointer(const Il2CppMethod* m) {
        return m ? *reinterpret_cast<void* const*>(m) : nullptr;
    }
};

inline Il2CppApi il2cpp;
