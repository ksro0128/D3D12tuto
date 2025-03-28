#pragma once

#include <windows.h>
#include <functional>

using WindowMessageCallback = std::function<LRESULT(HWND, UINT, WPARAM, LPARAM)>;

class Window {
public:
    Window();
    ~Window();

    bool create(HINSTANCE hInstance, int nCmdShow);
    HWND getHwnd() const { return hwnd; }
    void setMessageCallback(WindowMessageCallback callback) { messageCallback = callback; }

private:
    HWND hwnd = nullptr;
    HINSTANCE hInstance = nullptr;
    const wchar_t* className = L"D3D12WindowClass";
    WindowMessageCallback messageCallback = nullptr;

    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};
