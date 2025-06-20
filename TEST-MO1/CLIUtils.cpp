#include "CLIUtils.h"
#include <iostream>

void CLIUtils::clearScreen() {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

void CLIUtils::printHeader() {
    std::cout << "  _____  ____   ____   ____   ____   ____   ___  ___\n";
    std::cout << " / ____ / ___| / __ \\ |  _ \\ | ___| / ____| \\ \\  / /  \n";
    std::cout << "| |    | |___ | |  | || |_| || |__ | |____   \\ \\/ / \n";
    std::cout << "| |____ ___| || |  | || ___/ | ___|  ___| |   |  |  \n";
    std::cout << " \\____| ____/ \\ \\__/ /|_|    |____| |____/    |__|  \n";
    std::cout << "----------------------------------------------------\n";
    std::cout << "\033[32mWelcome to CSOPESY Emulator!\033[0m\n\n";
    std::cout << "\033[32mDevelopers:\033[0m\n\n";
    std::cout << "  - \033[34mArco, Gabrielle Mae\033[0m\n";
    std::cout << "  - \033[34mCanayon Jr., Roger\033[0m\n";
    std::cout << "  - \033[34mDe Los Reyes, Carl Justin\033[0m\n";
    std::cout << "  - \033[34mViguilla, Andrei Dominic\033[0m\n";
    std::cout << "----------------------------------------------------\n\n";
}
