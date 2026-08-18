#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <d3dx12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>

#include <vector>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")

using Microsoft::WRL::ComPtr;

HWND mainWnd{ nullptr };

int windowWidth{ 1280 };
int windowHeight{ 720 };
UINT32 videoWidth{ 0 };
UINT32 videoHeight{ 0 };
LPCWSTR videoPath = L"D:\\Dev_media\\video.mp4";
LPCWSTR windowTitle = L"DirectX12 Video Encode";
LPCWSTR windowClassName = L"DirectX12";
LPCWSTR videoVSPath = L"./Video_VS.hlsl";
LPCWSTR videoPSPath = L"./Video_PS.hlsl";

bool hasVideoFrame{ false };
bool decodeAsNv12{ false };
UINT32 nv12Stride{ 0 };

UINT rtvDescriptorSize{ 0 };
UINT srvDescriptorSize{ 0 };
const UINT swapChainBufferCount{ 2 };
UINT64 fenceValue{ 0 };
UINT64 frameFenceValue[swapChainBufferCount]{ 0 };
HANDLE frameFenceEvent{ nullptr };
const FLOAT clearColor[4]{ 0.0f,0.0f,0.0f,1.0f };
std::vector<BYTE> frameScratch{};

D3D12_VIEWPORT viewport{};
D3D12_RECT scissor{};
D3D12_RESOURCE_STATES videoTextureState = D3D12_RESOURCE_STATE_COPY_DEST;

ComPtr<IMFSourceReader> imfReader{};
ComPtr<IMFAttributes> imfReaderAttributes{};
ComPtr<IMFMediaType> imfOutType{};
ComPtr<IMFMediaType> imfCurType{};

ComPtr<IDXGIFactory> dxgiFactory{};
ComPtr<ID3D12Device> d3dDevice{};
ComPtr<ID3D12Fence> fence{};
ComPtr<ID3D12CommandQueue> commandQueue{};
ComPtr<ID3D12CommandAllocator> commandAllocator{};
ComPtr<ID3D12GraphicsCommandList> commandList{};
ComPtr<IDXGISwapChain3> swapChain{};
ComPtr<ID3D12DescriptorHeap> rtvHeap{};
ComPtr<ID3D12DescriptorHeap> srvHeap{};
ComPtr<ID3D12Resource> swapChainBuffer[swapChainBufferCount]{};
ComPtr<ID3D12Resource> videoTexture{};
ComPtr<ID3D12Resource> videoUploadBuffer{};
ComPtr<ID3D12PipelineState> videoPSO{};
ComPtr<ID3D12RootSignature> videoRootSignature{};
ComPtr<ID3DBlob> vsBlob{};
ComPtr<ID3DBlob> psBlob{};
ComPtr<ID3DBlob> signatureBlob{};

HRESULT GenWindow(HINSTANCE& hInstance, int cmdShow);
HRESULT InitCOM();
HRESULT InitMF();
HRESULT InitDX();
void UploadVideoFrameBuffer();
void Draw();

