// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "pch.h"

#include "ReadData.h"

using winrt::com_ptr;
using winrt::check_hresult;
using winrt::check_bool;
using winrt::handle;

#define USE_VPU 0

// Divide and round up
static UINT DivUp(UINT a, UINT b)
{
    return (a + b - 1) / b;
}

D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(
    ID3D12Device* d3d12_device,
    ID3D12DescriptorHeap* descriptor_heap,
    size_t index) {
  D3D12_DESCRIPTOR_HEAP_DESC heap_desc = descriptor_heap->GetDesc();
  uint32_t increment_size =
      d3d12_device->GetDescriptorHandleIncrementSize(heap_desc.Type);
  D3D12_GPU_DESCRIPTOR_HANDLE handle;
  D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle =
      descriptor_heap->GetGPUDescriptorHandleForHeapStart();
  handle.ptr = gpu_handle.ptr + UINT64(index) * UINT64(increment_size);
  return handle;
}

D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle(
    ID3D12Device* d3d12_device,
    ID3D12DescriptorHeap* descriptor_heap,
    size_t index) {
  D3D12_DESCRIPTOR_HEAP_DESC heap_desc = descriptor_heap->GetDesc();
  uint32_t increment_size =
      d3d12_device->GetDescriptorHandleIncrementSize(heap_desc.Type);
  D3D12_CPU_DESCRIPTOR_HANDLE handle;
  D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle =
      descriptor_heap->GetCPUDescriptorHandleForHeapStart();
  handle.ptr = cpu_handle.ptr + UINT64(index) * UINT64(increment_size);
  return handle;
}

std::string DriverDescription(com_ptr<IDXCoreAdapter>& adapter, bool selected = false) {
	// If the adapter is a software adapter then don't consider it for index selection
	size_t driverDescriptionSize;
	check_hresult(adapter->GetPropertySize(DXCoreAdapterProperty::DriverDescription,
		&driverDescriptionSize));
	CHAR* driverDescription = new CHAR[driverDescriptionSize];
	check_hresult(adapter->GetProperty(DXCoreAdapterProperty::DriverDescription,
		driverDescriptionSize, driverDescription));
	if (selected) {
		printf("Use adapter : %s\n", driverDescription);
	}

	std::string driverDescriptionStr = std::string(driverDescription);
	free(driverDescription);

	return driverDescriptionStr;
}

