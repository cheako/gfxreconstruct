/*
** Copyright (c) 2021 LunarG, Inc.
** Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
**
** Permission is hereby granted, free of charge, to any person obtaining a
** copy of this software and associated documentation files (the "Software"),
** to deal in the Software without restriction, including without limitation
** the rights to use, copy, modify, merge, publish, distribute, sublicense,
** and/or sell copies of the Software, and to permit persons to whom the
** Software is furnished to do so, subject to the following conditions:
**
** The above copyright notice and this permission notice shall be included in
** all copies or substantial portions of the Software.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
** AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
** LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
** FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
** DEALINGS IN THE SOFTWARE.
*/

#ifndef GFXRECON_DECODE_DX12_REPLAY_CONSUMER_BASE_H
#define GFXRECON_DECODE_DX12_REPLAY_CONSUMER_BASE_H

#include "decode/custom_dx12_struct_decoders_forward.h"
#include "decode/dx_replay_options.h"
#include "decode/dx12_object_info.h"
#include "decode/dx12_object_mapping_util.h"
#include "decode/window.h"
#include "format/format.h"
#include "generated/generated_dx12_consumer.h"
#include "graphics/dx12_gpu_va_map.h"
#include "graphics/dx12_shader_id_map.h"
#include "graphics/dx12_resource_data_util.h"
#include "graphics/dx12_image_renderer.h"
#include "decode/screenshot_handler_base.h"
#include "graphics/fps_info.h"
#include "graphics/dx12_util.h"
#include "application/application.h"

#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <dxgidebug.h>

#include <wrl/client.h>

GFXRECON_BEGIN_NAMESPACE(gfxrecon)
GFXRECON_BEGIN_NAMESPACE(decode)

class Dx12ReplayConsumerBase : public Dx12Consumer
{
  public:
    Dx12ReplayConsumerBase(std::shared_ptr<application::Application> application, const DxReplayOptions& options);

    virtual ~Dx12ReplayConsumerBase() override;

    void SetFatalErrorHandler(std::function<void(const char*)> handler) { fatal_error_handler_ = handler; }

    void SetFpsInfo(graphics::FpsInfo* fps_info) { fps_info_ = fps_info; }

    virtual void ProcessStateBeginMarker(uint64_t frame_number) override;

    virtual void ProcessStateEndMarker(uint64_t frame_number) override;

    virtual void
    ProcessFillMemoryCommand(uint64_t memory_id, uint64_t offset, uint64_t size, const uint8_t* data) override;

    virtual void ProcessCreateHeapAllocationCommand(uint64_t allocation_id, uint64_t allocation_size) override;

    virtual void ProcessBeginResourceInitCommand(format::HandleId device_id,
                                                 uint64_t         max_resource_size,
                                                 uint64_t         max_copy_size) override;

    virtual void ProcessEndResourceInitCommand(format::HandleId device_id) override;

    virtual void
    ProcessSetSwapchainImageStateCommand(format::HandleId                                    device_id,
                                         format::HandleId                                    swapchain_id,
                                         uint32_t                                            current_buffer_index,
                                         const std::vector<format::SwapchainImageStateInfo>& image_state) override;

    virtual void ProcessInitSubresourceCommand(const format::InitSubresourceCommandHeader& command_header,
                                               const uint8_t*                              data) override;

    virtual void Process_ID3D12Device_CheckFeatureSupport(format::HandleId object_id,
                                                          HRESULT          original_result,
                                                          D3D12_FEATURE    feature,
                                                          const void*      capture_feature_data,
                                                          void*            replay_feature_data,
                                                          UINT             feature_data_size) override;

    virtual void Process_IDXGIFactory5_CheckFeatureSupport(format::HandleId object_id,
                                                           HRESULT          original_result,
                                                           DXGI_FEATURE     feature,
                                                           const void*      capture_feature_data,
                                                           void*            replay_feature_data,
                                                           UINT             feature_data_size) override;

  protected:
    void MapGpuDescriptorHandle(D3D12_GPU_DESCRIPTOR_HANDLE& handle);

    void MapGpuDescriptorHandles(D3D12_GPU_DESCRIPTOR_HANDLE* handles, size_t handles_len);

