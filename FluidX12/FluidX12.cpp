//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#include "FluidX12.h"

using namespace std;
using namespace XUSG;


enum RenderMethod
{
	RAY_MARCH_MERGED,
	RAY_MARCH_SEPARATE,

	NUM_RENDER_METHOD
};
RenderMethod g_renderMethod = RAY_MARCH_SEPARATE;

const float g_FOVAngleY = XM_PIDIV4;
const float g_zNear = 1.0f;
const float g_zFar = 1000.0f;

FluidX::FluidX(uint32_t width, uint32_t height, std::wstring name) :
	DXFramework(width, height, name),
	m_frameIndex(0),
	m_showFPS(true),
	m_isPaused(false),
	m_tracking(false),
	m_gridSize(128, 128, 128),
	m_numParticles(0)
{
#if defined (_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
	AllocConsole();
	FILE* stream;
	freopen_s(&stream, "CONOUT$", "w+t", stdout);
	freopen_s(&stream, "CONIN$", "r+t", stdin);
#endif
}

FluidX::~FluidX()
{
#if defined (_DEBUG)
	FreeConsole();
#endif
}

void FluidX::OnInit()
{
	LoadPipeline();
	LoadAssets();
}

// Load the rendering pipeline dependencies.
void FluidX::LoadPipeline()
{
	auto dxgiFactoryFlags = 0u;

#if defined(_DEBUG)
	// Enable the debug layer (requires the Graphics Tools "optional feature").
	// NOTE: Enabling the debug layer after device creation will invalidate the active device.
	{
		com_ptr<ID3D12Debug1> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();
			//debugController->SetEnableGPUBasedValidation(TRUE);

			// Enable additional debug layers.
			dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
	}
#endif

	com_ptr<IDXGIFactory4> factory;
	ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

	DXGI_ADAPTER_DESC1 dxgiAdapterDesc;
	com_ptr<IDXGIAdapter1> dxgiAdapter;
	auto hr = DXGI_ERROR_UNSUPPORTED;
	for (auto i = 0u; hr == DXGI_ERROR_UNSUPPORTED; ++i)
	{
		dxgiAdapter = nullptr;
		ThrowIfFailed(factory->EnumAdapters1(i, &dxgiAdapter));

		m_device = Device::MakeShared();
		hr = m_device->Create(dxgiAdapter.get(), D3D_FEATURE_LEVEL_11_0);
	}

	dxgiAdapter->GetDesc1(&dxgiAdapterDesc);
	if (dxgiAdapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		m_title += dxgiAdapterDesc.VendorId == 0x1414 && dxgiAdapterDesc.DeviceId == 0x8c ? L" (WARP)" : L" (Software)";
	ThrowIfFailed(hr);

	// Create the command queue.
	m_commandQueue = CommandQueue::MakeUnique();
	N_RETURN(m_commandQueue->Create(m_device.get(), CommandListType::DIRECT, CommandQueueFlag::NONE,
		0, 0, L"CommandQueue"), ThrowIfFailed(E_FAIL));

	// Describe and create the swap chain.
	m_swapChain = SwapChain::MakeUnique();
	N_RETURN(m_swapChain->Create(factory.Get(), Win32Application::GetHwnd(), m_commandQueue.get(),
		FrameCount, m_width, m_height, Format::B8G8R8A8_UNORM), ThrowIfFailed(E_FAIL));

	// This sample does not support fullscreen transitions.
	ThrowIfFailed(factory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER));

	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	m_descriptorTableCache = DescriptorTableCache::MakeShared(m_device.get(), L"DescriptorTableCache");

	// Create frame resources.
	// Create a RTV and a command allocator for each frame.
	for (uint8_t n = 0; n < FrameCount; ++n)
	{
		m_renderTargets[n] = RenderTarget::MakeUnique();
		N_RETURN(m_renderTargets[n]->CreateFromSwapChain(m_device.get(), m_swapChain.get(), n), ThrowIfFailed(E_FAIL));

		m_commandAllocators[n] = CommandAllocator::MakeUnique();
		N_RETURN(m_commandAllocators[n]->Create(m_device.get(), CommandListType::DIRECT,
			(L"CommandAllocator" + to_wstring(n)).c_str()), ThrowIfFailed(E_FAIL));
	}

	// Create output views
	m_depth = DepthStencil::MakeUnique();
	m_depth->Create(m_device.get(), m_width, m_height, Format::D24_UNORM_S8_UINT,
		ResourceFlag::DENY_SHADER_RESOURCE, 1, 1, 1, 1.0f, 0, false, L"Depth");
}