void InitWithDXCore(com_ptr<ID3D12Device>& d3D12Device,
	com_ptr<ID3D12CommandQueue>& commandQueue,
	com_ptr<ID3D12CommandAllocator>& commandAllocator,
	com_ptr<ID3D12GraphicsCommandList>& commandList) {
	HMODULE library = nullptr;
	library = LoadLibrary(L"dxcore.dll");
	if (!library) {
		//throw hresult_invalid_argument(L"DXCore isn't support on this manchine.");
		std::wcout << L"DXCore isn't support on this manchine. ";
		return;
	}

	com_ptr<IDXCoreAdapterFactory> adapterFactory;
	check_hresult(DXCoreCreateAdapterFactory(IID_PPV_ARGS(adapterFactory.put())));

	com_ptr<IDXCoreAdapterList> adapterList;
	const GUID dxGUIDs[] = { DXCORE_ADAPTER_ATTRIBUTE_D3D12_CORE_COMPUTE };

	check_hresult(
		adapterFactory->CreateAdapterList(ARRAYSIZE(dxGUIDs), dxGUIDs, IID_PPV_ARGS(adapterList.put())));

	com_ptr<IDXCoreAdapter> currAdapter = nullptr;
	IUnknown* pAdapter = nullptr;
	com_ptr<IDXGIAdapter> dxgiAdapter;
	D3D_FEATURE_LEVEL d3dFeatureLevel = D3D_FEATURE_LEVEL_1_0_CORE;
	D3D12_COMMAND_LIST_TYPE commandQueueType = D3D12_COMMAND_LIST_TYPE_COMPUTE;
	for (UINT i = 0; i < adapterList->GetAdapterCount(); i++) {
		currAdapter = nullptr;
		check_hresult(adapterList->GetAdapter(i, currAdapter.put()));

		bool isHardware;
		check_hresult(currAdapter->GetProperty(DXCoreAdapterProperty::IsHardware, &isHardware));
#if USE_VPU == 1
		std::string adapterNameStr = "VPU";
		std::string driverDescriptionStr = DriverDescription(currAdapter);
		std::transform(driverDescriptionStr.begin(), driverDescriptionStr.end(),
			driverDescriptionStr.begin(), ::tolower);
		std::transform(adapterNameStr.begin(), adapterNameStr.end(), adapterNameStr.begin(),
			::tolower);
		if (isHardware && strstr(driverDescriptionStr.c_str(), adapterNameStr.c_str())) {
			pAdapter = currAdapter.get();
			break;
	}
#else
		// Check if adapter selected has DXCORE_ADAPTER_ATTRIBUTE_D3D12_GRAPHICS attribute selected. If
		// so, then GPU was selected that has D3D12 and D3D11 capabilities. It would be the most stable
		// to use DXGI to enumerate GPU and use D3D_FEATURE_LEVEL_11_0 so that image tensorization for
		// video frames would be able to happen on the GPU.
		if (isHardware && currAdapter->IsAttributeSupported(DXCORE_ADAPTER_ATTRIBUTE_D3D12_GRAPHICS)) {
			d3dFeatureLevel = D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_11_0;
			com_ptr<IDXGIFactory4> dxgiFactory4;
			HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(dxgiFactory4.put()));
			if (hr == S_OK) {
				// If DXGI factory creation was successful then get the IDXGIAdapter from the LUID
				// acquired from the selectedAdapter
				LUID adapterLuid;
				check_hresult(currAdapter->GetProperty(DXCoreAdapterProperty::InstanceLuid, &adapterLuid));
				check_hresult(dxgiFactory4->EnumAdapterByLuid(adapterLuid, __uuidof(IDXGIAdapter),
					dxgiAdapter.put_void()));
				pAdapter = dxgiAdapter.get();
				break;
			}
		}
#endif
}

	if (currAdapter == nullptr) {
		std::wcout << L"ERROR: No matching adapter with given adapter name: ";
		return;
	}
	DriverDescription(currAdapter, true);

	// create D3D12Device
	check_hresult(
		D3D12CreateDevice(pAdapter, d3dFeatureLevel, __uuidof(ID3D12Device), d3D12Device.put_void()));

	// create D3D12 command queue from device
	//com_ptr<ID3D12CommandQueue> d3d12CommandQueue;
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc = {};
	commandQueueDesc.Type = commandQueueType;
	check_hresult(d3D12Device->CreateCommandQueue(&commandQueueDesc, __uuidof(ID3D12CommandQueue),
		commandQueue.put_void()));

	check_hresult(d3D12Device->CreateCommandAllocator(
		commandQueueType,
		__uuidof(commandAllocator),
		commandAllocator.put_void()));

	check_hresult(d3D12Device->CreateCommandList(
		0,
		commandQueueType,
		commandAllocator.get(),
		nullptr,
		__uuidof(commandList),
		commandList.put_void()));
}

void CloseExecuteResetWait(
    com_ptr<ID3D12Device> d3D12Device,
    com_ptr<ID3D12CommandQueue> commandQueue,
    com_ptr<ID3D12CommandAllocator> commandAllocator,
    com_ptr<ID3D12GraphicsCommandList> commandList)
{
    check_hresult(commandList->Close());

    ID3D12CommandList* commandLists[] = { commandList.get() };
    commandQueue->ExecuteCommandLists(ARRAYSIZE(commandLists), commandLists);

    check_hresult(commandList->Reset(commandAllocator.get(), nullptr));

    com_ptr<ID3D12Fence> d3D12Fence;
    check_hresult(d3D12Device->CreateFence(
        0,
        D3D12_FENCE_FLAG_NONE,
        _uuidof(d3D12Fence),
        d3D12Fence.put_void()));

    handle fenceEventHandle{ 0 };
    fenceEventHandle.attach(::CreateEvent(nullptr, true, false, nullptr));
    check_bool(bool{ fenceEventHandle });

    check_hresult(d3D12Fence->SetEventOnCompletion(1, fenceEventHandle.get()));

    check_hresult(commandQueue->Signal(d3D12Fence.get(), 1));
    ::WaitForSingleObjectEx(fenceEventHandle.get(), INFINITE, FALSE);
}

