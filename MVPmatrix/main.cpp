#include <windows.h>
#include <iostream>
#include <wrl.h>
#include <d3d12.h>
#include "include/d3dx12/d3dx12.h" // Consider including only necessary parts.  Many prefer not to use this wholesale.
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <chrono>
#include <vector>
#include <stdexcept>

using namespace Microsoft::WRL;
using namespace DirectX;

// Link libraries
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

// Forward declarations
void UpdateAndRender();
void GetHardwareAdapter(IDXGIFactory1* pFactory, IDXGIAdapter1** ppAdapter, bool requestHighPerformanceAdapter=true);
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
void InitWindow(HINSTANCE hInstance);
void Initialize();
void LoadAssets();
void LoadShaderPipeline();
void ThrowIfFailed(HRESULT hr); // Centralized error handling

// Constants
const UINT Width = 800;
const UINT Height = 600;
const UINT FrameCount = 2;

// Vertex structure
struct Vertex {
    XMFLOAT3 position;
    XMFLOAT3 color;
};

// Cube data
Vertex cubeVertices[] = {
    {{-1,-1,-1}, {1,0,0}}, {{-1, 1,-1}, {0,1,0}}, {{1, 1,-1}, {0,0,1}}, {{1,-1,-1}, {1,1,0}},
    {{-1,-1, 1}, {1,0,1}}, {{-1, 1, 1}, {0,1,1}}, {{1, 1, 1}, {1,1,1}}, {{1,-1, 1}, {0,0,0}},
};

uint16_t cubeIndices[] = {
    0,1,2, 0,2,3,  4,6,5, 4,7,6,
    4,5,1, 4,1,0,  3,2,6, 3,6,7,
    1,5,6, 1,6,2,  4,0,3, 4,3,7,
};

// Globals (Consider minimizing these)
HWND hwnd = nullptr;
ComPtr<ID3D12Device> device;
ComPtr<IDXGISwapChain3> swapChain;
ComPtr<ID3D12CommandQueue> commandQueue;
ComPtr<ID3D12DescriptorHeap> rtvHeap;
UINT rtvDescriptorSize;
ComPtr<ID3D12Resource> renderTarget[FrameCount]; // Use FrameCount
ComPtr<ID3D12DescriptorHeap> dsvHeap;
UINT dsvDescriptorSize;
ComPtr<ID3D12Resource> depthStencilBuffer;
D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
ComPtr<ID3D12CommandAllocator> commandAllocator;
ComPtr<ID3D12GraphicsCommandList> commandList;
ComPtr<ID3D12Fence> fence;
HANDLE fenceEvent;
UINT64 fenceValue = 1;
UINT frameIndex;

ComPtr<ID3D12RootSignature> rootSignature;
ComPtr<ID3D12PipelineState> pipelineState;
ComPtr<ID3D12Resource> vertexBuffer;
ComPtr<ID3D12Resource> indexBuffer;
D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
D3D12_INDEX_BUFFER_VIEW indexBufferView;

// Descriptor heap is needed for constant buffer view (Root descriptor or Descriptor table)
ComPtr<ID3D12DescriptorHeap> cbvHeap;
ComPtr<ID3D12Resource> constantBuffer;
D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};

// Timer
std::chrono::steady_clock::time_point startTime;

// Helper Functions
void ThrowIfFailed(HRESULT hr) {
    if (FAILED(hr)) {
        std::cerr << "HRESULT failed: 0x" << std::hex << hr << std::endl; // More info
        throw std::runtime_error("HRESULT failed");
    }
}

// Window
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    case WM_PAINT:
        UpdateAndRender();
        break;
    default:
        return DefWindowProc(hWnd, msg, wParam, lParam); // Correct default handling
    }
    return 0; // Consistent return
}

void InitWindow(HINSTANCE hInstance) {
    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"DX12WindowClass";
    if (!RegisterClass(&wc)) {
        ThrowIfFailed(GetLastError()); // Use ThrowIfFailed for WinAPI errors too
    }
    hwnd = CreateWindow(wc.lpszClassName, L"DX12 Cube", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, Width, Height, nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) {
        ThrowIfFailed(GetLastError());
    }
    ShowWindow(hwnd, SW_SHOW);
}

