// dear imgui: Renderer Backend for DirectX12
// This needs to be used along with a Platform Backend (e.g. Win32)

// Implemented features:
//  [X] Renderer: User texture binding. Use 'D3D12_GPU_DESCRIPTOR_HANDLE' as texture identifier. Read the FAQ about ImTextureID/ImTextureRef!
//  [X] Renderer: Large meshes support (64k+ vertices) even with 16-bit indices (ImGuiBackendFlags_RendererHasVtxOffset).
//  [X] Renderer: Texture updates support for dynamic font atlas (ImGuiBackendFlags_RendererHasTextures).
//  [X] Renderer: Expose selected render state for draw callbacks to use. Access in '(ImGui_ImplXXXX_RenderState*)GetPlatformIO().Renderer_RenderState'.
//  [X] Renderer: Multi-viewport support (multiple windows). Enable with 'io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable'.

// The aim of imgui_impl_dx12.h/.cpp is to be usable in your engine without any modification.
// IF YOU FEEL YOU NEED TO MAKE ANY CHANGE TO THIS CODE, please share them and your feedback at https://github.com/ocornut/imgui/

// You can use unmodified imgui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire imgui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

#pragma once
#include "imgui.h"      // IMGUI_IMPL_API
#ifndef IMGUI_DISABLE
#include <dxgiformat.h> // DXGI_FORMAT
#include <d3d12.h>      // D3D12_CPU_DESCRIPTOR_HANDLE

// Clang/GCC warnings with -Weverything
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast" // warning: use of old-style cast
#endif

struct IDXGISwapChain3;

// NEM: Windowごとの表示形式とUIの輝度
struct ImGui_ImplDX12_OutputConfig
{
    DXGI_FORMAT SwapChainFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    DXGI_FORMAT UIFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    float UIWhiteScale = 1.0f;
    ImU64 Revision = 0;
};

// Initialization data, for ImGui_ImplDX12_Init()
struct ImGui_ImplDX12_InitInfo
{
    ID3D12Device*               Device;
    ID3D12CommandQueue*         CommandQueue;       // Command queue used for queuing texture uploads.
    int                         NumFramesInFlight;
    DXGI_FORMAT                 RTVFormat;          // RenderTarget format.
    DXGI_FORMAT                 DSVFormat;          // DepthStencilView format.
    void*                       UserData;

    // NEM: 表示方針とHDR合成をEngineへ接続する
    void*                       OutputUserData;
    // 0で解決、1で適用、2で復元する
    bool                        (*WindowOutputFn)(ImGui_ImplDX12_InitInfo*, ImGuiViewport*, IDXGISwapChain3*, ImGui_ImplDX12_OutputConfig*, int operation);
    void                        (*RenderOutputFn)(ImGui_ImplDX12_InitInfo*, ImGuiViewport*, ID3D12GraphicsCommandList*, D3D12_CPU_DESCRIPTOR_HANDLE);
    void                        (*DestroyOutputFn)(ImGui_ImplDX12_InitInfo*, ImGuiViewport*);

    // Allocating SRV descriptors for textures is up to the application, so we provide callbacks.
    // (current version of the backend will only allocate one descriptor, from 1.92 the backend will need to allocate more)
    // NEM: GPU完了まで保持する任意の回収窓口
    // NEM: Device状態を確認する待機窓口
    bool                        (*WaitForGPUFn)(ImGui_ImplDX12_InitInfo* info, ID3D12Fence* fence, UINT64 value, HANDLE event);
    void                        (*ResourceRetireFn)(ImGui_ImplDX12_InitInfo* info, ID3D12Object* resource);
    ID3D12DescriptorHeap*       SrvDescriptorHeap;
    void                        (*SrvDescriptorAllocFn)(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_desc_handle);
    void                        (*SrvDescriptorFreeFn)(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_desc_handle);
#ifndef IMGUI_DISABLE_OBSOLETE_FUNCTIONS
    D3D12_CPU_DESCRIPTOR_HANDLE LegacySingleSrvCpuDescriptor; // To facilitate transition from single descriptor to allocator callback, you may use those.
    D3D12_GPU_DESCRIPTOR_HANDLE LegacySingleSrvGpuDescriptor;
#endif

    ImGui_ImplDX12_InitInfo()   { memset((void*)this, 0, sizeof(*this)); }
};

// Follow "Getting Started" link and check examples/ folder to learn about using backends!
IMGUI_IMPL_API bool     ImGui_ImplDX12_Init(ImGui_ImplDX12_InitInfo* info);
IMGUI_IMPL_API void     ImGui_ImplDX12_Shutdown();
// GPU完了後に実行用フレーム数を切り替える
IMGUI_IMPL_API bool     ImGui_ImplDX12_PrepareFrameCount(int count);
IMGUI_IMPL_API void     ImGui_ImplDX12_ApplyFrameCount();
IMGUI_IMPL_API void     ImGui_ImplDX12_CancelFrameCount();
IMGUI_IMPL_API void     ImGui_ImplDX12_NewFrame();
IMGUI_IMPL_API void     ImGui_ImplDX12_RenderDrawData(ImDrawData* draw_data, ID3D12GraphicsCommandList* graphics_command_list, const ImGui_ImplDX12_OutputConfig* output = nullptr);

#ifndef IMGUI_DISABLE_OBSOLETE_FUNCTIONS
// Legacy initialization API Obsoleted in 1.91.5
// - font_srv_cpu_desc_handle and font_srv_gpu_desc_handle are handles to a single SRV descriptor to use for the internal font texture, they must be in 'srv_descriptor_heap'
// - When we introduced the ImGui_ImplDX12_InitInfo struct we also added a 'ID3D12CommandQueue* CommandQueue' field.
IMGUI_IMPL_API bool     ImGui_ImplDX12_Init(ID3D12Device* device, int num_frames_in_flight, DXGI_FORMAT rtv_format, ID3D12DescriptorHeap* srv_descriptor_heap, D3D12_CPU_DESCRIPTOR_HANDLE font_srv_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE font_srv_gpu_desc_handle);
#endif

// NEM: 描画画像の基準白をUIと分けて設定する
IMGUI_IMPL_API void     ImGui_ImplDX12_SetImageWhiteLevel(float nits);
// NEM: 外部Windowの出力を記録開始前に同期する
IMGUI_IMPL_API void     ImGui_ImplDX12_SyncDisplayOutputs();

// Use if you want to reset your rendering device without losing Dear ImGui state.
IMGUI_IMPL_API bool     ImGui_ImplDX12_CreateDeviceObjects();
IMGUI_IMPL_API void     ImGui_ImplDX12_InvalidateDeviceObjects();

// (Advanced) Use e.g. if you need to precisely control the timing of texture updates (e.g. for staged rendering), by setting ImDrawData::Textures = nullptr to handle this manually.
IMGUI_IMPL_API void     ImGui_ImplDX12_UpdateTexture(ImTextureData* tex);

// [BETA] Selected render state data shared with callbacks.
// This is temporarily stored in GetPlatformIO().Renderer_RenderState during the ImGui_ImplDX12_RenderDrawData() call.
// (Please open an issue if you feel you need access to more data)
struct ImGui_ImplDX12_RenderState
{
    ID3D12Device*               Device;
    ID3D12GraphicsCommandList*  CommandList;
};

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif // #ifndef IMGUI_DISABLE