// Load the sample assets.
void FluidX::LoadAssets()
{
	// Create the command list.
	m_commandList = CommandList::MakeUnique();
	const auto pCommandList = m_commandList.get();
	N_RETURN(pCommandList->Create(m_device.get(), 0, CommandListType::DIRECT,
		m_commandAllocators[m_frameIndex].get(), nullptr), ThrowIfFailed(E_FAIL));

	vector<Resource::uptr> uploaders(0);
	// Create fast hybrid fluid simulator
	m_fluid = make_unique<Fluid>(m_device);
	if (!m_fluid) ThrowIfFailed(E_FAIL);
	if (!m_fluid->Init(pCommandList, m_width, m_height, m_descriptorTableCache, uploaders,
		Format::B8G8R8A8_UNORM, Format::D24_UNORM_S8_UINT, m_gridSize, m_numParticles))
		ThrowIfFailed(E_FAIL);

	// Close the command list and execute it to begin the initial GPU setup.
	N_RETURN(pCommandList->Close(), ThrowIfFailed(E_FAIL));
	m_commandQueue->ExecuteCommandList(pCommandList);

	// Create synchronization objects and wait until assets have been uploaded to the GPU.
	{
		if (!m_fence)
		{
			m_fence = Fence::MakeUnique();
			N_RETURN(m_fence->Create(m_device.get(), m_fenceValues[m_frameIndex]++, FenceFlag::NONE, L"Fence"), ThrowIfFailed(E_FAIL));
		}

		// Create an event handle to use for frame synchronization.
		m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (!m_fenceEvent) ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));

		// Wait for the command list to execute; we are reusing the same command 
		// list in our main loop but for now, we just want to wait for setup to 
		// complete before continuing.
		WaitForGpu();
	}

	// Projection
	const auto aspectRatio = m_width / static_cast<float>(m_height);
	const auto proj = XMMatrixPerspectiveFovLH(g_FOVAngleY, aspectRatio, g_zNear, g_zFar);
	XMStoreFloat4x4(&m_proj, proj);

	// View initialization
	m_focusPt = XMFLOAT3(0.0f, 0.0f, 0.0f);
	m_eyePt = XMFLOAT3(4.0f, 16.0f, -40.0f);
	const auto focusPt = XMLoadFloat3(&m_focusPt);
	const auto eyePt = XMLoadFloat3(&m_eyePt);
	const auto view = XMMatrixLookAtLH(eyePt, focusPt, XMVectorSet(0.0f, 1.0f, 0.0f, 1.0f));
	XMStoreFloat4x4(&m_view, view);
}

// Update frame-based values.
void FluidX::OnUpdate()
{
	// Timer
	static auto time = 0.0, pauseTime = 0.0;

	m_timer.Tick();
	float timeStep;
	const auto totalTime = CalculateFrameStats(&timeStep);
	pauseTime = m_isPaused ? totalTime - time : pauseTime;
	//timeStep = (max)(timeStep, (m_gridSize.z > 1 ? 2.0f : 1.0f) / m_gridSize.y);
	timeStep = (m_gridSize.z > 1 ? 2.0f : 1.0f) / m_gridSize.y;
	timeStep = m_isPaused ? 0.0f : timeStep;
	time = totalTime - pauseTime;

	// View
	//const auto eyePt = XMLoadFloat3(&m_eyePt);
	//const auto view = XMLoadFloat4x4(&m_view);
	//const auto proj = XMLoadFloat4x4(&m_proj);
	//const auto viewProj = view * proj;
	m_fluid->UpdateFrame(timeStep, m_frameIndex, m_view, m_proj, m_eyePt);
}

// Render the scene.
void FluidX::OnRender()
{
	// Record all the commands we need to render the scene into the command list.
	PopulateCommandList();

	// Execute the command list.
	m_commandQueue->ExecuteCommandList(m_commandList.get());

	// Present the frame.
	N_RETURN(m_swapChain->Present(0, 0), ThrowIfFailed(E_FAIL));

	MoveToNextFrame();
}

void FluidX::OnDestroy()
{
	// Ensure that the GPU is no longer referencing resources that are about to be
	// cleaned up by the destructor.
	WaitForGpu();

	CloseHandle(m_fenceEvent);
}

// User hot-key interactions.
void FluidX::OnKeyUp(uint8_t key)
{
	switch (key)
	{
	case VK_SPACE:
		m_isPaused = !m_isPaused;
		break;
	case VK_F1:
		m_showFPS = !m_showFPS;
		break;
	case VK_LEFT:
		g_renderMethod = static_cast<RenderMethod>((g_renderMethod + NUM_RENDER_METHOD - 1) % NUM_RENDER_METHOD);
		break;
	case VK_RIGHT:
		g_renderMethod = static_cast<RenderMethod>((g_renderMethod + 1) % NUM_RENDER_METHOD);
		break;
	}
}

