#include "Renderer.h"
#include <stdexcept>
#include <d3dcompiler.h>
#include "d3dx12.h"
#include <dxgidebug.h>
#include <iostream>
#include <DirectXTex.h>

const int objectCount = 1;
const UINT indexCount = 36;
MVP mvpData[objectCount];

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
    swapDesc.Width = windowWidth;
    swapDesc.Height = windowHeight;
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

    // 상수 버퍼 생성
    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC cbDesc = CD3DX12_RESOURCE_DESC::Buffer((sizeof(MVP) * objectCount + 255) & ~255);

    device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &cbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&constantBuffer)
    );

    // 매핑 (CPU에서 직접 접근 가능하게 함)
    CD3DX12_RANGE readRange(0, 0);
    constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&constantBufferPtr));

    // cbvsrv 힙 생성
    D3D12_DESCRIPTOR_HEAP_DESC cbvsrvHeapDesc = {};
    cbvsrvHeapDesc.NumDescriptors = objectCount + 1;
    cbvsrvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvsrvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    if (FAILED(device->CreateDescriptorHeap(&cbvsrvHeapDesc, IID_PPV_ARGS(&cbvsrvHeap)))) {
        MessageBox(nullptr, L"CBV SRV Heap 생성 실패", L"Error", MB_OK);
        return false;
    }

    // 텍스처 생성
    if (!createTexture()) return false;

    // 파이프라인 생성
    if (!createPipeline()) return false;

    // 정점 버퍼 생성
    if (!createVertexBuffer()) return false;

    return true;
}

bool Renderer::createTexture() {
    using namespace DirectX;

    // 1. 텍스처 로딩
    ScratchImage image;
    HRESULT hr = LoadFromWICFile(L"textures/texture.png", WIC_FLAGS_NONE, nullptr, image);
    if (FAILED(hr)) {
        MessageBox(nullptr, L"텍스처 로드 실패", L"Error", MB_OK);
        return false;
    }

    const Image* img = image.GetImage(0, 0, 0);

    // 2. 리소스 생성
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = img->width;
    texDesc.Height = static_cast<UINT>(img->height);
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = img->format;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    if (FAILED(device->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&texture)))) {
        return false;
    }
    
    // 3. 업로드 힙 생성
    Microsoft::WRL::ComPtr<ID3D12Resource> textureUploadHeap;
    UINT64 uploadBufferSize = GetRequiredIntermediateSize(texture.Get(), 0, 1);

    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);

    if (FAILED(device->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&textureUploadHeap)))) {
        return false;
    }

    // 4. 텍스처 복사
    D3D12_SUBRESOURCE_DATA textureData = {};
    textureData.pData = img->pixels;
    textureData.RowPitch = img->rowPitch;
    textureData.SlicePitch = img->slicePitch;

    commandAllocator->Reset();
    commandList->Reset(commandAllocator.Get(), nullptr);
    UpdateSubresources(commandList.Get(), texture.Get(), textureUploadHeap.Get(), 0, 0, 1, &textureData);

    // 5. 리소스 상태 전이
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        texture.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    );
    commandList->ResourceBarrier(1, &barrier);
    commandList->Close();

    ID3D12CommandList* lists[] = { commandList.Get() };
    commandQueue->ExecuteCommandLists(1, lists);

    commandQueue->Signal(fence.Get(), fenceValue);
    waitForGPU(); // GPU가 전이 끝날 때까지 대기
    fenceValue++;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(
        cbvsrvHeap->GetCPUDescriptorHandleForHeapStart(),
        objectCount,
        device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
    );

    device->CreateShaderResourceView(
        texture.Get(),
        &srvDesc,
        srvHandle
    );

    // 7. 샘플러 디스크립터 힙 생성
    D3D12_DESCRIPTOR_HEAP_DESC samplerHeapDesc = {};
    samplerHeapDesc.NumDescriptors = 1;
    samplerHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    samplerHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    if (FAILED(device->CreateDescriptorHeap(&samplerHeapDesc, IID_PPV_ARGS(&samplerHeap)))) {
        return false;
    }

    D3D12_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;

    device->CreateSampler(&samplerDesc, samplerHeap->GetCPUDescriptorHandleForHeapStart());

    return true;
}


