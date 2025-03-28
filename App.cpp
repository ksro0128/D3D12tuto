#include "App.h"

bool App::init(HINSTANCE hInstance, int nCmdShow) {
    // 콘솔 생성 및 연결
    AllocConsole();
    FILE* dummy;
    freopen_s(&dummy, "CONOUT$", "w", stdout);

    if (!window.create(hInstance, nCmdShow)) return false;

    window.setMessageCallback([this](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        return this->handleMessage(hwnd, msg, wParam, lParam);
    });

    if (!renderer.initialize(window.getHwnd())) return false;
    return true;
}

int App::run() {
    MSG msg = {};
    while (true) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);

            if (msg.message == WM_QUIT) {
                return static_cast<int>(msg.wParam);
            }
        }
        renderer.update();
        renderer.render();
    }
    return static_cast<int>(msg.wParam);
}

App::~App() {
    FreeConsole();
}


LRESULT App::handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SIZE:
        {
            UINT width = LOWORD(lParam);
            UINT height = HIWORD(lParam);
            renderer.onResize(width, height);
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

