cmake_minimum_required(VERSION 3.15)
project(DirectXWindow)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 유니코드 설정
add_definitions(-DUNICODE -D_UNICODE)

add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

# Win32 GUI 앱으로 설정
add_executable(DirectXWindow WIN32 main.cpp
                App.cpp
                Window.cpp
                Renderer.cpp)

# 윈도우 관련 라이브러리 링크
target_link_libraries(DirectXWindow
    PRIVATE
    user32
    gdi32
    d3d12
    dxgi
    dxguid
    d3dcompiler
)
