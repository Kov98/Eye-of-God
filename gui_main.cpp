#include "gui.hpp"

#include <windows.h>

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
    return deep::run_gui(instance);
}