void WaitGPUSignal();
void WaitBackBuffer(UINT frameIndex);
BYTE Clamp8Bit(int v);
void Nv12ToBgra(const BYTE* yPlane, const BYTE* uvPlane, UINT width, UINT height, UINT yStride, BYTE* dst, UINT dstPitch);

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE prevInstance, LPWSTR cmdLine, int cmdShow)
{
	HRESULT hr{ 0 };
	hr = GenWindow(hInstance, cmdShow);
	if (hr != S_OK)
		return -1;

	hr = InitCOM();
	if (hr != S_OK)
		return -1;

	hr = InitMF();
	if (hr != S_OK)
		return -1;

	hr = InitDX();
	if (hr != S_OK)
		return -1;

	MSG msg{};
	ZeroMemory(&msg, sizeof(MSG));

	while (msg.message != WM_QUIT)
	{
		while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
		Draw();
	}

	WaitGPUSignal();
	CloseHandle(frameFenceEvent);
	MFShutdown();
	CoUninitialize();

	return 0;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

HRESULT GenWindow(HINSTANCE& hInstance, int cmdShow)
{
	HRESULT hr{ 0 };

	WNDCLASSEXW wndClass{};
	ZeroMemory(&wndClass, sizeof(WNDCLASSEXW));
	wndClass.cbSize = sizeof(WNDCLASSEXW);
	wndClass.style = CS_HREDRAW | CS_VREDRAW;
	wndClass.lpfnWndProc = WndProc;
	wndClass.cbClsExtra = 0;
	wndClass.cbWndExtra = 0;
	wndClass.hInstance = hInstance;
	wndClass.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
	wndClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wndClass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
	wndClass.lpszMenuName = nullptr;
	wndClass.lpszClassName = windowClassName;
	wndClass.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);

	//Failed when return value == 0
	hr = (HRESULT)RegisterClassExW(&wndClass);
	if (hr == 0)
	{
		MessageBox(nullptr, L"GenWindow::Register Window Class Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	//Failed when return value == nullptr
	mainWnd = CreateWindowExW(
		0,
		windowClassName, windowTitle,
		WS_OVERLAPPEDWINDOW,
		(GetSystemMetrics(SM_CXSCREEN) - windowWidth) / 2, (GetSystemMetrics(SM_CYSCREEN) - windowHeight) / 2,
		windowWidth, windowHeight,
		nullptr, nullptr,
		wndClass.hInstance,
		nullptr
	);
	if (mainWnd == nullptr)
	{
		MessageBox(nullptr, L"GenWindow::Create Window Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	ShowWindow(mainWnd, cmdShow);
	UpdateWindow(mainWnd);

	return S_OK;
}

HRESULT InitCOM()
{
	HRESULT hr{ 0 };

	hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
	{
		MessageBox(nullptr, L"InitCOM::Initialize COM Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	return S_OK;
}

HRESULT InitMF()
{
	HRESULT hr{ 0 };

	hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
	if (hr != S_OK)
	{
		MessageBox(nullptr, L"InitMF::Initialize Media Fundation Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	hr = MFCreateAttributes(imfReaderAttributes.GetAddressOf(), 1);
	if (hr != S_OK)
	{
		MessageBox(nullptr, L"InitMF::Create Media Fundation Attributes Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	imfReaderAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);

	hr = MFCreateSourceReaderFromURL(videoPath, imfReaderAttributes.Get(), imfReader.GetAddressOf());
	if (hr != S_OK)
	{
		MessageBox(nullptr, L"InitMF::Create Reader From URL Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	hr = MFCreateMediaType(imfOutType.GetAddressOf());
	if (hr != S_OK)
	{
		MessageBox(nullptr, L"InitMF::Create Media Type Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	imfOutType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	imfOutType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);

	hr = imfReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, imfOutType.Get());
	if (FAILED(hr))
	{
		imfOutType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
		hr = imfReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, imfOutType.Get());
		if (FAILED(hr))
		{
			MessageBox(nullptr, L"InitMF::Configure Media Format Failed.", L"Error", MB_OK);
			return E_FAIL;
		}
		decodeAsNv12 = true;
	}

	if (FAILED(imfReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, imfCurType.GetAddressOf())) ||
		FAILED(MFGetAttributeSize(imfCurType.Get(), MF_MT_FRAME_SIZE, &videoWidth, &videoHeight)) ||
		videoWidth == 0 || videoHeight == 0)
	{
		MessageBox(nullptr, L"InitMF::Video Size Get Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	if (decodeAsNv12)
	{
		nv12Stride = MFGetAttributeUINT32(imfCurType.Get(), MF_MT_DEFAULT_STRIDE, videoWidth);
		if (nv12Stride == 0)
			nv12Stride = videoWidth;
	}

	return S_OK;
}

HRESULT InitDX()
{
	HRESULT hr{ 0 };

	hr = CreateDXGIFactory1(IID_PPV_ARGS(dxgiFactory.GetAddressOf()));
	if (hr != S_OK)
	{
		MessageBox(nullptr, L"InitDX::Create DXGI Factory Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(d3dDevice.GetAddressOf()));
	if (hr != S_OK)
	{
		MessageBox(nullptr, L"InitDX::Create D3D Device Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	hr = d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf()));
	if (hr != S_OK)
	{
		MessageBox(nullptr, L"InitDX::Create D3D Fence Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	frameFenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	if (frameFenceEvent == 0)
	{
		MessageBox(nullptr, L"InitDX::Create Fence Event Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	D3D12_COMMAND_QUEUE_DESC queueDesc{};
	ZeroMemory(&queueDesc, sizeof(D3D12_COMMAND_QUEUE_DESC));
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.NodeMask = 0;

	hr = d3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(commandQueue.GetAddressOf()));
	if (hr != S_OK)
	{
		MessageBox(nullptr, L"InitDX::Create Command Queue Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	hr = d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(commandAllocator.GetAddressOf()));
	if (hr != S_OK)
	{
		MessageBox(nullptr, L"InitDX::Create Command Allocator Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	hr = d3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(commandList.GetAddressOf()));
	if (hr != S_OK)
	{
		MessageBox(nullptr, L"InitDX::Create Command List Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	commandList->Close();

	DXGI_SWAP_CHAIN_DESC swapChainDesc{};
	ZeroMemory(&swapChainDesc, sizeof(DXGI_SWAP_CHAIN_DESC));
	swapChainDesc.BufferDesc.Width = windowWidth;
	swapChainDesc.BufferDesc.Height = windowHeight;
	swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
	swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SampleDesc.Quality = 0;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = swapChainBufferCount;
	swapChainDesc.OutputWindow = mainWnd;
	swapChainDesc.Windowed = TRUE;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	ComPtr<IDXGISwapChain> swapChain1;
	hr = dxgiFactory->CreateSwapChain(commandQueue.Get(), &swapChainDesc, swapChain1.GetAddressOf());
	if (FAILED(hr))
	{
		MessageBox(nullptr, L"InitDX::DXGI Create Swap Chain Failed.", L"Error", MB_OK);
		return E_FAIL;
	}
	hr = swapChain1.As(&swapChain);
	if (FAILED(hr))
	{
		MessageBox(nullptr, L"InitDX::DXGI Create Swap Chain Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	hr = dxgiFactory->MakeWindowAssociation(mainWnd, DXGI_MWA_NO_ALT_ENTER);
	if (hr != S_OK)
	{
		MessageBox(nullptr, L"InitDX::DXGI Window Association Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
	ZeroMemory(&rtvHeapDesc, sizeof(D3D12_DESCRIPTOR_HEAP_DESC));
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.NumDescriptors = swapChainBufferCount;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;

	hr = d3dDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(rtvHeap.GetAddressOf()));
	if (hr != S_OK)
	{
		MessageBox(nullptr, L"InitDX::Create RTV Descriptor Heap Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	rtvDescriptorSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart());
	for (UINT i = 0; i < swapChainBufferCount; i++)
	{
		hr = swapChain->GetBuffer(i, IID_PPV_ARGS(swapChainBuffer[i].GetAddressOf()));
		if (hr != S_OK)
		{
			MessageBox(nullptr, L"InitDX::Get Swap Chain Buffer Failed.", L"Error", MB_OK);
			return E_FAIL;
		}

		d3dDevice->CreateRenderTargetView(swapChainBuffer[i].Get(), nullptr, rtvHandle);
		rtvHandle.Offset(1, rtvDescriptorSize);
	}

	CD3DX12_HEAP_PROPERTIES defaultHeapProperties(D3D12_HEAP_TYPE_DEFAULT);
	D3D12_RESOURCE_DESC defaultHeapDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_B8G8R8A8_UNORM, videoWidth, videoHeight, 1, 1
	);
	hr = d3dDevice->CreateCommittedResource(
		&defaultHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&defaultHeapDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(videoTexture.GetAddressOf())
	);
	if (hr != S_OK)
	{
		MessageBox(nullptr, L"InitDX::Create Default Buffer Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(videoTexture.Get(), 0, 1);
	CD3DX12_HEAP_PROPERTIES uploadHeapProperties(D3D12_HEAP_TYPE_UPLOAD);
	D3D12_RESOURCE_DESC uploadHeapDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
	hr = d3dDevice->CreateCommittedResource(
		&uploadHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&uploadHeapDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(videoUploadBuffer.GetAddressOf())
	);
	if (hr != S_OK)
	{
		MessageBox(nullptr, L"InitDX::Create Upload Buffer Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};
	ZeroMemory(&srvHeapDesc, sizeof(D3D12_DESCRIPTOR_HEAP_DESC));
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.NumDescriptors = 1;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	srvHeapDesc.NodeMask = 0;

	hr = d3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(srvHeap.GetAddressOf()));
	if (hr != S_OK)
	{
		MessageBox(nullptr, L"InitDX::Create SRV Descriptor Heap Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	srvDescriptorSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	ZeroMemory(&srvDesc, sizeof(D3D12_SHADER_RESOURCE_VIEW_DESC));
	srvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D.MipLevels = 1;

	d3dDevice->CreateShaderResourceView(videoTexture.Get(), &srvDesc, srvHeap->GetCPUDescriptorHandleForHeapStart());

	CD3DX12_DESCRIPTOR_RANGE1 srvRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
	CD3DX12_ROOT_PARAMETER1 rootParameter{};
	ZeroMemory(&rootParameter, sizeof(CD3DX12_ROOT_PARAMETER1));
	rootParameter.InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

	CD3DX12_STATIC_SAMPLER_DESC staticSampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
	staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc{};
	ZeroMemory(&rootSignatureDesc, sizeof(CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC));
	rootSignatureDesc.Init_1_1(1, &rootParameter, 1, &staticSampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	hr = D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, signatureBlob.GetAddressOf(), nullptr);
	if (hr != S_OK)
	{
		MessageBox(nullptr, L"InitDX::Serialize Root Signature Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	hr = d3dDevice->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(videoRootSignature.GetAddressOf()));
	if (hr != S_OK)
	{
		MessageBox(nullptr, L"InitDX::Create Root Signature Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	hr = D3DCompileFromFile(videoVSPath, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), nullptr);
	if (hr != S_OK)
	{
		MessageBox(nullptr, L"InitDX::Compile Video Vertex Shader Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	hr = D3DCompileFromFile(videoPSPath, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), nullptr);
	if (hr != S_OK)
	{
		MessageBox(nullptr, L"InitDX::Compile Video Pixel Shader Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
	ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	psoDesc.pRootSignature = videoRootSignature.Get();
	psoDesc.VS.pShaderBytecode = vsBlob->GetBufferPointer();
	psoDesc.VS.BytecodeLength = vsBlob->GetBufferSize();
	psoDesc.PS.pShaderBytecode = psBlob->GetBufferPointer();
	psoDesc.PS.BytecodeLength = psBlob->GetBufferSize();
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState.DepthEnable = FALSE;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.InputLayout.pInputElementDescs = nullptr;
	psoDesc.InputLayout.NumElements = 0;

	hr = d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(videoPSO.GetAddressOf()));
	if (hr != S_OK)
	{
		MessageBox(nullptr, L"InitDX::Create Pipeline State Object Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	ZeroMemory(&viewport, sizeof(D3D12_VIEWPORT));
	viewport.Width = static_cast<float>(windowWidth);
	viewport.Height = static_cast<float>(windowHeight);
	viewport.MaxDepth = 1.0f;

	ZeroMemory(&scissor, sizeof(D3D12_RECT));
	scissor.left = 0;
	scissor.top = 0;
	scissor.right = windowWidth;
	scissor.bottom = windowHeight;

	return S_OK;
}

BYTE Clamp8Bit(int v)
{
	if (v < 0) return 0;
	if (v > 255) return 255;
	return static_cast<BYTE>(v);
}

void Nv12ToBgra(const BYTE* yPlane, const BYTE* uvPlane, UINT width, UINT height, UINT yStride, BYTE* dst, UINT dstPitch)
{
	for (UINT y = 0; y < height; ++y)
	{
		const BYTE* yRow = yPlane + y * yStride;
		const BYTE* uvRow = uvPlane + (y / 2) * yStride;
		BYTE* dstRow = dst + y * dstPitch;
		for (UINT x = 0; x < width; ++x)
		{
			const int Y = yRow[x] - 16;
			const int U = uvRow[(x / 2) * 2] - 128;
			const int V = uvRow[(x / 2) * 2 + 1] - 128;
			dstRow[x * 4 + 0] = Clamp8Bit(((298 * Y + 516 * U + 128) >> 8));
			dstRow[x * 4 + 1] = Clamp8Bit(((298 * Y - 100 * U - 208 * V + 128) >> 8));
			dstRow[x * 4 + 2] = Clamp8Bit(((298 * Y + 409 * V + 128) >> 8));
			dstRow[x * 4 + 3] = 255;
		}
	}
}

void UploadVideoFrameBuffer()
{
	HRESULT hr{};

	ComPtr<IMFSample> sample{};
	ComPtr<IMFMediaBuffer> mediaBuffer{};

	for (int attempt = 0; attempt < 8; attempt++)
	{
		DWORD streamIndex{};
		DWORD flags{};
		LONGLONG timeStamp{};

		sample.Reset();
		mediaBuffer.Reset();

		hr = imfReader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &streamIndex, &flags, &timeStamp, sample.GetAddressOf());
		if (FAILED(hr))
			return;

		if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
		{
			PROPVARIANT var{};
			var.vt = VT_I8;
			var.hVal.QuadPart = 0;
			imfReader->SetCurrentPosition(GUID_NULL, var);
			return;
		}

		if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
			continue;

		if (sample)
			break;
	}

	if (!sample)
		return;

	if (FAILED(sample->ConvertToContiguousBuffer(mediaBuffer.GetAddressOf())))
		return;

	BYTE* scan0{ nullptr };
	DWORD maxLen{};
	DWORD curLen{};
	LONG pitch{};

	ComPtr<IMF2DBuffer> buffer2d{};
	const bool use2d = SUCCEEDED(mediaBuffer.As(&buffer2d)) && SUCCEEDED(buffer2d->Lock2D(&scan0, &pitch));

	if (!use2d)
	{
		if (FAILED(mediaBuffer->Lock(&scan0, &maxLen, &curLen)))
			return;
		pitch = decodeAsNv12 ? static_cast<LONG>(nv12Stride) : static_cast<LONG>(videoWidth * 4);
	}

	UINT rowPitch{};
	size_t copySize{};
	if (decodeAsNv12)
	{
		const BYTE* yPlane = scan0;
		const BYTE* uvPlane = scan0 + static_cast<size_t>(pitch) * videoHeight;
		rowPitch = videoWidth * 4;
		copySize = static_cast<size_t>(rowPitch) * videoHeight;
		frameScratch.resize(copySize);
		Nv12ToBgra(yPlane, uvPlane, videoWidth, videoHeight, static_cast<UINT>(pitch), frameScratch.data(), rowPitch);
	}
	else
	{
		rowPitch = static_cast<UINT>(pitch);
		copySize = static_cast<size_t>(rowPitch) * videoHeight;
		frameScratch.resize(copySize);
		for (UINT y = 0; y < videoHeight; ++y)
			memcpy(frameScratch.data() + static_cast<size_t>(y) * rowPitch, scan0 + static_cast<size_t>(y) * pitch, videoWidth * 4);
	}

	if (use2d)
		buffer2d->Unlock2D();
	else
		mediaBuffer->Unlock();

	if (videoTextureState != D3D12_RESOURCE_STATE_COPY_DEST)
	{
		const D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			videoTexture.Get(), videoTextureState, D3D12_RESOURCE_STATE_COPY_DEST);
		commandList->ResourceBarrier(1, &barrier);
		videoTextureState = D3D12_RESOURCE_STATE_COPY_DEST;
	}

	D3D12_SUBRESOURCE_DATA subData{};
	subData.pData = frameScratch.data();
	subData.RowPitch = rowPitch;
	subData.SlicePitch = copySize;

	if (UpdateSubresources(commandList.Get(), videoTexture.Get(), videoUploadBuffer.Get(), 0, 0, 1, &subData) == 0)
		return;

	hasVideoFrame = true;

	const D3D12_RESOURCE_BARRIER toSrv = CD3DX12_RESOURCE_BARRIER::Transition(
		videoTexture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	commandList->ResourceBarrier(1, &toSrv);
	videoTextureState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
}

void Draw()
{
	const UINT frameIndex = swapChain->GetCurrentBackBufferIndex();
	WaitBackBuffer(frameIndex);

	commandAllocator->Reset();
	commandList->Reset(commandAllocator.Get(), nullptr);

	const D3D12_RESOURCE_BARRIER toRT = CD3DX12_RESOURCE_BARRIER::Transition(
		swapChainBuffer[frameIndex].Get(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	commandList->ResourceBarrier(1, &toRT);

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart(), frameIndex, rtvDescriptorSize);
	commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissor);
	commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

	UploadVideoFrameBuffer();

	if (hasVideoFrame)
	{
		ID3D12DescriptorHeap* heaps[] = { srvHeap.Get() };
		commandList->SetDescriptorHeaps(1, heaps);
		commandList->SetGraphicsRootSignature(videoRootSignature.Get());
		commandList->SetPipelineState(videoPSO.Get());
		commandList->SetGraphicsRootDescriptorTable(0, srvHeap->GetGPUDescriptorHandleForHeapStart());
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		commandList->DrawInstanced(3, 1, 0, 0);
	}

	const auto toPresent = CD3DX12_RESOURCE_BARRIER::Transition(
		swapChainBuffer[frameIndex].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	commandList->ResourceBarrier(1, &toPresent);

	commandList->Close();

	ID3D12CommandList* lists[] = { commandList.Get() };
	commandQueue->ExecuteCommandLists(1, lists);

	fenceValue++;
	commandQueue->Signal(fence.Get(), fenceValue);
	frameFenceValue[frameIndex] = fenceValue;

	swapChain->Present(1, 0);
}

void WaitBackBuffer(UINT frameIndex)
{
	if (frameFenceValue[frameIndex] == 0)
		return;
	if (fence->GetCompletedValue() < frameFenceValue[frameIndex])
	{
		fence->SetEventOnCompletion(frameFenceValue[frameIndex], frameFenceEvent);
		WaitForSingleObject(frameFenceEvent, INFINITE);
	}
}

void WaitGPUSignal()
{
	fenceValue++;
	commandQueue->Signal(fence.Get(), fenceValue);
	if (fence->GetCompletedValue() < fenceValue)
	{
		fence->SetEventOnCompletion(fenceValue, frameFenceEvent);
		WaitForSingleObject(frameFenceEvent, INFINITE);
	}
}