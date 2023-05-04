#pragma once

#include <string_view>
#include <map>
#include <memory>
#include <functional>
#include "Integrator.h"
#include "LumenScene.h"
#include "Framework/LumenInstance.h"

struct IntegratorRegistry{
    struct Entry{
        std::function<std::unique_ptr<Integrator>(LumenInstance* instance, LumenScene* lumen_scene)> create;
    };
    static std::map<std::string_view, Entry> integrators;
    IntegratorRegistry(std::string_view name, std::function<std::unique_ptr<Integrator>(LumenInstance* instance, LumenScene* lumen_scene)> create) { integrators[name] = {create}; }
};

// easy to use macro to add registered integrators
#define REGISTER(Integrator) static IntegratorRegistry IntegratorReg_##Integrator(#Integrator, std::make_unique<Integrator, LumenInstance*, LumenScene*>)
