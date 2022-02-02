#include "config_reader.h"

#include <cassert>

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

using namespace std;

int main(int argc, char* argv[]) {
    assert(common::config::watchDirectory("config"));

    for (int i = 0; i < 20; ++i) {
        string debug_value = common::config::load("example", "debug");
        cout << "example::debug := " << debug_value << endl;
        this_thread::sleep_for(1000ms);
    }
}