bool Renderer::createPipeline() {
    // 1. 루트 시그니처 생성 (비어 있는 루트 시그니처)
    {
        // CBV range: register(b0)
        D3D12_DESCRIPTOR_RANGE cbvRange = {};
        cbvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        cbvRange.NumDescriptors = 1;
        cbvRange.BaseShaderRegister = 0;
        cbvRange.RegisterSpace = 0;
        cbvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE srvRange = {};
        srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors = 1;
        srvRange.BaseShaderRegister = 0; // t0
        srvRange.RegisterSpace = 0;
        srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE samplerRange = {};
        samplerRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        samplerRange.NumDescriptors = 1;
        samplerRange.BaseShaderRegister = 0; // s0
        samplerRange.RegisterSpace = 0;
        samplerRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        // 루트 파라미터 3개 정의
        D3D12_ROOT_PARAMETER rootParams[3] = {};

        // CBV table (b0)
        rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[0].DescriptorTable.pDescriptorRanges = &cbvRange;
        rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

        // SRV table (t0)
        rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[1].DescriptorTable.pDescriptorRanges = &srvRange;
        rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        // Sampler table (s0)
        rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[2].DescriptorTable.pDescriptorRanges = &samplerRange;
        rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        // 루트 시그니처 전체 정의
        D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
        rootSigDesc.NumParameters = 3;
        rootSigDesc.pParameters = rootParams;
        rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;

        if (FAILED(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob))) {
            if (errorBlob) {
                OutputDebugStringA((char*)errorBlob->GetBufferPointer());
            }
            return false;
        }

        if (FAILED(device->CreateRootSignature(
            0,
            signatureBlob->GetBufferPointer(),
            signatureBlob->GetBufferSize(),
            IID_PPV_ARGS(&rootSignature)))) {
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
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    // 4. PSO 생성
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    psoDesc.pRootSignature = rootSignature.Get();
    psoDesc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
    psoDesc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
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
        XMFLOAT3 position;
        XMFLOAT3 normal;
        XMFLOAT2 uv;
    };

    Vertex cubeVertices[] = {
        {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}},
        {{0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}},
        {{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}},
        {{-0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}},
        
        {{-0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{-0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
        
        {{-0.5f, -0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{-0.5f, -0.5f, 0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
        {{-0.5f, 0.5f, 0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
        {{-0.5f, 0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
        
        {{0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{0.5f, -0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
        {{0.5f, 0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
        {{0.5f, 0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
        
        {{-0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
        {{0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},
        {{0.5f, -0.5f, 0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},
        {{-0.5f, -0.5f, 0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}},
        
        {{-0.5f, 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
        {{0.5f, 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        {{0.5f, 0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
        {{-0.5f, 0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
    };

    uint16_t cubeIndices[] = {
        0,	2,	1,	2,	0,	3,	4,	5,	6,	6,	7,	4,	8,	9,	10, 10, 11, 8,
		12, 14, 13, 14, 12, 15, 16, 17, 18, 18, 19, 16, 20, 22, 21, 22, 20, 23,
    };

    const UINT vertexBufferSize = sizeof(cubeVertices);
    const UINT indexBufferSize = sizeof(cubeIndices);

    // 정점 버퍼 생성
    {
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

        void* mappedData = nullptr;
        vertexBuffer->Map(0, nullptr, &mappedData);
        memcpy(mappedData, cubeVertices, vertexBufferSize);
        vertexBuffer->Unmap(0, nullptr);

        vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
        vertexBufferView.StrideInBytes = sizeof(Vertex);
        vertexBufferView.SizeInBytes = vertexBufferSize;
    }

    // 인덱스 버퍼 생성
    {
        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize);

        if (FAILED(device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&indexBuffer)))) {
            return false;
        }

        void* mappedData = nullptr;
        indexBuffer->Map(0, nullptr, &mappedData);
        memcpy(mappedData, cubeIndices, indexBufferSize);
        indexBuffer->Unmap(0, nullptr);

        indexBufferView.BufferLocation = indexBuffer->GetGPUVirtualAddress();
        indexBufferView.Format = DXGI_FORMAT_R16_UINT;
        indexBufferView.SizeInBytes = indexBufferSize;
    }

    // 3. CBV 생성
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = constantBuffer->GetGPUVirtualAddress(); // 상수 버퍼 주소
    UINT cbSize = (sizeof(MVP) + 255) & ~255;
    for (UINT i = 0; i < objectCount; ++i) {
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = constantBuffer->GetGPUVirtualAddress() + i * cbSize;
        cbvDesc.SizeInBytes = cbSize;

        CD3DX12_CPU_DESCRIPTOR_HANDLE handle(
            cbvsrvHeap->GetCPUDescriptorHandleForHeapStart(),
            i,
            device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
        );
        device->CreateConstantBufferView(&cbvDesc, handle);
    }
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
    
    // 디스크립터 힙 바인딩 (CBV, SRV, UAV용 힙)
    ID3D12DescriptorHeap* heaps[] = { cbvsrvHeap.Get(), samplerHeap.Get() };
    commandList->SetDescriptorHeaps(_countof(heaps), heaps);

    // 루트 파라미터에 CBV 디스크립터 테이블 설정
    commandList->SetGraphicsRootDescriptorTable(
        0, // 루트 파라미터 인덱스 (register(b0)에 해당)
        cbvsrvHeap->GetGPUDescriptorHandleForHeapStart()
    );

    CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(
        cbvsrvHeap->GetGPUDescriptorHandleForHeapStart(),
        objectCount,
        device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
    );
    commandList->SetGraphicsRootDescriptorTable(
        1,
        srvHandle
    );

    // 루트 파라미터에 샘플러 디스크립터 테이블 설정
    commandList->SetGraphicsRootDescriptorTable(
        2, // 루트 파라미터 인덱스 (register(s0)에 해당)
        samplerHeap->GetGPUDescriptorHandleForHeapStart()
    );

    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
    commandList->IASetIndexBuffer(&indexBufferView);

    for (UINT i = 0; i < objectCount; ++i) {
        // 디스크립터 핸들 오프셋 계산
        CD3DX12_GPU_DESCRIPTOR_HANDLE cbvHandle(
            cbvsrvHeap->GetGPUDescriptorHandleForHeapStart(),
            i,
            device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
        );

        // 루트 파라미터에 현재 오브젝트의 CBV 디스크립터 설정
        commandList->SetGraphicsRootDescriptorTable(0, cbvHandle);

        // 드로우 호출
        commandList->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);
    }

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
    using namespace DirectX;

    for (UINT i = 0; i < objectCount; ++i) {
        float angle = 0.01f * i + static_cast<float>(GetTickCount64()) * 0.001f;

        XMMATRIX model = XMMatrixTranslation(static_cast<float>(i) * 1.5f, 0.0f, 0.0f) *
                         XMMatrixRotationY(angle);

        XMMATRIX view = XMMatrixLookAtLH(
            XMVectorSet(0.0f, 0.0f, -4.0f, 1.0f),
            XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f),
            XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)
        );

        XMMATRIX proj = XMMatrixPerspectiveFovLH(
            XM_PIDIV4,
            static_cast<float>(windowWidth) / static_cast<float>(windowHeight),
            0.1f, 100.0f
        );

        MVP mvp = {
            XMMatrixTranspose(model),
            XMMatrixTranspose(view),
            XMMatrixTranspose(proj)
        };

        // 복사할 위치 계산
        memcpy(
            constantBufferPtr + i * sizeof(MVP),
            &mvp,
            sizeof(MVP)
        );
    }
}

Renderer::~Renderer() {
    waitForGPU(); // 동기화
    
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

