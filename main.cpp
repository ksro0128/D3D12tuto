#include "App.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    App app;
    if (!app.init(hInstance, nCmdShow)) {
        return 0;
    }

    return app.run();
}
