#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl.h> // 스마트 포인터용 (Microsoft::WRL::ComPtr)
#include "d3dx12.h"
#include <stdexcept>
#include <iostream>

using Microsoft::WRL::ComPtr;

struct Vertex {
    float position[3];  // x, y, z
    float color[3];     // r, g, b
};


// 윈도우 메시지 처리 함수 선언
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);


ComPtr<ID3DBlob> CompileShader(const std::wstring& filename, const std::string& entryPoint, const std::string& target)
{
    ComPtr<ID3DBlob> shaderBlob;
    ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3DCompileFromFile(
        filename.c_str(),
        nullptr, nullptr,
        entryPoint.c_str(),
        target.c_str(),
        D3DCOMPILE_ENABLE_STRICTNESS,
        0,
        &shaderBlob,
        &errorBlob
    );

    if (FAILED(hr)) {
        std::cout << "Shader compilation failed." << std::endl;
        if (errorBlob) {
            std::cerr << "Shader Error:\n" 
                    << static_cast<const char*>(errorBlob->GetBufferPointer())
                    << std::endl;
        }
        throw std::runtime_error("Shader compilation failed.");
    }


    return shaderBlob;
}

// WinMain: 프로그램 시작점
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    AllocConsole();
    freopen("CONOUT$", "w", stdout);

    std::cout << "WinMain 시작" << std::endl;
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

    // DXGI 팩토리 생성
    ComPtr<IDXGIFactory4> dxgiFactory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory));
    if (FAILED(hr)) {
        MessageBox(hwnd, L"DXGI 팩토리 생성 실패", L"에러", MB_OK);
        return -1;
    }

    ComPtr<ID3D12Device> device;

    // 기본 디바이스 생성 (null = 기본 어댑터 사용)
    hr = D3D12CreateDevice(
        nullptr,                   // 기본 하드웨어 어댑터
        D3D_FEATURE_LEVEL_11_0,    // 최소 요구 스펙
        IID_PPV_ARGS(&device)
    );

    // 실패하면 WARP(소프트웨어 디바이스)로 fallback
    if (FAILED(hr)) {
        ComPtr<IDXGIAdapter> warpAdapter;
        dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter));

        hr = D3D12CreateDevice(
            warpAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&device)
        );

        if (FAILED(hr)) {
            MessageBox(hwnd, L"D3D12 디바이스 생성 실패", L"에러", MB_OK);
            return -1;
        }
    }

    // 커맨드 큐 디스크립터 정의
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type     = D3D12_COMMAND_LIST_TYPE_DIRECT; // 일반 렌더링용
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queueDesc.Flags    = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.NodeMask = 0; // 싱글 GPU 시스템이면 0

    // 커맨드 큐 생성
    ComPtr<ID3D12CommandQueue> commandQueue;
    hr = device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue));
    if (FAILED(hr)) {
        MessageBox(hwnd, L"커맨드 큐 생성 실패", L"에러", MB_OK);
        return -1;
    }

    ComPtr<IDXGISwapChain1> swapChain1;

    // 스왑체인 설정
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = 2;                                // 더블 버퍼링
    swapChainDesc.Width = 1280;                                   // 화면 너비
    swapChainDesc.Height = 720;                                   // 화면 높이
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;            // 픽셀 포맷
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // 렌더 타겟 용도
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;    // DX12 권장 방식
    swapChainDesc.SampleDesc.Count = 1;                           // 멀티샘플링 안 함

    // 스왑체인 생성 (Window용)
    hr = dxgiFactory->CreateSwapChainForHwnd(
        commandQueue.Get(),   // 렌더링에 사용할 커맨드 큐
        hwnd,                 // 렌더링 대상 윈도우
        &swapChainDesc,       // 설정 정보
        nullptr,              // 풀스크린 정보 (사용 안 함)
        nullptr,              // 알트+엔터 전환 제어 (사용 안 함)
        &swapChain1
    );

    if (FAILED(hr)) {
        MessageBox(hwnd, L"스왑체인 생성 실패", L"에러", MB_OK);
        return -1;
    }

    // IDXGISwapChain3로 캐스팅 (더 나은 기능 사용 위함)
    ComPtr<IDXGISwapChain3> swapChain;
    swapChain1.As(&swapChain);


    // 👉 백버퍼 개수 상수 정의
    const UINT frameCount = 2;

    // RTV 디스크립터 힙 생성
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = frameCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    hr = device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap));
    if (FAILED(hr)) {
        MessageBox(hwnd, L"RTV 힙 생성 실패", L"에러", MB_OK);
        return -1;
    }

    // RTV 크기 (디스크립터 간 offset)
    UINT rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // 백버퍼 RTV 생성
    ComPtr<ID3D12Resource> renderTargets[frameCount];
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart());

    for (UINT i = 0; i < frameCount; i++) {
        // 스왑체인에서 백버퍼 얻기
        hr = swapChain->GetBuffer(i, IID_PPV_ARGS(&renderTargets[i]));
        if (FAILED(hr)) {
            MessageBox(hwnd, L"백버퍼 GetBuffer 실패", L"에러", MB_OK);
            return -1;
        }

        // RTV 생성
        device->CreateRenderTargetView(renderTargets[i].Get(), nullptr, rtvHandle);

        // 다음 디스크립터 위치로 이동
        rtvHandle.Offset(1, rtvDescriptorSize);
    }

    // 삼각형 정점 데이터 정의
    Vertex triangleVertices[] = {
        { { 0.0f,  0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f } }, // 위쪽 (빨강)
        { { 0.5f, -0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f } }, // 오른쪽 아래 (초록)
        { { -0.5f, -0.5f, 0.0f },{ 0.0f, 0.0f, 1.0f } }  // 왼쪽 아래 (파랑)
    };

    const UINT vertexBufferSize = sizeof(triangleVertices);

    ComPtr<ID3D12Resource> vertexBuffer;

    // 힙 속성 설정 (업로드 힙: CPU에서 GPU로 데이터 보내기 용도)
    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);

    hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&vertexBuffer)
    );
    if (FAILED(hr)) {
        MessageBox(hwnd, L"버텍스 버퍼 생성 실패", L"에러", MB_OK);
        return -1;
    }
    std::cout << "버텍스 버퍼 생성 성공" << std::endl;

    // 정점 데이터를 GPU 메모리에 복사
    UINT8* vertexDataBegin;
    CD3DX12_RANGE readRange(0, 0); // 읽을 일 없으므로
    vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&vertexDataBegin));
    memcpy(vertexDataBegin, triangleVertices, vertexBufferSize);
    vertexBuffer->Unmap(0, nullptr);

    D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
    vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
    vertexBufferView.StrideInBytes = sizeof(Vertex); // 정점 하나당 크기
    vertexBufferView.SizeInBytes = vertexBufferSize; // 전체 크기

    std::cout << "버텍스 버퍼 뷰 생성 성공" << std::endl;

    // 1. 루트 시그니처 생성 (비어 있는 루트 시그니처)
    ComPtr<ID3D12RootSignature> rootSignature;
    {
        D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
        rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> signatureBlob;
        ComPtr<ID3DBlob> errorBlob;
        D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
        device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
    }
    std::cout << "루트 시그니처 생성 성공" << std::endl;
    // 2. 셰이더 컴파일
    ComPtr<ID3DBlob> vertexShader = CompileShader(L"shader.hlsl", "VSMain", "vs_5_0");
    ComPtr<ID3DBlob> pixelShader = CompileShader(L"shader.hlsl", "PSMain", "ps_5_0");
    std::cout << "셰이더 컴파일 성공" << std::endl;
    // 3. 입력 레이아웃
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        // POSITION: float3 → 12바이트
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },

        // COLOR: float3 → 다음 오프셋부터 12바이트
        { "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };
    std::cout << "입력 레이아웃 설정 성공" << std::endl;

    // 4. PSO 설정
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    psoDesc.pRootSignature = rootSignature.Get();
    psoDesc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
    psoDesc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;

    ComPtr<ID3D12PipelineState> pipelineState;
    device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState));
    std::cout << "PSO 설정 성공" << std::endl;

    // 커맨드 할당자 & 커맨드 리스트 생성
    ComPtr<ID3D12CommandAllocator> commandAllocator;
    hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
    if (FAILED(hr)) {
        MessageBox(hwnd, L"커맨드 할당자 생성 실패", L"에러", MB_OK);
        return -1;
    }
    std::cout << "커맨드 할당자 생성 성공" << std::endl;

    ComPtr<ID3D12GraphicsCommandList> commandList;
    hr = device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        commandAllocator.Get(),
        pipelineState.Get(),
        IID_PPV_ARGS(&commandList)
    );
    if (FAILED(hr)) {
        MessageBox(hwnd, L"커맨드 리스트 생성 실패", L"에러", MB_OK);
        return -1;
    }
    std::cout << "커맨드 리스트 생성 성공" << std::endl;
    commandList->Close(); // 첫 프레임에 다시 Reset할 예정이므로 Close

    // 펜스(Fence) 생성 (GPU 동기화용)
    ComPtr<ID3D12Fence> fence;
    UINT64 fenceValue = 0;
    hr = device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    if (FAILED(hr)) {
        MessageBox(hwnd, L"펜스 생성 실패", L"에러", MB_OK);
        return -1;
    }
    HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    UINT frameIndex = swapChain->GetCurrentBackBufferIndex();


    // 창 화면에 띄우기
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // 메시지 루프
    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);   // 키보드 입력 등 번역
        DispatchMessage(&msg);    // 메시지 → WndProc으로 전달
        // 프레임 인덱스 가져오기


        // ---------------------------
        // 커맨드 리스트 기록 시작
        // ---------------------------
        commandAllocator->Reset();
        commandList->Reset(commandAllocator.Get(), pipelineState.Get());

        D3D12_VIEWPORT viewport = {};
        viewport.TopLeftX = 0;
        viewport.TopLeftY = 0;
        viewport.Width = 1280;
        viewport.Height = 720;
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;

        D3D12_RECT scissorRect = {};
        scissorRect.left = 0;
        scissorRect.top = 0;
        scissorRect.right = 1280;
        scissorRect.bottom = 720;

        commandList->RSSetViewports(1, &viewport);
        commandList->RSSetScissorRects(1, &scissorRect);


        // 백버퍼 전환: Present 상태 → Render Target 상태
        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            renderTargets[frameIndex].Get(),
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET
        );
        commandList->ResourceBarrier(1, &barrier);

        // 렌더 타겟 뷰 설정
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandleRT(
            rtvHeap->GetCPUDescriptorHandleForHeapStart(),
            frameIndex,
            rtvDescriptorSize
        );
        commandList->OMSetRenderTargets(1, &rtvHandleRT, FALSE, nullptr);

        // 백버퍼를 클리어 (회색 배경)
        // const float clearColor[] = { 0.2f, 0.2f, 0.2f, 1.0f };
        const float clearColor[] = { 0.8f, 0.8f, 0.8f, 1.0f }; // 밝은 회색

        commandList->ClearRenderTargetView(rtvHandleRT, clearColor, 0, nullptr);

        // 파이프라인 설정
        commandList->SetGraphicsRootSignature(rootSignature.Get());
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commandList->IASetVertexBuffers(0, 1, &vertexBufferView);

        // 드로우 호출
        commandList->DrawInstanced(3, 1, 0, 0);

        // 다시 Present 상태로 전환
        barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            renderTargets[frameIndex].Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT
        );
        commandList->ResourceBarrier(1, &barrier);

        // 커맨드 리스트 닫고 제출
        commandList->Close();
        ID3D12CommandList* commandLists[] = { commandList.Get() };
        commandQueue->ExecuteCommandLists(1, commandLists);

        // 스왑체인 Present
        swapChain->Present(1, 0);

        // GPU 동기화
        fenceValue++;
        commandQueue->Signal(fence.Get(), fenceValue);
        if (fence->GetCompletedValue() < fenceValue) {
            fence->SetEventOnCompletion(fenceValue, fenceEvent);
            WaitForSingleObject(fenceEvent, INFINITE);
        }

        // 다음 프레임 인덱스 업데이트
        frameIndex = swapChain->GetCurrentBackBufferIndex();
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
