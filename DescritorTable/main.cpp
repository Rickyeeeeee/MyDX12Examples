#include <windows.h>
#include <iostream>
#include <wrl.h>
#include <d3d12.h>
#include "include/d3dx12/d3dx12.h"
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <chrono>
#include <vector>
#include <stdexcept>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"


using namespace Microsoft::WRL;
using namespace DirectX;

// Link libraries
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")


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
    XMFLOAT2 texCoord;
};

// Cube data
Vertex cubeVertices[] = {
    // +X
    {{+1, -1, -1}, {0, 1}}, // 0
    {{+1, +1, -1}, {0, 0}}, // 1
    {{+1, +1, +1}, {1, 0}}, // 2
    {{+1, -1, +1}, {1, 1}}, // 3

    // -X
    {{-1, -1, +1}, {0, 1}}, // 4
    {{-1, +1, +1}, {0, 0}}, // 5
    {{-1, +1, -1}, {1, 0}}, // 6
    {{-1, -1, -1}, {1, 1}}, // 7

    // +Y
    {{-1, +1, -1}, {0, 1}}, // 8
    {{-1, +1, +1}, {0, 0}}, // 9
    {{+1, +1, +1}, {1, 0}}, //10
    {{+1, +1, -1}, {1, 1}}, //11

    // -Y
    {{-1, -1, +1}, {0, 1}}, //12
    {{-1, -1, -1}, {0, 0}}, //13
    {{+1, -1, -1}, {1, 0}}, //14
    {{+1, -1, +1}, {1, 1}}, //15

    // +Z
    {{+1, -1, +1}, {0, 1}}, //16
    {{+1, +1, +1}, {0, 0}}, //17
    {{-1, +1, +1}, {1, 0}}, //18
    {{-1, -1, +1}, {1, 1}}, //19

    // -Z
    {{-1, -1, -1}, {0, 1}}, //20
    {{-1, +1, -1}, {0, 0}}, //21
    {{+1, +1, -1}, {1, 0}}, //22
    {{+1, -1, -1}, {1, 1}}, //23
};

