#include <cstdio>

#include "vtest.hpp"
int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);  // results stream live (CI, adb shell)
    return vtest::runAll(argc, argv);
}