    void MapGpuVirtualAddress(D3D12_GPU_VIRTUAL_ADDRESS& address);

    void MapGpuVirtualAddresses(D3D12_GPU_VIRTUAL_ADDRESS* addresses, size_t addresses_len);

    template <typename T>
    T* MapObject(const format::HandleId id)
    {
        return object_mapping::MapObject<T>(id, object_info_table_);
    }

    template <typename T>
    T** MapObjects(HandlePointerDecoder<T*>* handles_pointer, size_t handles_len)
    {
        // This parameter is only referenced by debug builds.
        GFXRECON_UNREFERENCED_PARAMETER(handles_len);

        T** handles = nullptr;

        if (handles_pointer != nullptr)
        {
            // The handle and ID array sizes are expected to be the same for mapping operations.
            assert(handles_len == handles_pointer->GetLength());

            handles = object_mapping::MapObjectArray(handles_pointer, object_info_table_);
        }

        return handles;
    }

    template <typename T>
    void AddObject(const format::HandleId* p_id, T** pp_object)
    {
        object_mapping::AddObject<T>(p_id, pp_object, &object_info_table_);
    }

    template <typename T>
    void AddObject(const format::HandleId* p_id, T** pp_object, DxObjectInfo&& initial_info)
    {
        object_mapping::AddObject<T>(p_id, pp_object, std::move(initial_info), &object_info_table_);
    }

    void RemoveObject(DxObjectInfo* info);

    void CheckReplayResult(const char* call_name, HRESULT capture_result, HRESULT replay_result);

    void* PreProcessExternalObject(uint64_t object_id, format::ApiCallId call_id, const char* call_name);

    void PostProcessExternalObject(
        HRESULT replay_result, void* object, uint64_t* object_id, format::ApiCallId call_id, const char* call_name);

    ULONG OverrideAddRef(DxObjectInfo* replay_object_info, ULONG original_result);

    ULONG OverrideRelease(DxObjectInfo* replay_object_info, ULONG original_result);

    HRESULT OverridePresent(DxObjectInfo* replay_object_info, HRESULT original_result, UINT sync_interval, UINT flags);

    HRESULT OverridePresent1(DxObjectInfo*                                          replay_object_info,
                             HRESULT                                                original_result,
                             UINT                                                   sync_interval,
                             UINT                                                   flags,
                             StructPointerDecoder<Decoded_DXGI_PRESENT_PARAMETERS>* present_parameters);

    HRESULT OverrideCreateSwapChain(DxObjectInfo*                                       replay_object_info,
                                    HRESULT                                             original_result,
                                    DxObjectInfo*                                       device_info,
                                    StructPointerDecoder<Decoded_DXGI_SWAP_CHAIN_DESC>* desc,
                                    HandlePointerDecoder<IDXGISwapChain*>*              swapchain);

    HRESULT
    OverrideCreateSwapChainForHwnd(DxObjectInfo*                                                  replay_object_info,
                                   HRESULT                                                        original_result,
                                   DxObjectInfo*                                                  device_info,
                                   uint64_t                                                       hwnd_id,
                                   StructPointerDecoder<Decoded_DXGI_SWAP_CHAIN_DESC1>*           desc,
                                   StructPointerDecoder<Decoded_DXGI_SWAP_CHAIN_FULLSCREEN_DESC>* full_screen_desc,
                                   DxObjectInfo*                           restrict_to_output_info,
                                   HandlePointerDecoder<IDXGISwapChain1*>* swapchain);

    HRESULT
    OverrideCreateSwapChainForCoreWindow(DxObjectInfo*                                        replay_object_info,
                                         HRESULT                                              original_result,
                                         DxObjectInfo*                                        device_info,
                                         DxObjectInfo*                                        window_info,
                                         StructPointerDecoder<Decoded_DXGI_SWAP_CHAIN_DESC1>* desc,
                                         DxObjectInfo*                                        restrict_to_output_info,
                                         HandlePointerDecoder<IDXGISwapChain1*>*              swapchain);

    HRESULT OverrideCreateSwapChainForComposition(DxObjectInfo* replay_object_info,
                                                  HRESULT       original_result,
                                                  DxObjectInfo* device_info,
                                                  StructPointerDecoder<Decoded_DXGI_SWAP_CHAIN_DESC1>* desc,
                                                  DxObjectInfo*                           restrict_to_output_info,
                                                  HandlePointerDecoder<IDXGISwapChain1*>* swapchain);

