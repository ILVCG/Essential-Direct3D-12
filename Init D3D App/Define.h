#pragma once
#include <dxgi1_6.h>
#include <d3dx12.h>
#include <assert.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

HWND mainWnd{};

int windowWidth{ 1280 };
int windowHeight{ 720 };

UINT rtvDescriptorSize{ 0 };
UINT msaa4xQuality{ 0 };
const UINT swapChainBufferCount{ 2 };
UINT64 currentFence{ 0 };
int currentBackBuffer{ 0 };

FLOAT clearColor[4]{ 0.2f,0.3f,0.2f,1.0f };

Microsoft::WRL::ComPtr<IDXGIFactory4> dxgiFactory{};
Microsoft::WRL::ComPtr<ID3D12Device> d3dDevice{};
Microsoft::WRL::ComPtr<ID3D12Fence> fence{};
Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue{};
Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator{};
Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList{};
Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain{};
Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap{};
Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap{};
Microsoft::WRL::ComPtr<ID3D12Resource> swapChainBuffer[swapChainBufferCount]{};
Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilBuffer{};
