#include "Config.h"
#include <type_traits>

Config config;
std::atomic<int> activeCores = 0;