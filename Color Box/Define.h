#pragma once
#include <d3dx12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <DirectXColors.h>
#include <vector>
#include <array>

#pragma comment(lib,"d3dcompiler.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace std;

XMFLOAT4X4 MatrixIdentity()
{
	XMFLOAT4X4 I(
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f);
	return I;
}

int windowWidth{ 800 };
int windowHeight{ 600 };
HWND mainWnd{};

UINT64 currentFence{};
const int swapChainBufferCount{ 2 };
int currentBackBuffer{};
UINT rtvDescriptorSize{};
UINT dsvDescriptorSize{};
UINT cbvSrvUavDescriptorSize{};
UINT msaa4xQualityLevel{};
UINT vbByteSize{};
UINT ibByteSize{};

DXGI_FORMAT backBufferFormat{ DXGI_FORMAT_R8G8B8A8_UNORM };
DXGI_FORMAT depthStencilFormat{ DXGI_FORMAT_D24_UNORM_S8_UINT };
DXGI_FORMAT indexFormat{ DXGI_FORMAT_R16_UINT };
CD3DX12_HEAP_PROPERTIES heapPropertiesDefault(D3D12_HEAP_TYPE_DEFAULT);
CD3DX12_HEAP_PROPERTIES heapPropertiesUpload(D3D12_HEAP_TYPE_UPLOAD);

BYTE* mappedData{};
vector<D3D12_INPUT_ELEMENT_DESC> inputLayout{};

ComPtr<IDXGIFactory4> dxgiFactory{};
ComPtr<IDXGISwapChain> swapChain{};
ComPtr<ID3D12Device> d3dDevice{};
ComPtr<ID3D12Fence> fence{};
ComPtr<ID3D12CommandQueue> commandQueue{};
ComPtr<ID3D12CommandAllocator> commandAllocator{};
ComPtr<ID3D12GraphicsCommandList> commandList{};
ComPtr<ID3D12RootSignature> rootSignature{};
ComPtr<ID3DBlob> vsByteCode{};
ComPtr<ID3DBlob> psByteCode{};
ComPtr<ID3D12Resource> vertexBufferGPU{};
ComPtr<ID3D12Resource> indexBufferGPU{};
ComPtr<ID3D12Resource> vertexBufferUpload{};
ComPtr<ID3D12Resource> indexBufferUpload{};
ComPtr<ID3D12Resource> constantBufferUpload{};
ComPtr<ID3D12Resource> swapChainBuffer[swapChainBufferCount]{};
ComPtr<ID3D12Resource> depthStencilBuffer{};
ComPtr<ID3D12DescriptorHeap> rtvHeap{};
ComPtr<ID3D12DescriptorHeap> dsvHeap{};
ComPtr<ID3D12DescriptorHeap> cbvHeap{};
ComPtr<ID3D12PipelineState> pso{};

struct Vertex
{
	XMFLOAT3 Pos;
	XMFLOAT4 Color;
};

const size_t vertexNum{ 8 };
const size_t indexNum{ 36 };

array<Vertex, vertexNum> vertices =
{
	Vertex({ XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT4(Colors::White) }),
	Vertex({ XMFLOAT3(-1.0f, +1.0f, -1.0f), XMFLOAT4(Colors::Black) }),
	Vertex({ XMFLOAT3(+1.0f, +1.0f, -1.0f), XMFLOAT4(Colors::Red) }),
	Vertex({ XMFLOAT3(+1.0f, -1.0f, -1.0f), XMFLOAT4(Colors::Green) }),
	Vertex({ XMFLOAT3(-1.0f, -1.0f, +1.0f), XMFLOAT4(Colors::Blue) }),
	Vertex({ XMFLOAT3(-1.0f, +1.0f, +1.0f), XMFLOAT4(Colors::Yellow) }),
	Vertex({ XMFLOAT3(+1.0f, +1.0f, +1.0f), XMFLOAT4(Colors::Cyan) }),
	Vertex({ XMFLOAT3(+1.0f, -1.0f, +1.0f), XMFLOAT4(Colors::Magenta) })
};

array<uint16_t, indexNum> indices =
{
	0, 1, 2,
	0, 2, 3,

	4, 6, 5,
	4, 7, 6,

	4, 5, 1,
	4, 1, 0,

	3, 2, 6,
	3, 6, 7,

	1, 5, 6,
	1, 6, 2,

	4, 0, 3,
	4, 3, 7
};

float theta{ XM_PIDIV4 };
float phi{ XM_PIDIV4 };
float radius{ 5.0f };

XMFLOAT4X4 modelMatrix{ MatrixIdentity() };
XMFLOAT4X4 modelViewProjMatrix{ MatrixIdentity() };
