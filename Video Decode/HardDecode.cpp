#include <d3dx12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

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
UINT32 videoCropX{ 0 };
UINT32 videoCropY{ 0 };
UINT32 videoDisplayWidth{ 0 };
UINT32 videoDisplayHeight{ 0 };
UINT32 nv12Stride{ 0 };
LPCWSTR videoPath = L"D:\\Dev_media\\video1.mp4";
LPCWSTR videoVSPath = L"./Video_VS.hlsl";
LPCWSTR videoPSPath = L"./Video_PS.hlsl";
LPCWSTR windowTitle = L"DirectX12 Hard Decode";
LPCWSTR windowClassName = L"DirectX12Hard";

bool hasVideoFrame{ false };
bool frameUploadPending{ false };

UINT32 videoFrameRateNum{ 30 };
UINT32 videoFrameRateDen{ 1 };
LONGLONG qpcFrequency{ 0 };
LONGLONG nextFrameQpc{ 0 };

UINT rtvDescriptorSize{ 0 };
const UINT swapChainBufferCount{ 2 };
UINT64 fenceValue{ 0 };
UINT64 frameFenceValue[swapChainBufferCount]{ 0 };
HANDLE frameFenceEvent{ nullptr };
const FLOAT clearColor[4]{ 0.0f, 0.0f, 0.0f, 1.0f };

D3D12_VIEWPORT viewport{};
D3D12_RECT scissor{};
D3D12_RESOURCE_STATES videoTextureState = D3D12_RESOURCE_STATE_COPY_DEST;
std::vector<BYTE> frameScratch{};

ComPtr<IMFSourceReader> imfReader{};
ComPtr<IMFAttributes> imfReaderAttributes{};
ComPtr<IMFMediaType> imfOutType{};
ComPtr<IMFMediaType> imfCurType{};

ComPtr<IDXGIFactory4> dxgiFactory{};
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
HRESULT InitD3D();
HRESULT InitMF();
HRESULT InitRender();
void ReadAndCopyVideoFrame();
bool UploadDecodedFrame();
void Draw();

