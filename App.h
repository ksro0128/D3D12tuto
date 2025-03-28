#pragma once
#include <windows.h>
#include "Window.h"
#include "Renderer.h"

class App {
public:
    App() = default;
    ~App();

    bool init(HINSTANCE hInstance, int nCmdShow);
    int run();

private:
    Window window;
    Renderer renderer;

    LRESULT handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};
