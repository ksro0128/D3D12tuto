#include "Window.h"

Window::Window() = default;

Window::~Window() = default;

bool Window::create(HINSTANCE hInstance, int nCmdShow) {
    this->hInstance = hInstance;

    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = Window::wndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = className;

    if (!RegisterClassEx(&wc)) {
        MessageBox(nullptr, L"윈도우 클래스 등록 실패", L"Error", MB_OK);
        return false;
    }

    hwnd = CreateWindowEx(
        0,
        className,
        L"Direct3D 12 Window",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1280, 720,
        nullptr, nullptr,
        hInstance,
        nullptr
    );

    if (!hwnd) {
        MessageBox(nullptr, L"윈도우 생성 실패", L"Error", MB_OK);
        return false;
    }

    // 윈도우 생성 이후에 this 포인터를 hwnd에 저장
    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    return true;
}

LRESULT CALLBACK Window::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // 윈도우 생성 이후에 this 포인터를 hwnd에 저장했는지 확인하고 꺼내기
    Window* window = reinterpret_cast<Window*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    // 콜백이 등록되어 있다면 호출
    if (window && window->messageCallback) {
        return window->messageCallback(hwnd, msg, wParam, lParam);
    }

    // 기본 메시지 처리
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