void WaitGPUSignal();
void WaitBackBuffer(UINT frameIndex);
bool ShouldAdvanceVideoFrame();
void UpdateVideoGeometry(IMFMediaType* mediaType);
void Nv12ToBgraScale(
	const BYTE* yPlane, const BYTE* uvPlane,
	UINT srcWidth, UINT srcHeight, UINT srcStride,
	BYTE* dst, UINT dstWidth, UINT dstHeight, UINT dstPitch);

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int cmdShow)
{
	if (FAILED(GenWindow(hInstance, cmdShow)))
		return -1;
	if (FAILED(InitCOM()))
		return -1;
	if (FAILED(InitD3D()))
		return -1;
	if (FAILED(InitMF()))
		return -1;
	if (FAILED(InitRender()))
		return -1;

	MSG msg{};
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
	if (msg == WM_DESTROY)
	{
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

HRESULT GenWindow(HINSTANCE& hInstance, int cmdShow)
{
	WNDCLASSEXW wndClass{ sizeof(WNDCLASSEXW) };
	wndClass.style = CS_HREDRAW | CS_VREDRAW;
	wndClass.lpfnWndProc = WndProc;
	wndClass.hInstance = hInstance;
	wndClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wndClass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	wndClass.lpszClassName = windowClassName;
	if (!RegisterClassExW(&wndClass))
	{
		MessageBox(nullptr, L"GenWindow::Register Window Class Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	mainWnd = CreateWindowExW(
		0, windowClassName, windowTitle, WS_OVERLAPPEDWINDOW,
		(GetSystemMetrics(SM_CXSCREEN) - windowWidth) / 2,
		(GetSystemMetrics(SM_CYSCREEN) - windowHeight) / 2,
		windowWidth, windowHeight,
		nullptr, nullptr, hInstance, nullptr);
	if (!mainWnd)
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
	const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
	{
		MessageBox(nullptr, L"InitCOM::Initialize COM Failed.", L"Error", MB_OK);
		return E_FAIL;
	}
	return S_OK;
}

HRESULT InitD3D()
{
	if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(dxgiFactory.GetAddressOf()))))
	{
		MessageBox(nullptr, L"InitD3D::Create DXGI Factory Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	ComPtr<IDXGIAdapter1> adapter;
	if (FAILED(dxgiFactory->EnumAdapters1(0, adapter.GetAddressOf())))
	{
		MessageBox(nullptr, L"InitD3D::Enum Adapters Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(d3dDevice.GetAddressOf()))))
	{
		MessageBox(nullptr, L"InitD3D::Create D3D12 Device Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	if (FAILED(d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf()))))
	{
		MessageBox(nullptr, L"InitD3D::Create Fence Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	frameFenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	if (!frameFenceEvent)
	{
		MessageBox(nullptr, L"InitD3D::Create Fence Event Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	D3D12_COMMAND_QUEUE_DESC queueDesc{};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	if (FAILED(d3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(commandQueue.GetAddressOf()))))
	{
		MessageBox(nullptr, L"InitD3D::Create Command Queue Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	if (FAILED(d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(commandAllocator.GetAddressOf()))))
	{
		MessageBox(nullptr, L"InitD3D::Create Command Allocator Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	if (FAILED(d3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
		commandAllocator.Get(), nullptr, IID_PPV_ARGS(commandList.GetAddressOf()))))
	{
		MessageBox(nullptr, L"InitD3D::Create Command List Failed.", L"Error", MB_OK);
		return E_FAIL;
	}
	commandList->Close();
	return S_OK;
}

HRESULT InitMF()
{
	if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE)))
	{
		MessageBox(nullptr, L"InitMF::Initialize Media Foundation Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	if (FAILED(MFCreateAttributes(imfReaderAttributes.GetAddressOf(), 1)))
	{
		MessageBox(nullptr, L"InitMF::Create Attributes Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	imfReaderAttributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);

	if (FAILED(MFCreateSourceReaderFromURL(videoPath, imfReaderAttributes.Get(), imfReader.GetAddressOf())))
	{
		MessageBox(nullptr, L"InitMF::Create Reader Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	if (FAILED(MFCreateMediaType(imfOutType.GetAddressOf())))
	{
		MessageBox(nullptr, L"InitMF::Create Media Type Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	imfOutType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	imfOutType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
	if (FAILED(imfReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, imfOutType.Get())))
	{
		MessageBox(nullptr, L"InitMF::Set NV12 Media Type Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	if (FAILED(imfReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, imfCurType.GetAddressOf())))
	{
		MessageBox(nullptr, L"InitMF::Get Video Size Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	UpdateVideoGeometry(imfCurType.Get());

	if (videoWidth > 8192 || videoHeight > 8192)
	{
		MessageBox(nullptr, L"InitMF::Invalid Video Size.", L"Error", MB_OK);
		return E_FAIL;
	}

	if (FAILED(MFGetAttributeRatio(imfCurType.Get(), MF_MT_FRAME_RATE, &videoFrameRateNum, &videoFrameRateDen)) ||
		videoFrameRateNum == 0)
	{
		videoFrameRateNum = 30;
		videoFrameRateDen = 1;
	}

	LARGE_INTEGER freq{};
	QueryPerformanceFrequency(&freq);
	qpcFrequency = freq.QuadPart;
	LARGE_INTEGER now{};
	QueryPerformanceCounter(&now);
	nextFrameQpc = now.QuadPart;

	return S_OK;
}

HRESULT InitRender()
{
	DXGI_SWAP_CHAIN_DESC swapChainDesc{};
	swapChainDesc.BufferDesc.Width = windowWidth;
	swapChainDesc.BufferDesc.Height = windowHeight;
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	swapChainDesc.BufferDesc.RefreshRate = { 60, 1 };
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SampleDesc.Quality = 0;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = swapChainBufferCount;
	swapChainDesc.OutputWindow = mainWnd;
	swapChainDesc.Windowed = TRUE;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	ComPtr<IDXGISwapChain> swapChain1;
	if (FAILED(dxgiFactory->CreateSwapChain(commandQueue.Get(), &swapChainDesc, swapChain1.GetAddressOf())))
	{
		MessageBox(nullptr, L"InitRender::Create Swap Chain Failed.", L"Error", MB_OK);
		return E_FAIL;
	}
	if (FAILED(swapChain1.As(&swapChain)))
	{
		MessageBox(nullptr, L"InitRender::Query Swap Chain3 Failed.", L"Error", MB_OK);
		return E_FAIL;
	}
	dxgiFactory->MakeWindowAssociation(mainWnd, DXGI_MWA_NO_ALT_ENTER);

	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
	rtvHeapDesc.NumDescriptors = swapChainBufferCount;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	if (FAILED(d3dDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(rtvHeap.GetAddressOf()))))
	{
		MessageBox(nullptr, L"InitRender::Create RTV Heap Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	rtvDescriptorSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart());
	for (UINT i = 0; i < swapChainBufferCount; ++i)
	{
		if (FAILED(swapChain->GetBuffer(i, IID_PPV_ARGS(swapChainBuffer[i].GetAddressOf()))))
		{
			MessageBox(nullptr, L"InitRender::Get Swap Chain Buffer Failed.", L"Error", MB_OK);
			return E_FAIL;
		}
		d3dDevice->CreateRenderTargetView(swapChainBuffer[i].Get(), nullptr, rtvHandle);
		rtvHandle.Offset(1, rtvDescriptorSize);
	}

	const CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
	const auto videoDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_B8G8R8A8_UNORM, windowWidth, windowHeight, 1, 1);
	if (FAILED(d3dDevice->CreateCommittedResource(
		&defaultHeap, D3D12_HEAP_FLAG_NONE, &videoDesc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
		IID_PPV_ARGS(videoTexture.GetAddressOf()))))
	{
		MessageBox(nullptr, L"InitRender::Create Video Texture Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(videoTexture.Get(), 0, 1);
	const CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
	const auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
	if (FAILED(d3dDevice->CreateCommittedResource(
		&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
		IID_PPV_ARGS(videoUploadBuffer.GetAddressOf()))))
	{
		MessageBox(nullptr, L"InitRender::Create Upload Buffer Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	frameScratch.resize(static_cast<size_t>(windowWidth) * windowHeight * 4);

	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};
	srvHeapDesc.NumDescriptors = 1;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	if (FAILED(d3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(srvHeap.GetAddressOf()))))
	{
		MessageBox(nullptr, L"InitRender::Create SRV Heap Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D.MipLevels = 1;
	d3dDevice->CreateShaderResourceView(videoTexture.Get(), &srvDesc, srvHeap->GetCPUDescriptorHandleForHeapStart());

	CD3DX12_DESCRIPTOR_RANGE1 srvRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
	CD3DX12_ROOT_PARAMETER1 rootParameter{};
	rootParameter.InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);
	CD3DX12_STATIC_SAMPLER_DESC staticSampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
	staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc{};
	rootSignatureDesc.Init_1_1(1, &rootParameter, 1, &staticSampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
	if (FAILED(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, signatureBlob.GetAddressOf(), nullptr)) ||
		FAILED(d3dDevice->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(videoRootSignature.GetAddressOf()))))
	{
		MessageBox(nullptr, L"InitRender::Create Root Signature Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	if (FAILED(D3DCompileFromFile(videoVSPath, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), nullptr)) ||
		FAILED(D3DCompileFromFile(videoPSPath, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), nullptr)))
	{
		MessageBox(nullptr, L"InitRender::Compile Shader Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
	psoDesc.pRootSignature = videoRootSignature.Get();
	psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
	psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState.DepthEnable = FALSE;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.InputLayout = { nullptr, 0 };
	if (FAILED(d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(videoPSO.GetAddressOf()))))
	{
		MessageBox(nullptr, L"InitRender::Create PSO Failed.", L"Error", MB_OK);
		return E_FAIL;
	}

	viewport.Width = static_cast<float>(windowWidth);
	viewport.Height = static_cast<float>(windowHeight);
	viewport.MaxDepth = 1.0f;
	scissor = { 0, 0, windowWidth, windowHeight };
	return S_OK;
}

void UpdateVideoGeometry(IMFMediaType* mediaType)
{
	if (!mediaType)
		return;

	if (FAILED(MFGetAttributeSize(mediaType, MF_MT_FRAME_SIZE, &videoWidth, &videoHeight)) ||
		videoWidth == 0 || videoHeight == 0)
		return;

	videoCropX = 0;
	videoCropY = 0;
	videoDisplayWidth = videoWidth;
	videoDisplayHeight = videoHeight;

	MFVideoArea aperture{};
	UINT32 blobSize = sizeof(aperture);
	if (SUCCEEDED(mediaType->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, (UINT8*)&aperture, sizeof(aperture), &blobSize)) ||
		SUCCEEDED(mediaType->GetBlob(MF_MT_GEOMETRIC_APERTURE, (UINT8*)&aperture, sizeof(aperture), &blobSize)))
	{
		if (aperture.Area.cx > 0 && aperture.Area.cy > 0)
		{
			videoCropX = aperture.OffsetX.value;
			videoCropY = aperture.OffsetY.value;
			videoDisplayWidth = aperture.Area.cx;
			videoDisplayHeight = aperture.Area.cy;
		}
	}

	videoCropX &= ~1u;

	nv12Stride = MFGetAttributeUINT32(mediaType, MF_MT_DEFAULT_STRIDE, videoWidth);
	if (nv12Stride == 0)
		nv12Stride = videoWidth;
}

bool ShouldAdvanceVideoFrame()
{
	LARGE_INTEGER now{};
	QueryPerformanceCounter(&now);
	if (now.QuadPart < nextFrameQpc)
		return false;

	const LONGLONG frameTicks = (qpcFrequency * static_cast<LONGLONG>(videoFrameRateDen)) /
		static_cast<LONGLONG>(videoFrameRateNum);
	nextFrameQpc += frameTicks;
	if (now.QuadPart - nextFrameQpc > frameTicks * 2)
		nextFrameQpc = now.QuadPart + frameTicks;
	return true;
}

static BYTE Clamp8Bit(int v)
{
	if (v < 0) return 0;
	if (v > 255) return 255;
	return static_cast<BYTE>(v);
}

void Nv12ToBgraScale(
	const BYTE* yPlane, const BYTE* uvPlane,
	UINT srcWidth, UINT srcHeight, UINT srcStride,
	BYTE* dst, UINT dstWidth, UINT dstHeight, UINT dstPitch)
{
	for (UINT dy = 0; dy < dstHeight; ++dy)
	{
		const UINT sy = (dy * srcHeight) / dstHeight;
		const BYTE* yRow = yPlane + sy * srcStride;
		const BYTE* uvRow = uvPlane + (sy / 2) * srcStride;
		BYTE* dstRow = dst + dy * dstPitch;
		for (UINT dx = 0; dx < dstWidth; ++dx)
		{
			const UINT sx = (dx * srcWidth) / dstWidth;
			const int Y = yRow[sx] - 16;
			const int U = uvRow[(sx / 2) * 2] - 128;
			const int V = uvRow[(sx / 2) * 2 + 1] - 128;
			dstRow[dx * 4 + 0] = Clamp8Bit(((298 * Y + 516 * U + 128) >> 8));
			dstRow[dx * 4 + 1] = Clamp8Bit(((298 * Y - 100 * U - 208 * V + 128) >> 8));
			dstRow[dx * 4 + 2] = Clamp8Bit(((298 * Y + 409 * V + 128) >> 8));
			dstRow[dx * 4 + 3] = 255;
		}
	}
}

void ReadAndCopyVideoFrame()
{
	ComPtr<IMFSample> sample;
	ComPtr<IMFMediaBuffer> mediaBuffer;

	for (int attempt = 0; attempt < 8; ++attempt)
	{
		DWORD streamIndex{};
		DWORD flags{};
		LONGLONG timeStamp{};
		sample.Reset();
		mediaBuffer.Reset();

		if (FAILED(imfReader->ReadSample(
			MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
			&streamIndex, &flags, &timeStamp, sample.GetAddressOf())))
			return;

		if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
		{
			PROPVARIANT var{};
			var.vt = VT_I8;
			var.hVal.QuadPart = 0;
			imfReader->SetCurrentPosition(GUID_NULL, var);
			LARGE_INTEGER now{};
			QueryPerformanceCounter(&now);
			nextFrameQpc = now.QuadPart;
			return;
		}

		if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
		{
			imfCurType.Reset();
			if (SUCCEEDED(imfReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, imfCurType.GetAddressOf())))
				UpdateVideoGeometry(imfCurType.Get());
			continue;
		}

		if (sample)
			break;
	}

	if (!sample)
		return;

	if (FAILED(sample->ConvertToContiguousBuffer(mediaBuffer.GetAddressOf())))
		return;

	BYTE* scan0 = nullptr;
	DWORD maxLen = 0;
	DWORD curLen = 0;
	LONG pitch = 0;

	ComPtr<IMF2DBuffer> buffer2d;
	const bool use2d = SUCCEEDED(mediaBuffer.As(&buffer2d)) && SUCCEEDED(buffer2d->Lock2D(&scan0, &pitch));
	if (!use2d)
	{
		if (FAILED(mediaBuffer->Lock(&scan0, &maxLen, &curLen)))
			return;
		pitch = static_cast<LONG>(nv12Stride);
	}

	const LONG absPitch = pitch >= 0 ? pitch : -pitch;
	const BYTE* planeBase = scan0;
	if (pitch < 0)
		planeBase = scan0 + pitch * static_cast<LONG>(videoHeight - 1);

	const UINT32 ySurfaceHeight = (videoHeight + 1) & ~1u;
	const BYTE* yPlane = planeBase + static_cast<size_t>(videoCropY) * absPitch + videoCropX;
	const BYTE* uvPlane = planeBase + static_cast<size_t>(absPitch) * ySurfaceHeight + videoCropX;
	const UINT dstPitch = static_cast<UINT>(windowWidth) * 4;

	Nv12ToBgraScale(
		yPlane, uvPlane, videoDisplayWidth, videoDisplayHeight, absPitch,
		frameScratch.data(), windowWidth, windowHeight, dstPitch);

	if (use2d)
		buffer2d->Unlock2D();
	else
		mediaBuffer->Unlock();

	frameUploadPending = true;
}

bool UploadDecodedFrame()
{
	if (!frameUploadPending)
		return hasVideoFrame;

	if (videoTextureState != D3D12_RESOURCE_STATE_COPY_DEST)
	{
		const auto toCopy = CD3DX12_RESOURCE_BARRIER::Transition(
			videoTexture.Get(), videoTextureState, D3D12_RESOURCE_STATE_COPY_DEST);
		commandList->ResourceBarrier(1, &toCopy);
		videoTextureState = D3D12_RESOURCE_STATE_COPY_DEST;
	}

	D3D12_SUBRESOURCE_DATA subData{};
	subData.pData = frameScratch.data();
	subData.RowPitch = static_cast<LONG>(windowWidth * 4);
	subData.SlicePitch = subData.RowPitch * windowHeight;
	if (UpdateSubresources(commandList.Get(), videoTexture.Get(), videoUploadBuffer.Get(), 0, 0, 1, &subData) == 0)
		return hasVideoFrame;

	const auto toSrv = CD3DX12_RESOURCE_BARRIER::Transition(
		videoTexture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	commandList->ResourceBarrier(1, &toSrv);
	videoTextureState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	frameUploadPending = false;
	hasVideoFrame = true;
	return true;
}

void Draw()
{
	if (!ShouldAdvanceVideoFrame())
	{
		Sleep(1);
		return;
	}

	ReadAndCopyVideoFrame();

	const UINT frameIndex = swapChain->GetCurrentBackBufferIndex();
	WaitBackBuffer(frameIndex);

	commandAllocator->Reset();
	commandList->Reset(commandAllocator.Get(), nullptr);

	const auto toRT = CD3DX12_RESOURCE_BARRIER::Transition(
		swapChainBuffer[frameIndex].Get(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	commandList->ResourceBarrier(1, &toRT);

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
		rtvHeap->GetCPUDescriptorHandleForHeapStart(), frameIndex, rtvDescriptorSize);
	commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissor);
	commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

	UploadDecodedFrame();

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
