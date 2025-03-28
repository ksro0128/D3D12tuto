#include "Renderer.h"
#include <stdexcept>
#include <d3dcompiler.h>
#include "d3dx12.h"
#include <dxgidebug.h>
#include <iostream>

bool Renderer::initialize(HWND hwnd) {
#if defined(_DEBUG)
    {
        Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
            debugController->EnableDebugLayer();
        }
    }
#endif

    // DXGI 팩토리
    Microsoft::WRL::ComPtr<IDXGIFactory4> dxgiFactory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)))) {
        MessageBox(hwnd, L"DXGI 팩토리 생성 실패", L"Error", MB_OK);
        return false;
    }

    // 디바이스
    HRESULT hr = D3D12CreateDevice(
        nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
    if (FAILED(hr)) {
        Microsoft::WRL::ComPtr<IDXGIAdapter> warpAdapter;
        dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter));
        hr = D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
        if (FAILED(hr)) {
            MessageBox(hwnd, L"디바이스 생성 실패", L"Error", MB_OK);
            return false;
        }
    }

    // 커맨드 큐
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue));

    // 스왑체인
    DXGI_SWAP_CHAIN_DESC1 swapDesc = {};
    swapDesc.BufferCount = frameCount;
    swapDesc.Width = 1280;
    swapDesc.Height = 720;
    swapDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapDesc.SampleDesc.Count = 1;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> tempSwap;
    dxgiFactory->CreateSwapChainForHwnd(
        commandQueue.Get(), hwnd, &swapDesc, nullptr, nullptr, &tempSwap);

    tempSwap.As(&swapChain);
    frameIndex = swapChain->GetCurrentBackBufferIndex();

    // RTV 힙 생성
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = frameCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap));

    rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // 백버퍼 RTV 생성
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < frameCount; ++i) {
        swapChain->GetBuffer(i, IID_PPV_ARGS(&renderTargets[i]));
        device->CreateRenderTargetView(renderTargets[i].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(1, rtvDescriptorSize);
    }

    // 커맨드 할당자/리스트
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList));
    commandList->Close();

    // 펜스 생성
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    fenceValue = 1;
    fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    // 파이프라인 생성
    if (!createPipeline()) return false;

    // 정점 버퍼 생성
    if (!createVertexBuffer()) return false;

    return true;
}


bool Renderer::createPipeline() {
    // 1. 루트 시그니처 생성 (비어 있는 루트 시그니처)
    {
        D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
        rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;

        if (FAILED(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob))) {
            if (errorBlob) {
                OutputDebugStringA((char*)errorBlob->GetBufferPointer());
            }
            return false;
        }

        if (FAILED(device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature)))) {
            return false;
        }
    }

    // 2. 셰이더 컴파일
    Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
    Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;

    UINT compileFlags = 0;
#if defined(_DEBUG)
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    if (FAILED(D3DCompileFromFile(L"shader.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, nullptr))) {
        MessageBox(nullptr, L"버텍스 셰이더 컴파일 실패", L"Error", MB_OK);
        return false;
    }

    if (FAILED(D3DCompileFromFile(L"shader.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, nullptr))) {
        MessageBox(nullptr, L"픽셀 셰이더 컴파일 실패", L"Error", MB_OK);
        return false;
    }

    // 3. 입력 레이아웃
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    // 4. PSO 생성
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

    if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState)))) {
        MessageBox(nullptr, L"PSO 생성 실패", L"Error", MB_OK);
        return false;
    }

    return true;
}