uint16_t cubeIndices[] = {
    // +X
    0, 1, 2,
    0, 2, 3,

    // -X
    4, 5, 6,
    4, 6, 7,

    // +Y
    8, 9,10,
    8,10,11,

    // -Y
   12,13,14,
   12,14,15,

   // +Z
  16,17,18,
  16,18,19,

  // -Z
 20,21,22,
 20,22,23,
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
ComPtr<ID3D12DescriptorHeap> shaderVisibleHeap;
ComPtr<ID3D12Resource> constantBuffer;
D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
ComPtr<ID3D12Resource> texture;
D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

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
		cbvHeapDesc.NumDescriptors = 100;
		cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		ThrowIfFailed(device->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&shaderVisibleHeap)));

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
		device->CreateConstantBufferView(&cbvDesc, shaderVisibleHeap->GetCPUDescriptorHandleForHeapStart());

        // Load texture
		int width, height, channels;
		unsigned char* imageData = stbi_load("block.png", &width, &height, &channels, 4);
		if (!imageData) {
			throw std::runtime_error("Failed to load texture image");
		}

		// Create texture resource
		D3D12_RESOURCE_DESC textureDesc = {};
		textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		textureDesc.Width = width;
		textureDesc.Height = height;
		textureDesc.DepthOrArraySize = 1;
		textureDesc.MipLevels = 1;
		textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		textureDesc.SampleDesc.Count = 1;
		textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		auto heapPropsTexture = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		ThrowIfFailed(device->CreateCommittedResource(
			&heapPropsTexture, D3D12_HEAP_FLAG_NONE,
			&textureDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture)));

        const UINT64 uploadBufferSize = GetRequiredIntermediateSize(texture.Get(), 0, 1);

		ComPtr<ID3D12Resource> textureUploadHeap;

        // Create the GPU upload buffer.
        auto textureUploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto textureUploadResourceDescription = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
        ThrowIfFailed(device->CreateCommittedResource(
            &textureUploadHeapProperties,
            D3D12_HEAP_FLAG_NONE,
            &textureUploadResourceDescription,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&textureUploadHeap)));

		// Copy image data to the texture
		D3D12_SUBRESOURCE_DATA subresourceData = {};
		subresourceData.pData = imageData;
		subresourceData.RowPitch = width * 4; // 4 bytes per pixel
		subresourceData.SlicePitch = subresourceData.RowPitch * height;

		// Create a command list for copying the data
		ComPtr<ID3D12GraphicsCommandList> copyCommandList;
		ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&copyCommandList)));
		UpdateSubresources(copyCommandList.Get(), texture.Get(), textureUploadHeap.Get(), 0, 0, 1, &subresourceData);
        auto textureResourceBarrier = CD3DX12_RESOURCE_BARRIER::Transition(texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        copyCommandList->ResourceBarrier(1, &textureResourceBarrier);
		ThrowIfFailed(copyCommandList->Close());
		ID3D12CommandList* ppCommandLists[] = { copyCommandList.Get() };
		commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
		// wait for the command queue to finish
		ComPtr<ID3D12Fence> fence;
		HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		uint32_t copyFenceValue = 0;
		ThrowIfFailed(device->CreateFence(copyFenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
		commandQueue->Signal(fence.Get(), copyFenceValue);
        if (fence->GetCompletedValue() < copyFenceValue) {
            ThrowIfFailed(fence->SetEventOnCompletion(copyFenceValue, fenceEvent));
            WaitForSingleObject(fenceEvent, INFINITE);
        }
		CloseHandle(fenceEvent);
		stbi_image_free(imageData); // Free the image data after copying

		// Create the shader resource view for the texture
		srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
		auto srvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(shaderVisibleHeap->GetCPUDescriptorHandleForHeapStart(), 1, device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
		device->CreateShaderResourceView(texture.Get(), &srvDesc, srvHandle);
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
    D3D12_ROOT_PARAMETER rootParams[1] = {};

    // MVP
    D3D12_DESCRIPTOR_RANGE range[2] = {};
    range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    range[0].NumDescriptors = 1;
    range[0].BaseShaderRegister = 0;
    range[0].RegisterSpace = 0;
    range[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    range[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range[1].NumDescriptors = 1;
    range[1].BaseShaderRegister = 0;
    range[1].RegisterSpace = 0;
    range[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rootParams[0].DescriptorTable.NumDescriptorRanges = 2;
	rootParams[0].DescriptorTable.pDescriptorRanges = range;

    D3D12_STATIC_SAMPLER_DESC staticSampler = {};
    staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSampler.MipLODBias = 0;
    staticSampler.MaxAnisotropy = 1;
    staticSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    staticSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    staticSampler.MinLOD = 0.0f;
    staticSampler.MaxLOD = D3D12_FLOAT32_MAX;
    staticSampler.ShaderRegister = 0;        // register(s0)
    staticSampler.RegisterSpace = 0;
    staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;


    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 1;
    rootSigDesc.pParameters = rootParams;
    rootSigDesc.NumStaticSamplers = 1;
	rootSigDesc.pStaticSamplers = &staticSampler;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> sigBlob;
    ThrowIfFailed(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, nullptr));
    ThrowIfFailed(device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature)));

    // Input layout
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
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
    //commandList->SetGraphicsRoot32BitConstants(0, sizeof(DirectX::XMMATRIX) / 4, &mvp, 0);

	// [The second MVP]
	 UINT8* pData;
	 ThrowIfFailed(constantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pData)));
	 memcpy(pData, &mvp, sizeof(XMMATRIX));
	 constantBuffer->Unmap(0, nullptr);
	// commandList->SetGraphicsRootConstantBufferView(1, constantBuffer->GetGPUVirtualAddress());

	// [The third MVP]
	ID3D12DescriptorHeap* heaps[] = { shaderVisibleHeap.Get() };
	commandList->SetDescriptorHeaps(1, heaps);
	D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = shaderVisibleHeap->GetGPUDescriptorHandleForHeapStart();
	commandList->SetGraphicsRootDescriptorTable(0, gpuHandle);

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