// D3D12 Setup
void LoadAssets() {
    // Vertex buffer
    {
        const UINT bufferSize = sizeof(cubeVertices);
        auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize, D3D12_RESOURCE_FLAG_NONE);
        ThrowIfFailed(device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertexBuffer)));

        void* pData;
        ThrowIfFailed(vertexBuffer->Map(0, nullptr, &pData));
        memcpy(pData, cubeVertices, bufferSize);
        vertexBuffer->Unmap(0, nullptr);

        vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
        vertexBufferView.SizeInBytes = bufferSize;
        vertexBufferView.StrideInBytes = sizeof(Vertex);
    }

    // Index buffer
    {
        const UINT bufferSize = sizeof(cubeIndices);
        auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize, D3D12_RESOURCE_FLAG_NONE);
        ThrowIfFailed(device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&indexBuffer)));

        void* pData;
        ThrowIfFailed(indexBuffer->Map(0, nullptr, &pData));
        memcpy(pData, cubeIndices, bufferSize);
        indexBuffer->Unmap(0, nullptr);

        indexBufferView.BufferLocation = indexBuffer->GetGPUVirtualAddress();
        indexBufferView.Format = DXGI_FORMAT_R16_UINT;
        indexBufferView.SizeInBytes = bufferSize;
    }

    {
		// Create a descriptor heap for the constant buffer view
		D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc = {};
		cbvHeapDesc.NumDescriptors = 1;
		cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		ThrowIfFailed(device->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&cbvHeap)));

		// Create the constant buffer resource
		const UINT bufferSize = (sizeof(XMMATRIX) + 255) & ~255;
		auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize, D3D12_RESOURCE_FLAG_NONE);
		ThrowIfFailed(device->CreateCommittedResource(
			&heapProps, D3D12_HEAP_FLAG_NONE,
			&bufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&constantBuffer)));

		// Create the constant buffer view
		cbvDesc.BufferLocation = constantBuffer->GetGPUVirtualAddress();
		cbvDesc.SizeInBytes = bufferSize;
		device->CreateConstantBufferView(&cbvDesc, cbvHeap->GetCPUDescriptorHandleForHeapStart());
    }
}

void Initialize() {
    UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
            std::cout << "Debug Layer Enabled" << std::endl;
            debugController->EnableDebugLayer();
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif

    ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

    ComPtr<IDXGIAdapter1> hardwareAdapter;
    GetHardwareAdapter(factory.Get(), &hardwareAdapter);

    ThrowIfFailed(D3D12CreateDevice(
        hardwareAdapter.Get(),
        D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(&device)
    ));

    D3D12_COMMAND_QUEUE_DESC qdesc = {};
    ThrowIfFailed(device->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&commandQueue)));

    DXGI_SWAP_CHAIN_DESC1 scDesc = {};
    scDesc.BufferCount = FrameCount; // Use FrameCount
    scDesc.Width = Width;
    scDesc.Height = Height;
    scDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> sc1;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(commandQueue.Get(), hwnd, &scDesc, nullptr, nullptr, &sc1));
    ThrowIfFailed(sc1.As(&swapChain));
    frameIndex = swapChain->GetCurrentBackBufferIndex();

    // RTV Heap
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = FrameCount; // Use FrameCount
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap)));
    rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // Create RTVs
    for (UINT i = 0; i < FrameCount; i++) { // Use FrameCount
        ThrowIfFailed(swapChain->GetBuffer(i, IID_PPV_ARGS(&renderTarget[i])));
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart(), i, rtvDescriptorSize);
        device->CreateRenderTargetView(renderTarget[i].Get(), nullptr, rtvHandle);
    }

    // DSV Heap
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap)));
    dsvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    // Create Depth Stencil Buffer
    D3D12_RESOURCE_DESC depthStencilDesc = {};
    depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthStencilDesc.Width = Width;
    depthStencilDesc.Height = Height;
    depthStencilDesc.DepthOrArraySize = 1;
    depthStencilDesc.MipLevels = 1;
	depthStencilDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthStencilDesc.SampleDesc.Count = 1;
	depthStencilDesc.SampleDesc.Quality = 0;
    depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
    depthOptimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
    depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
    depthOptimizedClearValue.DepthStencil.Stencil = 0;
	auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &depthStencilDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &depthOptimizedClearValue,
        IID_PPV_ARGS(&depthStencilBuffer)));

    // Create the depth stencil view
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	dsvDesc.Texture2D.MipSlice = 0;
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(dsvHeap->GetCPUDescriptorHandleForHeapStart());
    device->CreateDepthStencilView(depthStencilBuffer.Get(), &dsvDesc, dsvHandle);

    // Command Allocator
    ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)));
}