bool Renderer::createVertexBuffer() {
    struct Vertex {
        float position[3];
        float color[3];
    };

    Vertex triangleVertices[] = {
        { { 0.0f,  0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
        { { 0.5f, -0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
        { {-0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
    };

    const UINT vertexBufferSize = sizeof(triangleVertices);

    // 1. 업로드 힙에 버퍼 생성
    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);

    if (FAILED(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&vertexBuffer)))) {
        return false;
    }

    // 2. 정점 데이터 업로드
    UINT8* mappedData = nullptr;
    CD3DX12_RANGE readRange(0, 0); // 읽지는 않음
    vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&mappedData));
    memcpy(mappedData, triangleVertices, vertexBufferSize);
    vertexBuffer->Unmap(0, nullptr);

    // 3. VertexBufferView 설정
    vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
    vertexBufferView.StrideInBytes = sizeof(Vertex);
    vertexBufferView.SizeInBytes = vertexBufferSize;

    return true;
}


void Renderer::render() {
    // 1. 커맨드 리스트 준비
    commandAllocator->Reset();
    commandList->Reset(commandAllocator.Get(), pipelineState.Get());

    // 2. 뷰포트 & 시저 설정
    D3D12_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>(windowWidth), static_cast<float>(windowHeight), 0.0f, 1.0f };
    D3D12_RECT scissorRect = { 0, 0, static_cast<LONG>(windowWidth), static_cast<LONG>(windowHeight) };
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissorRect);

    // 3. 백버퍼 상태: Present → RenderTarget
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        renderTargets[frameIndex].Get(),
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET
    );
    commandList->ResourceBarrier(1, &barrier);

    // 4. RenderTargetView 설정 + Clear
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
        rtvHeap->GetCPUDescriptorHandleForHeapStart(),
        frameIndex,
        rtvDescriptorSize
    );
    commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    const float clearColor[] = { 0.1f, 0.1f, 0.3f, 1.0f };
    commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

    // 5. 파이프라인 상태 설정
    commandList->SetGraphicsRootSignature(rootSignature.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView);

    // 6. 드로우 호출
    commandList->DrawInstanced(3, 1, 0, 0);

    // 7. 백버퍼 상태: RenderTarget → Present
    barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        renderTargets[frameIndex].Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT
    );
    commandList->ResourceBarrier(1, &barrier);

    // 8. 커맨드 리스트 종료 및 제출
    commandList->Close();
    ID3D12CommandList* cmdLists[] = { commandList.Get() };
    commandQueue->ExecuteCommandLists(1, cmdLists);

    // 9. Present
    swapChain->Present(1, 0);

    // 10. GPU 동기화
    fenceValue++;
    commandQueue->Signal(fence.Get(), fenceValue);
    if (fence->GetCompletedValue() < fenceValue) {
        fence->SetEventOnCompletion(fenceValue, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }

    // 11. 다음 프레임 인덱스 갱신
    frameIndex = swapChain->GetCurrentBackBufferIndex();
}

void Renderer::update() {
    // 추후 애니메이션, 입력 등 로직을 여기에
}

Renderer::~Renderer() {
    waitForGPU(); // 동기화 (선택)
    
    if (fenceEvent) {
        CloseHandle(fenceEvent);
    }

#if defined(_DEBUG)
    Microsoft::WRL::ComPtr<IDXGIDebug1> debug;
    if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
        debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_SUMMARY);
    }
#endif
}

void Renderer::waitForGPU() {
    // GPU가 fenceValue까지 완료했는지 확인
    if (fence->GetCompletedValue() < fenceValue) {
        fence->SetEventOnCompletion(fenceValue, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }
}

void Renderer::onResize(UINT width, UINT height) {
    std::cout << "onResize: " << width << ", " << height << std::endl;
    waitForGPU(); // GPU가 백버퍼 잡고 있으면 문제 생기니까 먼저 대기

    for (UINT i = 0; i < frameCount; ++i) {
        renderTargets[i].Reset(); // 기존 백버퍼 해제
    }

    DXGI_SWAP_CHAIN_DESC swapDesc = {};
    swapChain->GetDesc(&swapDesc);
    swapChain->ResizeBuffers(
        frameCount,
        width,
        height,
        swapDesc.BufferDesc.Format,
        swapDesc.Flags
    );

    frameIndex = swapChain->GetCurrentBackBufferIndex();

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < frameCount; ++i) {
        swapChain->GetBuffer(i, IID_PPV_ARGS(&renderTargets[i]));
        device->CreateRenderTargetView(renderTargets[i].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(1, rtvDescriptorSize);
    }

    windowWidth = width;
    windowHeight = height;
}