    HRESULT
    OverrideCreateDXGIFactory2(HRESULT                      original_result,
                               UINT                         flags,
                               Decoded_GUID                 riid,
                               HandlePointerDecoder<void*>* ppFactory);

    HRESULT OverrideD3D12CreateDevice(HRESULT                      original_result,
                                      DxObjectInfo*                adapter_info,
                                      D3D_FEATURE_LEVEL            minimum_feature_level,
                                      Decoded_GUID                 riid,
                                      HandlePointerDecoder<void*>* device);

    HRESULT OverrideCreateCommandQueue(DxObjectInfo*                                           replay_object_info,
                                       HRESULT                                                 original_result,
                                       StructPointerDecoder<Decoded_D3D12_COMMAND_QUEUE_DESC>* desc,
                                       Decoded_GUID                                            riid,
                                       HandlePointerDecoder<void*>*                            command_queue);

    HRESULT OverrideCreateDescriptorHeap(DxObjectInfo*                                             replay_object_info,
                                         HRESULT                                                   original_result,
                                         StructPointerDecoder<Decoded_D3D12_DESCRIPTOR_HEAP_DESC>* desc,
                                         Decoded_GUID                                              riid,
                                         HandlePointerDecoder<void*>*                              heap);

    HRESULT OverrideCreateCommittedResource(DxObjectInfo*                                        replay_object_info,
                                            HRESULT                                              original_result,
                                            StructPointerDecoder<Decoded_D3D12_HEAP_PROPERTIES>* pHeapProperties,
                                            D3D12_HEAP_FLAGS                                     HeapFlags,
                                            StructPointerDecoder<Decoded_D3D12_RESOURCE_DESC>*   pDesc,
                                            D3D12_RESOURCE_STATES                                InitialResourceState,
                                            StructPointerDecoder<Decoded_D3D12_CLEAR_VALUE>*     pOptimizedClearValue,
                                            Decoded_GUID                                         riid,
                                            HandlePointerDecoder<void*>*                         resource);

    HRESULT OverrideCreateCommittedResource1(DxObjectInfo*                                        replay_object_info,
                                             HRESULT                                              original_result,
                                             StructPointerDecoder<Decoded_D3D12_HEAP_PROPERTIES>* pHeapProperties,
                                             D3D12_HEAP_FLAGS                                     HeapFlags,
                                             StructPointerDecoder<Decoded_D3D12_RESOURCE_DESC>*   pDesc,
                                             D3D12_RESOURCE_STATES                                InitialResourceState,
                                             StructPointerDecoder<Decoded_D3D12_CLEAR_VALUE>*     pOptimizedClearValue,
                                             DxObjectInfo*                protected_session_object_info,
                                             Decoded_GUID                 riid,
                                             HandlePointerDecoder<void*>* resource);

    HRESULT OverrideCreateCommittedResource2(DxObjectInfo*                                        replay_object_info,
                                             HRESULT                                              original_result,
                                             StructPointerDecoder<Decoded_D3D12_HEAP_PROPERTIES>* pHeapProperties,
                                             D3D12_HEAP_FLAGS                                     HeapFlags,
                                             StructPointerDecoder<Decoded_D3D12_RESOURCE_DESC1>*  pDesc,
                                             D3D12_RESOURCE_STATES                                InitialResourceState,
                                             StructPointerDecoder<Decoded_D3D12_CLEAR_VALUE>*     pOptimizedClearValue,
                                             DxObjectInfo*                protected_session_object_info,
                                             Decoded_GUID                 riid,
                                             HandlePointerDecoder<void*>* resource);

    HRESULT OverrideCreateFence(DxObjectInfo*                replay_object_info,
                                HRESULT                      original_result,
                                UINT64                       initial_value,
                                D3D12_FENCE_FLAGS            flags,
                                Decoded_GUID                 riid,
                                HandlePointerDecoder<void*>* fence);

    UINT OverrideGetDescriptorHandleIncrementSize(DxObjectInfo*              replay_object_info,
                                                  UINT                       original_result,
                                                  D3D12_DESCRIPTOR_HEAP_TYPE descriptor_heap_type);

