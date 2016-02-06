#pragma once

#include <string>
#include <stdexcept>
#include <dlfcn.h>

template<typename T>
class DynamicLib
{
public:
    typename T::produce produce_method;
    typename T::destruct destruct_method;

    DynamicLib(const std::string& lib_path) :
        lib_path(lib_path)
    {
    }

    //  TODO debug why this doesn't work
    typename T::lib_object* instance()
    {
        //  TODO singleton, lazy, concurrency
        this->load();
        lib_object = this->produce_method();
        return lib_object;
    }

    void load()
    {
        load_lib();
        load_produce_func();
        load_destruct_func();
    }

    ~DynamicLib()
    {
        if (lib_object != nullptr) {
            destruct_method(lib_object);
        }
    }

private:
    std::string lib_path;
    void *dynamic_lib;
    typename T::lib_object* lib_object;

    void load_lib()
    {
        dynamic_lib = dlopen(lib_path.c_str(), RTLD_LAZY);
        if (!dynamic_lib) {
            throw std::runtime_error(dlerror());
        }
        dlerror();
    }

    void load_produce_func()
    {
        produce_method = (typename T::produce) dlsym(
            dynamic_lib,
            T::produce_name.c_str()
        );
        const char* dlsym_error = dlerror();
        if (dlsym_error) {
            throw std::runtime_error(dlsym_error);
        }
    }

    void load_destruct_func()
    {
        destruct_method = (typename T::destruct) dlsym(
            dynamic_lib,
            T::destruct_name.c_str()
        );
        const char *dlsym_error = dlerror();
        if (dlsym_error) {
            throw std::runtime_error(dlsym_error);
        }
    }
};
