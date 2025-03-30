#pragma once

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include <DirectXMath.h>

using namespace DirectX;

struct alignas(256) MVP
{
    XMMATRIX model;
    XMMATRIX view;
    XMMATRIX projection;
};

extern const int objectCount;
extern const UINT indexCount;
extern MVP mvpData[];


class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    bool initialize(HWND hwnd);
    void update();
    void render();
    void onResize(UINT width, UINT height);


private:
    static const UINT frameCount = 2;

    // 기본 자원
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> renderTargets[frameCount];
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    UINT64 fenceValue = 0;
    HANDLE fenceEvent = nullptr;

    // PSO 관련
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;

    // 정점 버퍼
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};

    // 인덱스 버퍼
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer;
    D3D12_INDEX_BUFFER_VIEW indexBufferView = {};

    // 상태
    UINT frameIndex = 0;
    UINT rtvDescriptorSize = 0;

    // 윈도우 크기
    UINT windowWidth = 1280;
    UINT windowHeight = 720;

    // 상수 버퍼
    Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer;
    UINT8* constantBufferPtr = nullptr;
    MVP mvpData = {};

    // cbvsrv 힙
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> cbvsrvHeap;

    // 텍스처
    Microsoft::WRL::ComPtr<ID3D12Resource> texture;

    // 샘플러
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> samplerHeap;
    D3D12_GPU_DESCRIPTOR_HANDLE samplerGpuHandle = {};


    bool createPipeline();
    bool createVertexBuffer();
    bool createTexture();
    void waitForGPU();
};