    D3D12_CPU_DESCRIPTOR_HANDLE
    OverrideGetCPUDescriptorHandleForHeapStart(DxObjectInfo*                              replay_object_info,
                                               const Decoded_D3D12_CPU_DESCRIPTOR_HANDLE& original_result);

    D3D12_GPU_DESCRIPTOR_HANDLE
    OverrideGetGPUDescriptorHandleForHeapStart(DxObjectInfo*                              replay_object_info,
                                               const Decoded_D3D12_GPU_DESCRIPTOR_HANDLE& original_result);

    D3D12_GPU_VIRTUAL_ADDRESS OverrideGetGpuVirtualAddress(DxObjectInfo*             replay_object_info,
                                                           D3D12_GPU_VIRTUAL_ADDRESS original_result);

    HRESULT OverrideCreatePipelineLibrary(DxObjectInfo*                replay_object_info,
                                          HRESULT                      original_result,
                                          PointerDecoder<uint8_t>*     library_blob,
                                          SIZE_T                       blob_length,
                                          Decoded_GUID                 riid,
                                          HandlePointerDecoder<void*>* library);

    HRESULT OverrideEnqueueMakeResident(DxObjectInfo*                          replay_object_info,
                                        HRESULT                                original_result,
                                        D3D12_RESIDENCY_FLAGS                  flags,
                                        UINT                                   num_objects,
                                        HandlePointerDecoder<ID3D12Pageable*>* objects,
                                        DxObjectInfo*                          fence_info,
                                        UINT64                                 fence_value);

    HRESULT
    OverrideOpenExistingHeapFromAddress(DxObjectInfo*                replay_object_info,
                                        HRESULT                      original_result,
                                        uint64_t                     allocation_id,
                                        Decoded_GUID                 riid,
                                        HandlePointerDecoder<void*>* heap);

    HRESULT OverrideResourceMap(DxObjectInfo*                              replay_object_info,
                                HRESULT                                    original_result,
                                UINT                                       subresource,
                                StructPointerDecoder<Decoded_D3D12_RANGE>* read_range,
                                PointerDecoder<uint64_t, void*>*           data);

    void OverrideResourceUnmap(DxObjectInfo*                              replay_object_info,
                               UINT                                       subresource,
                               StructPointerDecoder<Decoded_D3D12_RANGE>* written_range);

    HRESULT
    OverrideWriteToSubresource(DxObjectInfo*                            replay_object_info,
                               HRESULT                                  original_result,
                               UINT                                     dst_subresource,
                               StructPointerDecoder<Decoded_D3D12_BOX>* dst_box,
                               uint64_t                                 src_data,
                               UINT                                     src_row_pitch,
                               UINT                                     src_depth_pitch);

    HRESULT
    OverrideReadFromSubresource(DxObjectInfo*                            replay_object_info,
                                HRESULT                                  original_result,
                                uint64_t                                 dst_data,
                                UINT                                     dst_row_pitch,
                                UINT                                     dst_depth_pitch,
                                UINT                                     src_subresource,
                                StructPointerDecoder<Decoded_D3D12_BOX>* src_box);

    void OverrideExecuteCommandLists(DxObjectInfo*                             replay_object_info,
                                     UINT                                      num_command_lists,
                                     HandlePointerDecoder<ID3D12CommandList*>* command_lists);

    HRESULT OverrideCommandQueueSignal(DxObjectInfo* replay_object_info,
                                       HRESULT       original_result,
                                       DxObjectInfo* fence_info,
                                       UINT64        value);

    HRESULT OverrideCommandQueueWait(DxObjectInfo* replay_object_info,
                                     HRESULT       original_result,
                                     DxObjectInfo* fence_info,
                                     UINT64        value);

    UINT64 OverrideGetCompletedValue(DxObjectInfo* replay_object_info, UINT64 original_result);

    HRESULT OverrideSetEventOnCompletion(DxObjectInfo* replay_object_info,
                                         HRESULT       original_result,
                                         UINT64        value,
                                         uint64_t      event_id);

    HRESULT OverrideFenceSignal(DxObjectInfo* replay_object_info, HRESULT original_result, UINT64 value);

