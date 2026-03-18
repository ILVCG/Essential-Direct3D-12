#include "Define.h"

void FlushCommandQueue();

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE prevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
	WNDCLASSEXW wc{};
	wc.cbSize = sizeof(WNDCLASSEXW);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = WndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
	wc.lpszMenuName = nullptr;
	wc.lpszClassName = L"DirectX";
	wc.hIconSm = LoadIconW(NULL, IDI_APPLICATION);
	assert(RegisterClassExW(&wc) && "Register Class Failed.");

	int posX{ (GetSystemMetrics(SM_CXSCREEN) - windowWidth) / 2 };
	int posY{ (GetSystemMetrics(SM_CYSCREEN) - windowHeight) / 2 };

	mainWnd = CreateWindowExW(0, wc.lpszClassName, L"Direct3D", WS_OVERLAPPEDWINDOW,
		posX, posY, windowWidth, windowHeight, NULL, NULL, wc.hInstance, NULL);
	assert((mainWnd != NULL) && "Create Window Failed.");

	ShowWindow(mainWnd, SW_SHOW);
	UpdateWindow(mainWnd);

	CreateDXGIFactory1(IID_PPV_ARGS(dxgiFactory.GetAddressOf()));

	HRESULT hardwareResult{ D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(d3dDevice.GetAddressOf())) };

	if (hardwareResult != S_OK)
	{
		Microsoft::WRL::ComPtr<IDXGIAdapter> pWarpAdapter{};
		dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(pWarpAdapter.GetAddressOf()));
		D3D12CreateDevice(pWarpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(d3dDevice.GetAddressOf()));
	}

	d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf()));

	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS qualityLevels{};
	qualityLevels.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	qualityLevels.SampleCount = 4;
	qualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	qualityLevels.NumQualityLevels = 0;
	d3dDevice->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &qualityLevels, sizeof(qualityLevels));

	msaa4xQuality = qualityLevels.NumQualityLevels;
	assert((msaa4xQuality > 0) && "Unexpected MSAA quality level.");

	D3D12_COMMAND_QUEUE_DESC queueDesc{};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	d3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(commandQueue.GetAddressOf()));
	d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(commandAllocator.GetAddressOf()));
	d3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, 
		commandAllocator.Get(), nullptr, IID_PPV_ARGS(commandList.GetAddressOf()));
	commandList->Close();

	DXGI_SWAP_CHAIN_DESC sd{};
	sd.BufferDesc.Width = windowWidth;
	sd.BufferDesc.Height = windowHeight;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	sd.SampleDesc.Count = msaa4xQuality;
	sd.SampleDesc.Quality = msaa4xQuality - 1;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = swapChainBufferCount;
	sd.OutputWindow = mainWnd;
	sd.Windowed = true;
	sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	dxgiFactory->CreateSwapChain(commandQueue.Get(), &sd, swapChain.GetAddressOf());

	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
	rtvHeapDesc.NumDescriptors = swapChainBufferCount;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;
	d3dDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(rtvHeap.GetAddressOf()));

	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;
	d3dDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(dsvHeap.GetAddressOf()));

	rtvDescriptorSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle{ rtvHeap->GetCPUDescriptorHandleForHeapStart() };
	for (UINT i{0}; i < swapChainBufferCount; i++)
	{
		swapChain->GetBuffer(i, IID_PPV_ARGS(swapChainBuffer[i].GetAddressOf()));
		d3dDevice->CreateRenderTargetView(swapChainBuffer[i].Get(), nullptr, rtvHeapHandle);
		rtvHeapHandle.Offset(1, rtvDescriptorSize);
	}

	D3D12_RESOURCE_DESC depthStencilDesc{};
	depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthStencilDesc.Alignment = 0;
	depthStencilDesc.Width = windowWidth;
	depthStencilDesc.Height = windowHeight;
	depthStencilDesc.DepthOrArraySize = 1;
	depthStencilDesc.MipLevels = 1;
	depthStencilDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	depthStencilDesc.SampleDesc.Count = msaa4xQuality;
	depthStencilDesc.SampleDesc.Quality = msaa4xQuality - 1;
	depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE optClear{};
	optClear.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	optClear.DepthStencil.Depth = 1.0f;
	optClear.DepthStencil.Stencil = 0;

	CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_DEFAULT);
	d3dDevice->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &depthStencilDesc,
		D3D12_RESOURCE_STATE_COMMON, &optClear, IID_PPV_ARGS(depthStencilBuffer.GetAddressOf()));

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.Texture2D.MipSlice = 0;
	d3dDevice->CreateDepthStencilView(depthStencilBuffer.Get(), &dsvDesc, dsvHeap->GetCPUDescriptorHandleForHeapStart());

	D3D12_VIEWPORT screenViewport{};
	screenViewport.TopLeftX = 0;
	screenViewport.TopLeftY = 0;
	screenViewport.Width = static_cast<float>(windowWidth);
	screenViewport.Height = static_cast<float>(windowHeight);
	screenViewport.MinDepth = 0.0f;
	screenViewport.MaxDepth = 1.0f;

	D3D12_RECT scissorRect{};
	scissorRect.left = 0;
	scissorRect.top = 0;
	scissorRect.right = windowWidth;
	scissorRect.bottom = windowHeight;

	MSG msg{};
	while (msg.message != WM_QUIT)
	{
		if (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
		else
		{
			commandAllocator->Reset();
			commandList->Reset(commandAllocator.Get(), nullptr);

			CD3DX12_RESOURCE_BARRIER resPresent2Render(CD3DX12_RESOURCE_BARRIER::Transition(swapChainBuffer[currentBackBuffer].Get(),
				D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));
			commandList->ResourceBarrier(1, &resPresent2Render);

			commandList->RSSetViewports(1, &screenViewport);
			commandList->RSSetScissorRects(1, &scissorRect);
			
			CD3DX12_CPU_DESCRIPTOR_HANDLE currentBackBufferView(rtvHeap->GetCPUDescriptorHandleForHeapStart(),
				currentBackBuffer, rtvDescriptorSize);
			commandList->ClearRenderTargetView(currentBackBufferView, clearColor, 0, nullptr);

			CD3DX12_CPU_DESCRIPTOR_HANDLE depthStencilView(dsvHeap->GetCPUDescriptorHandleForHeapStart());
			commandList->ClearDepthStencilView(depthStencilView,
				D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

			commandList->OMSetRenderTargets(1, &currentBackBufferView, true, &depthStencilView);

			CD3DX12_RESOURCE_BARRIER resRender2Present(CD3DX12_RESOURCE_BARRIER::Transition(swapChainBuffer[currentBackBuffer].Get(),
				D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));
			commandList->ResourceBarrier(1, &resRender2Present);
			commandList->Close();

			ID3D12CommandList* cmdsLists[]{ commandList.Get() };
			commandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
			swapChain->Present(0, 0);
			currentBackBuffer = (currentBackBuffer + 1) % swapChainBufferCount;

			FlushCommandQueue();
		}
	}

	d3dDevice->Release();
	dxgiFactory->Release();

	return 0;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	default:
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}
}

void FlushCommandQueue()
{
	currentFence++;

	commandQueue->Signal(fence.Get(), currentFence);

	if (fence->GetCompletedValue() < currentFence)
	{
		HANDLE eventHandle{};
		eventHandle = CreateEventExW(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
		fence->SetEventOnCompletion(currentFence, eventHandle);
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}
}