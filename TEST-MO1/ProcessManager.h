#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "Screen.h"
#include "Config.h"

class Scheduler;

class ProcessManager {
public:
    static void setScheduler(Scheduler* sched);
    static void createAndAttach(const std::string& name, const Config& config);
    static void resumeScreen(const std::string& name);
    static void listScreens(const Config& config);
    static void generateReport();
    static void registerProcess(std::shared_ptr<Screen> process);

    static bool hasProcess(const std::string& name);
    static std::shared_ptr<Screen> getProcess(const std::string& name);

private:
    static std::map<std::string, std::shared_ptr<Screen>> processes;
    static std::mutex processMutex;
    static Scheduler* scheduler;
};