    HRESULT OverrideGetBuffer(DxObjectInfo*                replay_object_info,
                              HRESULT                      original_result,
                              UINT                         buffer,
                              Decoded_GUID                 riid,
                              HandlePointerDecoder<void*>* ppSurface);

    HRESULT OverrideResizeBuffers(DxObjectInfo* replay_object_info,
                                  HRESULT       original_result,
                                  UINT          buffer_count,
                                  UINT          width,
                                  UINT          height,
                                  DXGI_FORMAT   new_format,
                                  UINT          flags);

    HRESULT OverrideResizeBuffers1(DxObjectInfo*                    replay_object_info,
                                   HRESULT                          original_result,
                                   UINT                             buffer_count,
                                   UINT                             width,
                                   UINT                             height,
                                   DXGI_FORMAT                      new_format,
                                   UINT                             flags,
                                   PointerDecoder<UINT>*            node_mask,
                                   HandlePointerDecoder<IUnknown*>* present_queue);

    HRESULT OverrideLoadGraphicsPipeline(DxObjectInfo*   replay_object_info,
                                         HRESULT         original_result,
                                         WStringDecoder* name,
                                         StructPointerDecoder<Decoded_D3D12_GRAPHICS_PIPELINE_STATE_DESC>* desc,
                                         Decoded_GUID                                                      riid,
                                         HandlePointerDecoder<void*>*                                      state);

    HRESULT OverrideLoadComputePipeline(DxObjectInfo*   replay_object_info,
                                        HRESULT         original_result,
                                        WStringDecoder* name,
                                        StructPointerDecoder<Decoded_D3D12_COMPUTE_PIPELINE_STATE_DESC>* desc,
                                        Decoded_GUID                                                     riid,
                                        HandlePointerDecoder<void*>*                                     state);

    HRESULT OverrideLoadPipeline(DxObjectInfo*                                                   replay_object_info,
                                 HRESULT                                                         original_result,
                                 WStringDecoder*                                                 name,
                                 StructPointerDecoder<Decoded_D3D12_PIPELINE_STATE_STREAM_DESC>* desc,
                                 Decoded_GUID                                                    riid,
                                 HandlePointerDecoder<void*>*                                    state);

    void* OverrideGetShaderIdentifier(DxObjectInfo*            replay_object_info,
                                      PointerDecoder<uint8_t>* original_result,
                                      WStringDecoder*          pExportName);

    void OverrideEnableDebugLayer(DxObjectInfo* replay_object_info);

    void EnableDebugLayer(ID3D12Debug* dx12_debug);

    void PostPresent(IDXGISwapChain* swapchain);

    void OverrideSetAutoBreadcrumbsEnablement(DxObjectInfo* replay_object_info, D3D12_DRED_ENABLEMENT enablement);

    void SetAutoBreadcrumbsEnablement(ID3D12DeviceRemovedExtendedDataSettings1* dred_settings,
                                      D3D12_DRED_ENABLEMENT                     enablement);

    void OverrideSetBreadcrumbContextEnablement(DxObjectInfo* replay_object_info, D3D12_DRED_ENABLEMENT enablement);

    void SetBreadcrumbContextEnablement(ID3D12DeviceRemovedExtendedDataSettings1* dred_settings,
                                        D3D12_DRED_ENABLEMENT                     enablement);

    void OverrideSetPageFaultEnablement(DxObjectInfo* replay_object_info, D3D12_DRED_ENABLEMENT enablement);

    void SetPageFaultEnablement(ID3D12DeviceRemovedExtendedDataSettings1* dred_settings,
                                D3D12_DRED_ENABLEMENT                     enablement);

    HRESULT OverrideCreateReservedResource(DxObjectInfo*                                      device_object_info,
                                           HRESULT                                            original_result,
                                           StructPointerDecoder<Decoded_D3D12_RESOURCE_DESC>* desc,
                                           D3D12_RESOURCE_STATES                              initial_state,
                                           StructPointerDecoder<Decoded_D3D12_CLEAR_VALUE>*   optimized_clear_value,
                                           Decoded_GUID                                       riid,
                                           HandlePointerDecoder<void*>*                       resource);