int __cdecl wmain(int /*argc*/, char ** /*argv*/)
{
    com_ptr<ID3D12Device> d3D12Device;
    com_ptr<ID3D12CommandQueue> commandQueue;
    com_ptr<ID3D12CommandAllocator> commandAllocator;
    com_ptr<ID3D12GraphicsCommandList> commandList;

    // Set up Direct3D 12.
	InitWithDXCore(d3D12Device, commandQueue, commandAllocator, commandList);

    // 24 elements * 4 == 96 bytes.
	constexpr UINT tensorSizes[4] = { 1, 2, 3, 4 };
	constexpr UINT tensorElementCount = tensorSizes[0] * tensorSizes[1] * tensorSizes[2] * tensorSizes[3];
	UINT64 tensorBufferSize = tensorElementCount * sizeof(float);

	// Create descriptor heap
    com_ptr<ID3D12DescriptorHeap> descriptorHeap;
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
	descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	descriptorHeapDesc.NumDescriptors = 2;
	descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	check_hresult(d3D12Device->CreateDescriptorHeap(
		&descriptorHeapDesc,
		_uuidof(descriptorHeap),
		descriptorHeap.put_void()));

    // Compute object
    Microsoft::WRL::ComPtr<ID3D12PipelineState> computePSO;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> computeRootSignature;

    // Define root table layout
    CD3DX12_DESCRIPTOR_RANGE descRange[1];
    descRange[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0); // u0, u1

    CD3DX12_ROOT_PARAMETER rootParameters[2];
    rootParameters[0].InitAsConstants(3, 0);
    rootParameters[1].InitAsDescriptorTable(1, descRange, D3D12_SHADER_VISIBILITY_ALL);
    CD3DX12_ROOT_SIGNATURE_DESC rootSignature(_countof(rootParameters), rootParameters);

    Microsoft::WRL::ComPtr<ID3DBlob> serializedSignature;
    check_hresult(
        D3D12SerializeRootSignature(&rootSignature, D3D_ROOT_SIGNATURE_VERSION_1, serializedSignature.GetAddressOf(), nullptr));

    // Create the root signature
    check_hresult(
        d3D12Device->CreateRootSignature(
            0,
            serializedSignature->GetBufferPointer(),
            serializedSignature->GetBufferSize(),
            IID_PPV_ARGS(computeRootSignature.ReleaseAndGetAddressOf())));
    computeRootSignature->SetName(L"Compute RS");

	Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer;
	Microsoft::WRL::ComPtr<ID3D12Resource> inputBuffer;
	{
		check_hresult(d3D12Device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(tensorBufferSize),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&uploadBuffer)));

		check_hresult(d3D12Device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(tensorBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&inputBuffer)));

		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.FirstElement = 0;
		uavDesc.Buffer.NumElements = tensorElementCount;
		uavDesc.Buffer.StructureByteStride = 0;
		uavDesc.Buffer.CounterOffsetInBytes = 0;
		uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

		d3D12Device->CreateUnorderedAccessView(inputBuffer.Get(), nullptr, &uavDesc,
			GetCpuHandle(d3D12Device.get(), descriptorHeap.get(), 0));
	}

    Microsoft::WRL::ComPtr<ID3D12Resource> outputBuffer;
	{
		D3D12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(tensorBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		check_hresult(d3D12Device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&outputBuffer)
		));
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.FirstElement = 0;
		uavDesc.Buffer.NumElements = tensorElementCount;
		uavDesc.Buffer.StructureByteStride = 0;
		uavDesc.Buffer.CounterOffsetInBytes = 0;
		uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

		d3D12Device->CreateUnorderedAccessView(outputBuffer.Get(), nullptr, &uavDesc,
			GetCpuHandle(d3D12Device.get(), descriptorHeap.get(), 1));
	}
	// Create compute pipeline state
	auto computeShaderBlob = DX::ReadData(L"identity.cso");
	D3D12_COMPUTE_PIPELINE_STATE_DESC descComputePSO = {};
	descComputePSO.pRootSignature = computeRootSignature.Get();
	descComputePSO.CS.pShaderBytecode = computeShaderBlob.data();
	descComputePSO.CS.BytecodeLength = computeShaderBlob.size();

	check_hresult(
		d3D12Device->CreateComputePipelineState(&descComputePSO, IID_PPV_ARGS(computePSO.ReleaseAndGetAddressOf())));
	computePSO->SetName(L"Compute PSO");

	ID3D12DescriptorHeap* pHeaps[] = { descriptorHeap.get() };
	commandList->SetDescriptorHeaps(_countof(pHeaps), pHeaps);

	commandList->SetComputeRootSignature(computeRootSignature.Get());
	struct ImageLayoutCB
	{
		UINT Height;
		UINT Width;
		UINT Channel;
	};
	ImageLayoutCB imageLayoutCB = {};
	imageLayoutCB.Height = 3;
	imageLayoutCB.Width = 4;
	imageLayoutCB.Channel = 2;

	commandList->SetComputeRoot32BitConstants(0, 4, &imageLayoutCB, 0);
	commandList->SetComputeRootDescriptorTable(1, GetGpuHandle(d3D12Device.get(), descriptorHeap.get(), 0));

	commandList->SetPipelineState(computePSO.Get());

	std::wcout << std::fixed; std::wcout.precision(2);
	std::array<FLOAT, tensorElementCount> inputTensorElementArray;
	{
		std::wcout << L"input tensor: ";
		for (auto& element : inputTensorElementArray)
		{
			element = 1.618f;
			std::wcout << element << L' ';
		};
		std::wcout << std::endl;

		D3D12_SUBRESOURCE_DATA tensorSubresourceData{};
		tensorSubresourceData.pData = inputTensorElementArray.data();
		tensorSubresourceData.RowPitch = tensorBufferSize;
		tensorSubresourceData.SlicePitch = tensorSubresourceData.RowPitch;

		// Upload the input tensor to the GPU.
		::UpdateSubresources(
			commandList.get(),
			inputBuffer.Get(),
			uploadBuffer.Get(),
			0,
			0,
			1,
			&tensorSubresourceData);

		commandList->ResourceBarrier(
			1,
			&CD3DX12_RESOURCE_BARRIER::Transition(
				inputBuffer.Get(),
				D3D12_RESOURCE_STATE_COPY_DEST,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS
			)
		);
	}
	
	commandList->Dispatch(DivUp(3, 32), DivUp(4, 16), 1);

	commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(nullptr));

    // The output buffer now contains the result of the identity operator,
    // so read it back if you want the CPU to access it.

    com_ptr<ID3D12Resource> readbackBuffer;
    check_hresult(d3D12Device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(tensorBufferSize),
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        __uuidof(readbackBuffer),
        readbackBuffer.put_void()));

    commandList->ResourceBarrier(
        1,
        &CD3DX12_RESOURCE_BARRIER::Transition(
			outputBuffer.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_COPY_SOURCE
            )
        );

    commandList->CopyResource(readbackBuffer.get(), outputBuffer.Get());

    CloseExecuteResetWait(d3D12Device, commandQueue, commandAllocator, commandList);

    D3D12_RANGE tensorBufferRange{ 0, tensorBufferSize };
    FLOAT* outputBufferData{};
    check_hresult(readbackBuffer->Map(0, &tensorBufferRange, reinterpret_cast<void**>(&outputBufferData)));

    std::wcout << L"output tensor: ";
    for (size_t tensorElementIndex{ 0 }; tensorElementIndex < tensorElementCount; ++tensorElementIndex, ++outputBufferData)
    {
        std::wcout << *outputBufferData << L' ';
    }
    std::wcout << std::endl;

    D3D12_RANGE emptyRange{ 0, 0 };
    readbackBuffer->Unmap(0, &emptyRange);
}