void LoadShaderPipeline() {
    ComPtr<ID3DBlob> vs, ps;
    ThrowIfFailed(D3DCompileFromFile(L"shader.hlsl", nullptr, nullptr, "VSMain", "vs_5_1", 0, 0, &vs, nullptr));
    ThrowIfFailed(D3DCompileFromFile(L"shader.hlsl", nullptr, nullptr, "PSMain", "ps_5_1", 0, 0, &ps, nullptr));

    // Root signature: root constant for MVP
    D3D12_ROOT_PARAMETER rootParams[3] = {};

	// [The first MVP]
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParams[0].Constants.Num32BitValues = 16;
    rootParams[0].Constants.ShaderRegister = 0;
	rootParams[0].Constants.RegisterSpace = 0;

	// [The second MVP]
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParams[1].Descriptor.ShaderRegister = 1;
    rootParams[1].Descriptor.RegisterSpace = 0;

	// [The third MVP]
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 2;
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
	rootParams[2].DescriptorTable.pDescriptorRanges = &range;


    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 3;
    rootSigDesc.pParameters = rootParams;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> sigBlob;
    ThrowIfFailed(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, nullptr));
    ThrowIfFailed(device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature)));

    // Input layout
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    // Pipeline State Object
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    psoDesc.pRootSignature = rootSignature.Get();
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;

    ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState)));
    ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), pipelineState.Get(), IID_PPV_ARGS(&commandList)));
	commandList->Close(); // Close the command list after creating it
}