    HRESULT OverrideCreateReservedResource1(DxObjectInfo*                                      device_object_info,
                                            HRESULT                                            original_result,
                                            StructPointerDecoder<Decoded_D3D12_RESOURCE_DESC>* desc,
                                            D3D12_RESOURCE_STATES                              initial_state,
                                            StructPointerDecoder<Decoded_D3D12_CLEAR_VALUE>*   optimized_clear_value,
                                            DxObjectInfo*                protected_session_object_info,
                                            Decoded_GUID                 riid,
                                            HandlePointerDecoder<void*>* resource);

    HRESULT OverrideCreateGraphicsPipelineState(DxObjectInfo* device_object_info,
                                                HRESULT       original_result,
                                                StructPointerDecoder<Decoded_D3D12_GRAPHICS_PIPELINE_STATE_DESC>* pDesc,
                                                Decoded_GUID                                                      riid,
                                                HandlePointerDecoder<void*>* pipelineState);

    HRESULT OverrideCreateComputePipelineState(DxObjectInfo* device_object_info,
                                               HRESULT       original_result,
                                               StructPointerDecoder<Decoded_D3D12_COMPUTE_PIPELINE_STATE_DESC>* pDesc,
                                               Decoded_GUID                                                     riid,
                                               HandlePointerDecoder<void*>* pipelineState);

    HRESULT OverrideSetFullscreenState(DxObjectInfo* swapchain_info,
                                       HRESULT       original_result,
                                       BOOL          Fullscreen,
                                       DxObjectInfo* pTarget);

    const Dx12ObjectInfoTable& GetObjectInfoTable() const { return object_info_table_; }

    Dx12ObjectInfoTable& GetObjectInfoTable() { return object_info_table_; }

    DxObjectInfo* GetObjectInfo(format::HandleId id)
    {
        auto entry = object_info_table_.find(id);
        if (entry != object_info_table_.end())
        {
            return &entry->second;
        }

        return nullptr;
    }

    const Dx12DescriptorMap& GetDescriptorMap() const { return descriptor_map_; }

    Dx12DescriptorMap& GetDescriptorMap() { return descriptor_map_; }

    const graphics::Dx12GpuVaMap& GetGpuVaTable() const { return gpu_va_map_; }

    graphics::Dx12GpuVaMap& GetGpuVaTable() { return gpu_va_map_; }

    const graphics::Dx12ShaderIdMap& GetShaderIdTable() const { return shader_id_map_; }

    graphics::Dx12ShaderIdMap& GetShaderIdTable() { return shader_id_map_; }

    void ReplaceWindowedResolution(uint32_t& width, uint32_t& height)
    {
        if (options_.force_windowed)
        {
            width  = options_.windowed_width;
            height = options_.windowed_height;
        }
    }

    void ReplaceWindowedResolution(float& width, float& height)
    {
        if (options_.force_windowed)
        {
            width  = static_cast<float>(options_.windowed_width);
            height = static_cast<float>(options_.windowed_height);
        }
    }

  private:
    void RaiseFatalError(const char* message) const;

    HRESULT
    CreateSwapChainForHwnd(DxObjectInfo*                                                  replay_object_info,
                           HRESULT                                                        original_result,
                           DxObjectInfo*                                                  device_info,
                           uint64_t                                                       hwnd_id,
                           StructPointerDecoder<Decoded_DXGI_SWAP_CHAIN_DESC1>*           desc,
                           StructPointerDecoder<Decoded_DXGI_SWAP_CHAIN_FULLSCREEN_DESC>* full_screen_desc,
                           DxObjectInfo*                                                  restrict_to_output_info,
                           HandlePointerDecoder<IDXGISwapChain1*>*                        swapchain);

    void SetSwapchainInfo(DxObjectInfo* info, Window* window, uint64_t hwnd_id, HWND hwnd, uint32_t image_count);

    void ResetSwapchainImages(DxObjectInfo* info, uint32_t buffer_count, uint32_t width, uint32_t height);

    void ReleaseSwapchainImages(DxgiSwapchainInfo* info);

    void WaitIdle();

    void DestroyObjectExtraInfo(DxObjectInfo* info, bool release_extra_refs);

    void DestroyActiveObjects();

    void DestroyActiveWindows();

    void DestroyActiveEvents();

    void DestroyHeapAllocations();

