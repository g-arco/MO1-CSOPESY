#ifndef PROCESSMANAGER_H
#define PROCESSMANAGER_H

#include "Screen.h"
#include "Config.h"
#include "Scheduler.h"
#include <map>
#include <memory>

class ProcessManager {
public:
    static void createAndAttach(const std::string& name, const Config& config, Scheduler& scheduler);
    static void resumeScreen(const std::string& name);
    static void listScreens(const Config& config);
    static void generateReport();
    static void registerProcess(std::shared_ptr<Screen> process);

    static const std::map<std::string, std::shared_ptr<Screen>>& getProcesses() { return processes; }

private:
    static std::map<std::string, std::shared_ptr<Screen>> processes;
};

#endif