// Main render loop
void UpdateAndRender() {
    // Reset the command allocator.  This is done at the beginning of each frame.
    ThrowIfFailed(commandAllocator->Reset());

    // Reset the command list.
    ThrowIfFailed(commandList->Reset(commandAllocator.Get(), pipelineState.Get()));

    // Resource barriers for render target and depth stencil
    CD3DX12_RESOURCE_BARRIER rtBarrierBegin = CD3DX12_RESOURCE_BARRIER::Transition(
        renderTarget[frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    commandList->ResourceBarrier(1, &rtBarrierBegin);

    // Get descriptor handles
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(rtvHeap->GetCPUDescriptorHandleForHeapStart(), frameIndex, rtvDescriptorSize);
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(dsvHeap->GetCPUDescriptorHandleForHeapStart());
    // Set viewport and scissor rect
    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width = static_cast<float>(Width);
    viewport.Height = static_cast<float>(Height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissorRect = {};
    scissorRect.left = 0;
    scissorRect.top = 0;
    scissorRect.right = static_cast<LONG>(Width);
    scissorRect.bottom = static_cast<LONG>(Height);

    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissorRect);
    // Set render targets and clear
    commandList->OMSetRenderTargets(1, &rtvHandle, TRUE, &dsvHandle);
    const float clearColor[] = { 0.1f, 0.1f, 0.1f, 1.0f };
    commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    // Set pipeline state and resources
    commandList->SetGraphicsRootSignature(rootSignature.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
    commandList->IASetIndexBuffer(&indexBufferView);

    // MVP Calculation
    auto now = std::chrono::steady_clock::now();
    float time = std::chrono::duration<float>(now - startTime).count();

    XMMATRIX model = XMMatrixRotationY(time) * XMMatrixRotationX(time * 0.5f);
    XMMATRIX view = XMMatrixLookAtLH({ 0.0f, 0.0f, -5.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XMConvertToRadians(90.0f), (float)Width / (float)Height, 0.1f, 100.0f);
    XMMATRIX mvp = model * view * proj;

    // [The first MVP]
    commandList->SetGraphicsRoot32BitConstants(0, sizeof(DirectX::XMMATRIX) / 4, &mvp, 0);

	// [The second MVP]
	UINT8* pData;
	ThrowIfFailed(constantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pData)));
	memcpy(pData, &mvp, sizeof(XMMATRIX));
	constantBuffer->Unmap(0, nullptr);
	commandList->SetGraphicsRootConstantBufferView(1, constantBuffer->GetGPUVirtualAddress());

	// [The third MVP]
	ID3D12DescriptorHeap* heaps[] = { cbvHeap.Get() };
	commandList->SetDescriptorHeaps(1, heaps);
	D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = cbvHeap->GetGPUDescriptorHandleForHeapStart();
	commandList->SetGraphicsRootDescriptorTable(2, gpuHandle);

    // Draw
    commandList->DrawIndexedInstanced(_countof(cubeIndices), 1, 0, 0, 0);

    // Resource barrier for present
    CD3DX12_RESOURCE_BARRIER rtBarrierEnd = CD3DX12_RESOURCE_BARRIER::Transition(
        renderTarget[frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    commandList->ResourceBarrier(1, &rtBarrierEnd);

    // Close the command list before executing it.  This is the crucial change.
    ThrowIfFailed(commandList->Close());

    // Execute the command list.
    ID3D12CommandList* cmdLists[] = { commandList.Get() };
    commandQueue->ExecuteCommandLists(1, cmdLists);
    ThrowIfFailed(swapChain->Present(1, 0));

    // Fence and update frame index
    fenceValue++;
    ThrowIfFailed(commandQueue->Signal(fence.Get(), fenceValue));
    if (fence->GetCompletedValue() < fenceValue) {
        ThrowIfFailed(fence->SetEventOnCompletion(fenceValue, fenceEvent));
        WaitForSingleObject(fenceEvent, INFINITE);
    }
    frameIndex = swapChain->GetCurrentBackBufferIndex();
}

static void GetHardwareAdapter(
    IDXGIFactory1* pFactory,
    IDXGIAdapter1** ppAdapter,
    bool requestHighPerformanceAdapter) {
    *ppAdapter = nullptr;
    ComPtr<IDXGIAdapter1> adapter;

    ComPtr<IDXGIFactory6> factory6;
    if (SUCCEEDED(pFactory->QueryInterface(IID_PPV_ARGS(&factory6)))) {
        for (UINT adapterIndex = 0;
            SUCCEEDED(factory6->EnumAdapterByGpuPreference(
                adapterIndex,
                requestHighPerformanceAdapter ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE : DXGI_GPU_PREFERENCE_UNSPECIFIED,
                IID_PPV_ARGS(&adapter)));
                ++adapterIndex) {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;

            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr))) {
                break;
            }
        }
    }

    if (!adapter) {
        for (UINT adapterIndex = 0; SUCCEEDED(pFactory->EnumAdapters1(adapterIndex, &adapter)); ++adapterIndex) {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;

            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr))) {
                break;
            }
        }
    }

    *ppAdapter = adapter.Detach();
    if (!*ppAdapter) {
        throw std::runtime_error("No suitable Direct3D 12 adapter found.");
    }
}

int main() {
    std::cout << "Starting Direct3D 12 Cube Demo" << std::endl;
    HINSTANCE hInstance = GetModuleHandle(nullptr);
    InitWindow(hInstance);
    Initialize();
    LoadAssets();
    LoadShaderPipeline();

    // Fence and Event
    ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent) {
        ThrowIfFailed(GetLastError());
    }
    startTime = std::chrono::steady_clock::now();

    // Main loop
    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        //UpdateAndRender();
    }

    CloseHandle(fenceEvent);
    std::cout << "Exiting Direct3D 12 Cube Demo" << std::endl;
    return 0;
}
