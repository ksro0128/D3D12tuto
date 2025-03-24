#include <windows.h>

// 윈도우 메시지 처리 함수 선언
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// WinMain: 프로그램 시작점
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    const wchar_t CLASS_NAME[] = L"MyWindowClass";

    // 윈도우 클래스 정보 구성
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;         // 메시지 처리 함수
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = CLASS_NAME;

    // 윈도우 클래스 등록
    RegisterClassEx(&wc);

    // 실제 윈도우 생성
    HWND hwnd = CreateWindowEx(
        0,
        CLASS_NAME,                  // 클래스 이름
        L"DirectX 12 Window",        // 창 타이틀
        WS_OVERLAPPEDWINDOW,         // 창 스타일
        CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) return 0;

    // 창 화면에 띄우기
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // 메시지 루프
    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);   // 키보드 입력 등 번역
        DispatchMessage(&msg);    // 메시지 → WndProc으로 전달
    }

    return static_cast<int>(msg.wParam);
}

// 메시지 처리 함수
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_DESTROY:          // 창이 닫힐 때
        PostQuitMessage(0);   // 메시지 루프 종료
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam); // 기본 처리
}