// User camera interactions.
void FluidX::OnLButtonDown(float posX, float posY)
{
	m_tracking = true;
	m_mousePt = XMFLOAT2(posX, posY);
}

void FluidX::OnLButtonUp(float posX, float posY)
{
	m_tracking = false;
}

void FluidX::OnMouseMove(float posX, float posY)
{
	if (m_tracking)
	{
		const auto dPos = XMFLOAT2(m_mousePt.x - posX, m_mousePt.y - posY);

		XMFLOAT2 radians;
		radians.x = XM_2PI * dPos.y / m_height;
		radians.y = XM_2PI * dPos.x / m_width;

		const auto focusPt = XMLoadFloat3(&m_focusPt);
		auto eyePt = XMLoadFloat3(&m_eyePt);

		const auto len = XMVectorGetX(XMVector3Length(focusPt - eyePt));
		auto transform = XMMatrixTranslation(0.0f, 0.0f, -len);
		transform *= XMMatrixRotationRollPitchYaw(radians.x, radians.y, 0.0f);
		transform *= XMMatrixTranslation(0.0f, 0.0f, len);

		const auto view = XMLoadFloat4x4(&m_view) * transform;
		const auto viewInv = XMMatrixInverse(nullptr, view);
		eyePt = viewInv.r[3];

		XMStoreFloat3(&m_eyePt, eyePt);
		XMStoreFloat4x4(&m_view, view);

		m_mousePt = XMFLOAT2(posX, posY);
	}
}

void FluidX::OnMouseWheel(float deltaZ, float posX, float posY)
{
	const auto focusPt = XMLoadFloat3(&m_focusPt);
	auto eyePt = XMLoadFloat3(&m_eyePt);

	const auto len = XMVectorGetX(XMVector3Length(focusPt - eyePt));
	const auto transform = XMMatrixTranslation(0.0f, 0.0f, -len * deltaZ / 16.0f);

	const auto view = XMLoadFloat4x4(&m_view) * transform;
	const auto viewInv = XMMatrixInverse(nullptr, view);
	eyePt = viewInv.r[3];

	XMStoreFloat3(&m_eyePt, eyePt);
	XMStoreFloat4x4(&m_view, view);
}

void FluidX::OnMouseLeave()
{
	m_tracking = false;
}

void FluidX::ParseCommandLineArgs(wchar_t* argv[], int argc)
{
	DXFramework::ParseCommandLineArgs(argv, argc);

	for (auto i = 1; i < argc; ++i)
	{
		if (_wcsnicmp(argv[i], L"-gridSize", wcslen(argv[i])) == 0 ||
			_wcsnicmp(argv[i], L"/gridSize", wcslen(argv[i])) == 0)
		{
			m_gridSize.x = ++i < argc ? static_cast<uint32_t>(_wtof(argv[i])) : m_gridSize.x;
			m_gridSize.y = ++i < argc ? static_cast<uint32_t>(_wtof(argv[i])) : m_gridSize.y;
			m_gridSize.z = ++i < argc ? static_cast<uint32_t>(_wtof(argv[i])) : m_gridSize.z;
		}
		else if (_wcsnicmp(argv[i], L"-particles", wcslen(argv[i])) == 0 ||
			_wcsnicmp(argv[i], L"/particles", wcslen(argv[i])) == 0)
		{
			m_numParticles = ++i < argc ? static_cast<uint32_t>(_wtof(argv[i])) : m_numParticles;
		}
	}
}