    void ProcessQueueSignal(DxObjectInfo* queue_info, DxObjectInfo* fence_info, uint64_t value);

    void ProcessQueueWait(DxObjectInfo* queue_info, DxObjectInfo* fence_info, uint64_t value);

    void ProcessFenceSignal(DxObjectInfo* info, uint64_t value);

    void SignalWaitingQueue(DxObjectInfo* queue_info, DxObjectInfo* fence_info, uint64_t value);

    HANDLE GetEventObject(uint64_t event_id, bool reset);

    void ReadDebugMessages();

    void InitializeScreenshotHandler();

    void WaitForFenceEvent(format::HandleId fence_id, HANDLE event);

    void SetDebugMsgFilter(std::vector<DXGI_INFO_QUEUE_MESSAGE_ID> denied_msgs,
                           std::vector<DXGI_INFO_QUEUE_MESSAGE_ID> allowed_msgs);

    // When processing swapchain image state for the trimming state setup, acquire an image, transition it to
    // the expected state, and then call queue present.
    void ProcessSetSwapchainImageStateQueueSubmit(ID3D12CommandQueue* command_queue,
                                                  DxObjectInfo*       swapchain_info,
                                                  uint32_t            current_buffer_index);

    void ApplyResourceInitInfo();

    QueueSyncEventInfo CreateWaitQueueSyncEvent(DxObjectInfo* fence_info, uint64_t value);
    QueueSyncEventInfo CreateSignalQueueSyncEvent(DxObjectInfo* fence_info, uint64_t value);

  private:
    std::unique_ptr<graphics::DX12ImageRenderer> frame_buffer_renderer_;
    Dx12ObjectInfoTable                          object_info_table_;
    std::shared_ptr<application::Application>    application_;
    DxReplayOptions                              options_;
    std::unordered_set<Window*>                  active_windows_;
    std::unordered_map<uint64_t, HWND>           window_handles_;
    std::unordered_map<uint64_t, void*>          mapped_memory_;
    std::unordered_map<uint64_t, void*>          heap_allocations_;
    std::unordered_map<uint64_t, HANDLE>         event_objects_;
    std::function<void(const char*)>             fatal_error_handler_;
    Dx12DescriptorMap                            descriptor_map_;
    graphics::Dx12GpuVaMap                       gpu_va_map_;
    graphics::Dx12ShaderIdMap                    shader_id_map_;
    std::unique_ptr<uint8_t[]>                   debug_message_;
    SIZE_T                                       current_message_length_;
    IDXGIInfoQueue*                              info_queue_;
    bool                                         debug_layer_enabled_;
    bool                                         set_auto_breadcrumbs_enablement_;
    bool                                         set_breadcrumb_context_enablement_;
    bool                                         set_page_fault_enablement_;
    bool                                         loading_trim_state_;
    graphics::FpsInfo*                           fps_info_;

    struct ResourceInitInfo
    {
        ID3D12Resource*                                resource{ nullptr };
        bool                                           try_map_and_copy{ true };
        std::vector<uint8_t>                           data;
        std::vector<uint64_t>                          subresource_offsets;
        std::vector<uint64_t>                          subresource_sizes;
        std::vector<graphics::dx12::ResourceStateInfo> before_states;
        std::vector<graphics::dx12::ResourceStateInfo> after_states;

        // Prefer Reset over creating new ResourceInitInfos in order to reuse the vectors' heap allocations.
        void Reset()
        {
            resource         = nullptr;
            try_map_and_copy = true;
            data.clear();
            subresource_offsets.clear();
            subresource_sizes.clear();
            before_states.clear();
            after_states.clear();
        }
    };
    ResourceInitInfo                                      resource_init_info_;
    std::unique_ptr<graphics::Dx12ResourceDataUtil>       resource_data_util_;
    std::string                                           screenshot_file_prefix_;
    std::unique_ptr<ScreenshotHandlerBase>                screenshot_handler_;
    std::vector<graphics::dx12::ID3D12CommandQueueComPtr> direct_queues_;
};

GFXRECON_END_NAMESPACE(decode)
GFXRECON_END_NAMESPACE(gfxrecon)

#endif // GFXRECON_DECODE_DX12_REPLAY_CONSUMER_BASE_H