void FluidX::PopulateCommandList()
{
	// Command list allocators can only be reset when the associated 
	// command lists have finished execution on the GPU; apps should use 
	// fences to determine GPU execution progress.
	const auto pCommandAllocator = m_commandAllocators[m_frameIndex].get();
	N_RETURN(pCommandAllocator->Reset(), ThrowIfFailed(E_FAIL));

	// However, when ExecuteCommandList() is called on a particular command 
	// list, that command list can then be reset at any time and must be before 
	// re-recording.
	const auto pCommandList = m_commandList.get();
	N_RETURN(pCommandList->Reset(pCommandAllocator, nullptr), ThrowIfFailed(E_FAIL));

	// Record commands.
	const DescriptorPool descriptorPools[] =
	{
		m_descriptorTableCache->GetDescriptorPool(CBV_SRV_UAV_POOL),
		m_descriptorTableCache->GetDescriptorPool(SAMPLER_POOL)
	};
	pCommandList->SetDescriptorPools(static_cast<uint32_t>(size(descriptorPools)), descriptorPools);

	// Fluid simulation
	m_fluid->Simulate(pCommandList, m_frameIndex);

	ResourceBarrier barriers[1];
	auto numBarriers = m_renderTargets[m_frameIndex]->SetBarrier(barriers, ResourceState::RENDER_TARGET);
	pCommandList->Barrier(numBarriers, barriers);

	// Clear render target
	const float clearColor[4] = { 0.2f, 0.2f, 0.2f, 0.0f };
	pCommandList->ClearRenderTargetView(m_renderTargets[m_frameIndex]->GetRTV(), clearColor);
	pCommandList->ClearDepthStencilView(m_depth->GetDSV(), ClearFlag::DEPTH, 1.0f);

	pCommandList->OMSetRenderTargets(1, &m_renderTargets[m_frameIndex]->GetRTV(), &m_depth->GetDSV());

	// Set viewport
	Viewport viewport(0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height));
	RectRange scissorRect(0, 0, m_width, m_height);
	pCommandList->RSSetViewports(1, &viewport);
	pCommandList->RSSetScissorRects(1, &scissorRect);

	switch (g_renderMethod)
	{
	case RAY_MARCH_MERGED:
		m_fluid->Render(pCommandList, m_frameIndex, Fluid::RAY_MARCH_DIRECT);
		break;
	case RAY_MARCH_SEPARATE:
		m_fluid->Render(pCommandList, m_frameIndex, Fluid::SEPARATE_LIGHT_PASS);
		break;
	default:
		m_fluid->Render(pCommandList, m_frameIndex, Fluid::RAY_MARCH_DIRECT);
	}

	// Indicate that the back buffer will now be used to present.
	numBarriers = m_renderTargets[m_frameIndex]->SetBarrier(barriers, ResourceState::PRESENT);
	pCommandList->Barrier(numBarriers, barriers);

	N_RETURN(pCommandList->Close(), ThrowIfFailed(E_FAIL));
}

// Wait for pending GPU work to complete.
void FluidX::WaitForGpu()
{
	// Schedule a Signal command in the queue.
	N_RETURN(m_commandQueue->Signal(m_fence.get(), m_fenceValues[m_frameIndex]), ThrowIfFailed(E_FAIL));

	// Wait until the fence has been processed.
	N_RETURN(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent), ThrowIfFailed(E_FAIL));
	WaitForSingleObject(m_fenceEvent, INFINITE);

	// Increment the fence value for the current frame.
	m_fenceValues[m_frameIndex]++;
}

// Prepare to render the next frame.
void FluidX::MoveToNextFrame()
{
	// Schedule a Signal command in the queue.
	const auto currentFenceValue = m_fenceValues[m_frameIndex];
	N_RETURN(m_commandQueue->Signal(m_fence.get(), currentFenceValue), ThrowIfFailed(E_FAIL));

	// Update the frame index.
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	// If the next frame is not ready to be rendered yet, wait until it is ready.
	if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex])
	{
		N_RETURN(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent), ThrowIfFailed(E_FAIL));
		WaitForSingleObject(m_fenceEvent, INFINITE);
	}

	// Set the fence value for the next frame.
	m_fenceValues[m_frameIndex] = currentFenceValue + 1;
}

double FluidX::CalculateFrameStats(float* pTimeStep)
{
	static int frameCnt = 0;
	static double elapsedTime = 0.0;
	static double previousTime = 0.0;
	const auto totalTime = m_timer.GetTotalSeconds();
	++frameCnt;

	const auto timeStep = static_cast<float>(totalTime - elapsedTime);

	// Compute averages over one second period.
	if ((totalTime - elapsedTime) >= 1.0f)
	{
		float fps = static_cast<float>(frameCnt) / timeStep;	// Normalize to an exact second.

		frameCnt = 0;
		elapsedTime = totalTime;

		wstringstream windowText;
		windowText << L"    fps: ";
		if (m_showFPS) windowText << setprecision(2) << fixed << fps;
		else windowText << L"[F1]";
		windowText << "  ";
		switch (g_renderMethod)
		{
		case RAY_MARCH_MERGED:
			windowText << L"Ray marching without separate lighting pass";
			break;
		case RAY_MARCH_SEPARATE:
			windowText << L"Ray marching with separate lighting pass";
			break;
		default:
			windowText << L"Simple particle rendering";
		}

		SetCustomWindowText(windowText.str().c_str());
	}

	if (pTimeStep)* pTimeStep = static_cast<float>(totalTime - previousTime);
	previousTime = totalTime;

	return totalTime;
}
