#define RIVE_RENDERER_FFI_IMPLEMENTATION
#include "rive_renderer_ffi.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <tuple>
#include <vector>
#include <limits>

#include "rive/renderer/render_context.hpp"
#include "rive/renderer.hpp"
#include "rive/renderer/rive_renderer.hpp"
#include "rive/command_path.hpp"
#include "rive/math/mat2d.hpp"
#include "rive/math/transform_components.hpp"
#include "rive/math/path_types.hpp"
#include "rive/shapes/paint/color.hpp"
#include "rive/shapes/paint/stroke_cap.hpp"
#include "rive/shapes/paint/stroke_join.hpp"
#include "rive/shapes/paint/blend_mode.hpp"
#include "rive/shapes/paint/image_sampler.hpp"
#include "rive/span.hpp"
#if defined(WITH_RIVE_TEXT)
#include "rive/text/utf.hpp"
#include "rive/text/text.hpp"
#include "rive/text_engine.hpp"
#endif

#if defined(_WIN32) && !defined(RIVE_UNREAL)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wrl/client.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <d3dx12.h>
#include "rive/renderer/d3d12/render_context_d3d12_impl.hpp"
#include "rive/renderer/d3d/d3d.hpp"
#endif

#if defined(__APPLE__) && !defined(RIVE_UNREAL)
extern "C"
{
    void* rive_metal_device_new(rive_renderer_capabilities_t* caps);
    void rive_metal_device_release(void* device);
    rive_renderer_status_t rive_metal_context_create(void* device, std::uint32_t width, std::uint32_t height,
                                                     void** out_context,
                                                     std::unique_ptr<rive::gpu::RenderContext>* out_render_context);
    void rive_metal_context_destroy(void* context);
    rive_renderer_status_t rive_metal_context_begin_frame(void* context, rive::gpu::RenderContext* render_context,
                                                          std::uint32_t* width, std::uint32_t* height,
                                                          const rive_renderer_frame_options_t* options, void* surface);
    rive_renderer_status_t rive_metal_context_end_frame(void* context, rive::gpu::RenderContext* render_context,
                                                        void* surface);
    rive_renderer_status_t rive_metal_context_submit(void* context, bool has_surface);
    rive_renderer_status_t rive_metal_surface_create(void* device, void* context,
                                                     const rive_renderer_surface_create_info_metal_layer_t* info,
                                                     void** out_surface);
    void rive_metal_surface_destroy(void* surface);
    rive_renderer_status_t rive_metal_surface_resize(void* surface, std::uint32_t width, std::uint32_t height);
    rive_renderer_status_t rive_metal_surface_present(void* surface, void* context,
                                                      rive::gpu::RenderContext* render_context,
                                                      rive_renderer_present_flags_t flags,
                                                      std::uint32_t present_interval);
}
#endif

#if defined(RIVE_RENDERER_FFI_HAS_VULKAN)
#include <vulkan/vulkan.h>
#include "rive/renderer/vulkan/render_context_vulkan_impl.hpp"
#endif

using namespace std::literals;

namespace
{

    thread_local std::string g_lastError;

    void SetLastError(const char* message)
    {
        g_lastError = message ? message : "";
    }

    extern "C" void rive_renderer_set_last_error(const char* message)
    {
        SetLastError(message);
    }

    void ClearLastError()
    {
        g_lastError.clear();
    }

#if defined(_WIN32) && !defined(RIVE_UNREAL)
    struct D3D12AdapterRecord
    {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        rive_renderer_adapter_desc_t          desc {};
        bool                                  isIntel = false;
    };

    std::mutex                      g_adapterMutex;
    std::vector<D3D12AdapterRecord> g_d3d12Adapters;

    void WideToUtf8(const wchar_t* source, char* dest, std::size_t destSize)
    {
        if (dest == nullptr || destSize == 0)
        {
            return;
        }
        std::memset(dest, 0, destSize);
        if (source == nullptr)
        {
            return;
        }
        const int result =
            WideCharToMultiByte(CP_UTF8, 0, source, -1, dest, static_cast<int>(destSize), nullptr, nullptr);
        if (result == 0)
        {
            // Fallback: copy lower byte values.
            std::size_t i = 0;
            while (source[i] != L'\0' && i < destSize - 1)
            {
                dest[i] = static_cast<char>(source[i] & 0xFF);
                ++i;
            }
            dest[i] = '\0';
        }
    }

    rive_renderer_status_t PopulateD3D12AdaptersLocked()
    {
        g_d3d12Adapters.clear();

        Microsoft::WRL::ComPtr<IDXGIFactory6> factory6;
        HRESULT                               hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory6));
        if (FAILED(hr))
        {
            SetLastError("CreateDXGIFactory1 failed");
            return rive_renderer_status_t::internal_error;
        }

        UINT                                  adapterIndex = 0;
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        while (SUCCEEDED(factory6->EnumAdapterByGpuPreference(adapterIndex, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                              IID_PPV_ARGS(&adapter))))
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                ++adapterIndex;
                continue;
            }

            if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)))
            {
                ++adapterIndex;
                continue;
            }

            D3D12AdapterRecord record;
            record.adapter                     = adapter;
            record.isIntel                     = desc.VendorId == 0x8086;
            record.desc.backend                = rive_renderer_backend_t::d3d12;
            record.desc.backend_padding        = 0;
            record.desc.vendor_id              = static_cast<std::uint16_t>(desc.VendorId);
            record.desc.device_id              = static_cast<std::uint16_t>(desc.DeviceId);
            record.desc.subsys_id              = static_cast<std::uint16_t>(desc.SubSysId);
            record.desc.revision               = static_cast<std::uint16_t>(desc.Revision);
            record.desc.dedicated_video_memory = desc.DedicatedVideoMemory;
            record.desc.shared_system_memory   = desc.SharedSystemMemory;
            record.desc.flags    = static_cast<std::uint32_t>(rive_renderer_feature_flags_t::headless_supported);
            record.desc.reserved = 0;
            std::memset(record.desc.reserved_padding, 0, sizeof(record.desc.reserved_padding));
            WideToUtf8(desc.Description, record.desc.name, RIVE_RENDERER_MAX_ADAPTER_NAME);
            g_d3d12Adapters.emplace_back(std::move(record));
            ++adapterIndex;
        }

        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t EnsureD3D12Adapters()
    {
        std::scoped_lock lock(g_adapterMutex);
        if (g_d3d12Adapters.empty())
        {
            return PopulateD3D12AdaptersLocked();
        }
        return rive_renderer_status_t::ok;
    }

    rive_renderer_feature_flags_t FlagsFromD3DCapabilities(const rive::gpu::D3DCapabilities& caps)
    {
        std::uint32_t bits = static_cast<std::uint32_t>(rive_renderer_feature_flags_t::headless_supported);
        if (caps.supportsRasterizerOrderedViews)
        {
            bits |= static_cast<std::uint32_t>(rive_renderer_feature_flags_t::raster_ordering);
        }
        if (caps.supportsTypedUAVLoadStore)
        {
            bits |= static_cast<std::uint32_t>(rive_renderer_feature_flags_t::atomic_path_rendering);
        }
        if (caps.supportsMin16Precision)
        {
            bits |= static_cast<std::uint32_t>(rive_renderer_feature_flags_t::clockwise_fill);
        }
        if (caps.allowsUAVSlot0WithColorOutput)
        {
            bits |= static_cast<std::uint32_t>(rive_renderer_feature_flags_t::advanced_blend);
        }
        return static_cast<rive_renderer_feature_flags_t>(bits);
    }

    rive::gpu::D3DCapabilities QueryD3DCapabilities(ID3D12Device* device)
    {
        rive::gpu::D3DCapabilities caps;

        D3D12_FEATURE_DATA_D3D12_OPTIONS options {};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))))
        {
            caps.supportsRasterizerOrderedViews = options.ROVsSupported;
            if (options.TypedUAVLoadAdditionalFormats)
            {
                auto supportsTypedUAV = [device](DXGI_FORMAT format)
                {
                    D3D12_FEATURE_DATA_FORMAT_SUPPORT support {};
                    support.Format = format;
                    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support))))
                    {
                        return false;
                    }
                    constexpr UINT loadStoreFlags =
                        D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD | D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;
                    return (support.Support2 & loadStoreFlags) == loadStoreFlags;
                };
                caps.supportsTypedUAVLoadStore =
                    supportsTypedUAV(DXGI_FORMAT_R8G8B8A8_UNORM) && supportsTypedUAV(DXGI_FORMAT_B8G8R8A8_UNORM);
            }
            caps.supportsMin16Precision =
                (options.MinPrecisionSupport & D3D12_SHADER_MIN_PRECISION_SUPPORT_16_BIT) != 0;
        }

        D3D12_FEATURE_DATA_D3D12_OPTIONS3 options3 {};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3, &options3, sizeof(options3))))
        {
            caps.allowsUAVSlot0WithColorOutput =
                options3.WriteBufferImmediateSupportFlags & D3D12_COMMAND_LIST_SUPPORT_FLAG_DIRECT;
        }

        return caps;
    }

    void PopulateCapabilitiesFromD3D12(ID3D12Device* device, rive_renderer_capabilities_t& caps, bool isIntel)
    {
        rive::gpu::D3DCapabilities d3dCaps = QueryD3DCapabilities(device);
        d3dCaps.isIntel                    = isIntel;

        caps.backend                  = rive_renderer_backend_t::d3d12;
        caps.backend_padding          = 0;
        caps.reserved                 = 0;
        caps.feature_flags            = FlagsFromD3DCapabilities(d3dCaps);
        caps.max_buffer_size          = 4ull * 1024ull * 1024ull * 1024ull; // 4 GiB
        caps.max_texture_dimension    = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
        caps.max_texture_array_layers = D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION;
        caps.max_sampler_anisotropy   = 16.0f;
        caps.supports_hdr             = 0;
        caps.supports_presentation    = 1;
        std::memset(caps.reserved_padding, 0, sizeof(caps.reserved_padding));
        std::memset(caps.reserved_tail, 0, sizeof(caps.reserved_tail));
    }

    rive::gpu::RenderContextD3D12Impl* GetD3D12Impl(ContextHandle* context)
    {
        if (context == nullptr || context->renderContext == nullptr)
        {
            return nullptr;
        }
        return context->renderContext->static_impl_cast<rive::gpu::RenderContextD3D12Impl>();
    }

    HRESULT CreateRenderTargetTexture(ID3D12Device* device, std::uint32_t width, std::uint32_t height,
                                      Microsoft::WRL::ComPtr<ID3D12Resource>& outTexture)
    {
        D3D12_CLEAR_VALUE clearValue {};
        clearValue.Format   = DXGI_FORMAT_R8G8B8A8_UNORM;
        clearValue.Color[0] = 0.0f;
        clearValue.Color[1] = 0.0f;
        clearValue.Color[2] = 0.0f;
        clearValue.Color[3] = 0.0f;

        auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        auto desc      = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1, 1, 0,
                                                      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

        return device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PRESENT,
                                               &clearValue, IID_PPV_ARGS(&outTexture));
    }

    rive_renderer_status_t EnsureD3D12RenderTarget(ContextHandle* context)
    {
#if defined(_WIN32) && !defined(RIVE_UNREAL)
        if (context->surface != nullptr)
        {
            return EnsureD3D12SurfaceRenderTarget(context);
        }
#endif
        auto* device = context->device;
        auto* impl   = GetD3D12Impl(context);
        if (device == nullptr || impl == nullptr)
        {
            SetLastError("render context not initialized");
            return rive_renderer_status_t::internal_error;
        }

        const bool needsResize = !context->renderTarget || context->renderTarget->width() != context->width ||
                                 context->renderTarget->height() != context->height;

        if (!needsResize)
        {
            return rive_renderer_status_t::ok;
        }

        Microsoft::WRL::ComPtr<ID3D12Resource> texture;
        HRESULT hr = CreateRenderTargetTexture(device->d3d12Device.Get(), context->width, context->height, texture);
        if (FAILED(hr))
        {
            SetLastError("failed to allocate render target texture");
            return rive_renderer_status_t::out_of_memory;
        }

        auto renderTarget = impl->makeRenderTarget(context->width, context->height);
        renderTarget->setTargetTexture(texture);

        context->renderTargetTexture = texture;
        context->renderTarget        = std::move(renderTarget);
        return rive_renderer_status_t::ok;
    }

    void ReleaseD3D12Context(ContextHandle* context)
    {
        if (context == nullptr)
        {
            return;
        }

        if (context->renderContext)
        {
            context->renderContext->releaseResources();
            context->renderContext.reset();
        }
        context->renderTarget.reset();
        context->renderTargetTexture.Reset();
        context->directCommandList.Reset();
        context->copyCommandList.Reset();
        context->directAllocator.Reset();
        context->copyAllocator.Reset();
        context->directFence.Reset();
        context->copyFence.Reset();
        if (context->fenceEvent != nullptr)
        {
            CloseHandle(context->fenceEvent);
            context->fenceEvent = nullptr;
        }
        context->cpuRenderTarget.reset();
        context->cpuFramebuffer.clear();
        context->cpuFrameRecording  = false;
        context->commandListsClosed = false;
    }

    rive_renderer_status_t InitializeD3D12Context(DeviceHandle* device, ContextHandle* context, std::uint32_t width,
                                                  std::uint32_t height)
    {
        HRESULT hr = device->d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                                 IID_PPV_ARGS(&context->directAllocator));
        if (FAILED(hr))
        {
            SetLastError("CreateCommandAllocator (direct) failed");
            return rive_renderer_status_t::internal_error;
        }

        hr = device->d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY,
                                                         IID_PPV_ARGS(&context->copyAllocator));
        if (FAILED(hr))
        {
            SetLastError("CreateCommandAllocator (copy) failed");
            return rive_renderer_status_t::internal_error;
        }

        hr = device->d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, context->directAllocator.Get(),
                                                    nullptr, IID_PPV_ARGS(&context->directCommandList));
        if (FAILED(hr))
        {
            SetLastError("CreateCommandList (direct) failed");
            return rive_renderer_status_t::internal_error;
        }

        hr = device->d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, context->copyAllocator.Get(),
                                                    nullptr, IID_PPV_ARGS(&context->copyCommandList));
        if (FAILED(hr))
        {
            SetLastError("CreateCommandList (copy) failed");
            return rive_renderer_status_t::internal_error;
        }

        hr = device->d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&context->directFence));
        if (FAILED(hr))
        {
            SetLastError("CreateFence (direct) failed");
            return rive_renderer_status_t::internal_error;
        }

        hr = device->d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&context->copyFence));
        if (FAILED(hr))
        {
            SetLastError("CreateFence (copy) failed");
            return rive_renderer_status_t::internal_error;
        }

        context->fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (context->fenceEvent == nullptr)
        {
            SetLastError("CreateEvent failed");
            return rive_renderer_status_t::internal_error;
        }

        context->width  = width;
        context->height = height;
        return rive_renderer_status_t::ok;
    }
#endif

    struct DeviceHandle
    {
        std::atomic<std::uint32_t>   ref_count {1};
        rive_renderer_backend_t      backend {rive_renderer_backend_t::unknown};
        rive_renderer_capabilities_t capabilities {};
#if defined(_WIN32) && !defined(RIVE_UNREAL)
        Microsoft::WRL::ComPtr<IDXGIAdapter1>      adapter;
        Microsoft::WRL::ComPtr<ID3D12Device>       d3d12Device;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> directQueue;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> copyQueue;
        bool                                       isIntel = false;
#elif defined(__APPLE__) && !defined(RIVE_UNREAL)
        void*                                      metalDevice {nullptr};
#elif defined(RIVE_RENDERER_FFI_HAS_VULKAN)
        VkInstance                                 vkInstance = VK_NULL_HANDLE;
        VkPhysicalDevice                           vkPhysicalDevice = VK_NULL_HANDLE;
        VkDevice                                   vkDevice = VK_NULL_HANDLE;
        rive::gpu::VulkanFeatures                  vkFeatures {};
        PFN_vkGetInstanceProcAddr                  getInstanceProcAddr = nullptr;
        VkQueue                                    graphicsQueue = VK_NULL_HANDLE;
        uint32_t                                   graphicsQueueFamilyIndex = 0;
        VkQueue                                    presentQueue = VK_NULL_HANDLE;
        uint32_t                                   presentQueueFamilyIndex = 0;
#endif
    };

    struct SurfaceHandle;

    struct ContextHandle
    {
        std::atomic<std::uint32_t>                ref_count {1};
        DeviceHandle*                             device {nullptr};
        std::uint32_t                             width {0};
        std::uint32_t                             height {0};
        std::unique_ptr<rive::gpu::RenderContext> renderContext;
#if defined(_WIN32) && !defined(RIVE_UNREAL)
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator>    directAllocator;
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator>    copyAllocator;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> directCommandList;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> copyCommandList;
        Microsoft::WRL::ComPtr<ID3D12Resource>            renderTargetTexture;
        rive::rcp<rive::gpu::RenderTarget>                renderTarget;
        SurfaceHandle*                                    surface {nullptr};
        Microsoft::WRL::ComPtr<ID3D12Fence>               directFence;
        Microsoft::WRL::ComPtr<ID3D12Fence>               copyFence;
        HANDLE                                            fenceEvent {nullptr};
        UINT64                                            fenceValue {0};
#elif defined(__APPLE__) && !defined(RIVE_UNREAL)
        void*           metalContext {nullptr};
        SurfaceHandle*  surface {nullptr};
#endif
        std::unique_ptr<rive::gpu::RenderTarget> cpuRenderTarget;
        std::vector<uint8_t>                     cpuFramebuffer;
        std::uint64_t                            frameCounter {1};
        std::uint64_t                            lastCompletedFrame {0};
        std::uint64_t                            pendingFrameNumber {0};
        bool                                     hasActiveFrame {false};
        bool                                     commandListsClosed {false};
        bool                                     cpuFrameRecording = false;
    };

    DeviceHandle* ToDevice(const rive_renderer_device_t& device)
    {
        return static_cast<DeviceHandle*>(device.handle);
    }

    ContextHandle* ToContext(const rive_renderer_context_t& context)
    {
        return static_cast<ContextHandle*>(context.handle);
    }

    struct PathHandle
    {
        std::atomic<std::uint32_t>  ref_count {1};
        rive::rcp<rive::RenderPath> path;
    };

    struct PaintHandle
    {
        std::atomic<std::uint32_t>   ref_count {1};
        rive::rcp<rive::RenderPaint> paint;
    };

    struct RendererHandle
    {
        std::atomic<std::uint32_t>          ref_count {1};
        ContextHandle*                      context {nullptr};
        std::unique_ptr<rive::RiveRenderer> renderer;
    };

    PathHandle* ToPath(const rive_renderer_path_t& path)
    {
        return static_cast<PathHandle*>(path.handle);
    }

    PaintHandle* ToPaint(const rive_renderer_paint_t& paint)
    {
        return static_cast<PaintHandle*>(paint.handle);
    }

    RendererHandle* ToRenderer(const rive_renderer_renderer_t& renderer)
    {
        return static_cast<RendererHandle*>(renderer.handle);
    }

    struct BufferHandle
    {
        std::atomic<std::uint32_t>    ref_count {1};
        rive_renderer_buffer_type_t   type {rive_renderer_buffer_type_t::vertex};
        rive::rcp<rive::RenderBuffer> buffer;
        std::size_t                   size_in_bytes {0};
        void*                         mapped_ptr {nullptr};
    };

    struct ImageHandle
    {
        std::atomic<std::uint32_t>   ref_count {1};
        rive::rcp<rive::RenderImage> image;
    };

    struct FontHandle
    {
        std::atomic<std::uint32_t> ref_count {1};
        rive::rcp<rive::Font>      font;
    };

    struct ShaderHandle
    {
        std::atomic<std::uint32_t>    ref_count {1};
        rive::rcp<rive::RenderShader> shader;
    };

    BufferHandle* ToBuffer(const rive_renderer_buffer_t& buffer)
    {
        return static_cast<BufferHandle*>(buffer.handle);
    }

    ImageHandle* ToImage(const rive_renderer_image_t& image)
    {
        return static_cast<ImageHandle*>(image.handle);
    }

    FontHandle* ToFont(const rive_renderer_font_t& font)
    {
        return static_cast<FontHandle*>(font.handle);
    }

    ShaderHandle* ToShader(const rive_renderer_shader_t& shader)
    {
        return static_cast<ShaderHandle*>(shader.handle);
    }

    struct FenceHandle
    {
        std::atomic<std::uint32_t> ref_count {1};
        DeviceHandle*              device {nullptr};
#if defined(_WIN32) && !defined(RIVE_UNREAL)
        Microsoft::WRL::ComPtr<ID3D12Fence> fence;
        HANDLE                                   eventHandle {nullptr};
        std::atomic<std::uint64_t>              lastValue {0};
#endif
    };

    FenceHandle* ToFence(const rive_renderer_fence_t& fence)
    {
        return static_cast<FenceHandle*>(fence.handle);
    }

    struct SurfaceHandle
    {
        std::atomic<std::uint32_t>        ref_count {1};
        DeviceHandle*                     device {nullptr};
        ContextHandle*                    context {nullptr};
        rive_renderer_backend_t           backend {rive_renderer_backend_t::unknown};
        std::uint32_t                     width {0};
        std::uint32_t                     height {0};
        std::uint32_t                     buffer_count {0};
        rive_renderer_surface_flags_t     flags {rive_renderer_surface_flags_t::none};
        std::uint32_t                     present_interval {1};
#if defined(_WIN32) && !defined(RIVE_UNREAL)
        void* hwnd {nullptr};
        Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain;
        std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> backBuffers;
        std::vector<rive::rcp<rive::gpu::RenderTarget>>     renderTargets;
        UINT borrowedIndex {std::numeric_limits<UINT>::max()};
        bool supportsTearing {false};
#elif defined(__APPLE__) && !defined(RIVE_UNREAL)
        void* metalSurface {nullptr};
#endif
    };

    SurfaceHandle* ToSurface(const rive_renderer_surface_t& surface)
    {
        return static_cast<SurfaceHandle*>(surface.handle);
    }

#if defined(_WIN32) && !defined(RIVE_UNREAL)
    bool CheckTearingSupport()
    {
        Microsoft::WRL::ComPtr<IDXGIFactory5> factory5;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory5))))
        {
            return false;
        }

        BOOL allowTearing = FALSE;
        if (FAILED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing,
                                                 sizeof(allowTearing))))
        {
            return false;
        }
        return allowTearing == TRUE;
    }

    void ReturnSurfaceRenderTarget(ContextHandle* context)
    {
        if (context == nullptr || context->surface == nullptr)
        {
            return;
        }

        auto* surface = context->surface;
        if (surface->borrowedIndex != std::numeric_limits<UINT>::max() && surface->borrowedIndex <
                                                                        surface->renderTargets.size())
        {
            if (context->renderTarget)
            {
                surface->renderTargets[surface->borrowedIndex] = std::move(context->renderTarget);
            }
            context->renderTargetTexture.Reset();
        }
        else if (context->renderTarget)
        {
            context->renderTarget.reset();
            context->renderTargetTexture.Reset();
        }
        surface->borrowedIndex = std::numeric_limits<UINT>::max();
    }

    rive_renderer_status_t CreateSurfaceRenderTargets(SurfaceHandle* surface, std::uint32_t width,
                                                      std::uint32_t height)
    {
        if (surface == nullptr || surface->context == nullptr || surface->device == nullptr)
        {
            SetLastError("surface context or device is null");
            return rive_renderer_status_t::internal_error;
        }

        auto* context = surface->context;
        auto* impl    = GetD3D12Impl(context);
        if (impl == nullptr)
        {
            SetLastError("render context not initialized");
            return rive_renderer_status_t::internal_error;
        }

        surface->backBuffers.clear();
        surface->renderTargets.clear();

        surface->backBuffers.reserve(surface->buffer_count);
        surface->renderTargets.reserve(surface->buffer_count);

        for (std::uint32_t i = 0; i < surface->buffer_count; ++i)
        {
            Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
            HRESULT hr = surface->swapChain->GetBuffer(i, IID_PPV_ARGS(&buffer));
            if (FAILED(hr))
            {
                SetLastError("swapchain get buffer failed");
                return rive_renderer_status_t::internal_error;
            }

            auto renderTarget = impl->makeRenderTarget(width, height);
            if (!renderTarget)
            {
                SetLastError("makeRenderTarget failed");
                return rive_renderer_status_t::internal_error;
            }
            renderTarget->setTargetTexture(buffer);

            surface->backBuffers.emplace_back(std::move(buffer));
            surface->renderTargets.emplace_back(std::move(renderTarget));
        }

        surface->borrowedIndex = std::numeric_limits<UINT>::max();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t EnsureD3D12SurfaceRenderTarget(ContextHandle* context)
    {
        auto* surface = context->surface;
        if (surface == nullptr || surface->swapChain == nullptr)
        {
            SetLastError("surface not initialized");
            return rive_renderer_status_t::invalid_handle;
        }

        UINT backIndex = surface->swapChain->GetCurrentBackBufferIndex();
        if (backIndex >= surface->renderTargets.size())
        {
            SetLastError("swapchain buffer index out of range");
            return rive_renderer_status_t::internal_error;
        }

        context->width  = surface->width;
        context->height = surface->height;

        if (surface->borrowedIndex == backIndex && context->renderTarget)
        {
            context->renderTargetTexture = surface->backBuffers[backIndex];
            return rive_renderer_status_t::ok;
        }

        if (surface->borrowedIndex != std::numeric_limits<UINT>::max() && context->renderTarget)
        {
            // Return previously borrowed target before switching to a new one.
            ReturnSurfaceRenderTarget(context);
        }

        if (!surface->renderTargets[backIndex])
        {
            auto* impl = GetD3D12Impl(context);
            if (impl == nullptr)
            {
                SetLastError("render context not initialized");
                return rive_renderer_status_t::internal_error;
            }
            auto renderTarget = impl->makeRenderTarget(surface->width, surface->height);
            if (!renderTarget)
            {
                SetLastError("makeRenderTarget failed");
                return rive_renderer_status_t::internal_error;
            }
            renderTarget->setTargetTexture(surface->backBuffers[backIndex]);
            surface->renderTargets[backIndex] = std::move(renderTarget);
        }

        if (!surface->renderTargets[backIndex])
        {
            SetLastError("render target unavailable");
            return rive_renderer_status_t::internal_error;
        }

        context->renderTarget        = std::move(surface->renderTargets[backIndex]);
        context->renderTargetTexture = surface->backBuffers[backIndex];
        surface->borrowedIndex       = backIndex;
        return rive_renderer_status_t::ok;
    }
#endif
#if defined(RIVE_RENDERER_FFI_HAS_VULKAN)
    rive::gpu::VulkanFeatures ConvertVulkanFeatures(const rive_renderer_vulkan_features_t& features)
    {
        rive::gpu::VulkanFeatures out {};
        out.apiVersion = features.api_version;
        out.independentBlend = features.independent_blend != 0;
        out.fillModeNonSolid = features.fill_mode_non_solid != 0;
        out.fragmentStoresAndAtomics = features.fragment_stores_and_atomics != 0;
        out.shaderClipDistance = features.shader_clip_distance != 0;
        out.rasterizationOrderColorAttachmentAccess =
            features.rasterization_order_color_attachment_access != 0;
        out.fragmentShaderPixelInterlock = features.fragment_shader_pixel_interlock != 0;
        out.VK_KHR_portability_subset = features.portability_subset != 0;
        return out;
    }
#endif
    bool ConvertFillRule(rive_renderer_fill_rule_t value, rive::FillRule* out)
    {
        switch (value)
        {
        case rive_renderer_fill_rule_t::non_zero:
            *out = rive::FillRule::nonZero;
            return true;
        case rive_renderer_fill_rule_t::even_odd:
            *out = rive::FillRule::evenOdd;
            return true;
        case rive_renderer_fill_rule_t::clockwise:
            *out = rive::FillRule::clockwise;
            return true;
        }
        return false;
    }

    bool ConvertPaintStyle(rive_renderer_paint_style_t value, rive::RenderPaintStyle* out)
    {
        switch (value)
        {
        case rive_renderer_paint_style_t::fill:
            *out = rive::RenderPaintStyle::fill;
            return true;
        case rive_renderer_paint_style_t::stroke:
            *out = rive::RenderPaintStyle::stroke;
            return true;
        }
        return false;
    }

    bool ConvertStrokeCap(rive_renderer_stroke_cap_t value, rive::StrokeCap* out)
    {
        switch (value)
        {
        case rive_renderer_stroke_cap_t::butt:
            *out = rive::StrokeCap::butt;
            return true;
        case rive_renderer_stroke_cap_t::round:
            *out = rive::StrokeCap::round;
            return true;
        case rive_renderer_stroke_cap_t::square:
            *out = rive::StrokeCap::square;
            return true;
        }
        return false;
    }

    bool ConvertStrokeJoin(rive_renderer_stroke_join_t value, rive::StrokeJoin* out)
    {
        switch (value)
        {
        case rive_renderer_stroke_join_t::miter:
            *out = rive::StrokeJoin::miter;
            return true;
        case rive_renderer_stroke_join_t::round:
            *out = rive::StrokeJoin::round;
            return true;
        case rive_renderer_stroke_join_t::bevel:
            *out = rive::StrokeJoin::bevel;
            return true;
        }
        return false;
    }

    bool ConvertBlendMode(rive_renderer_blend_mode_t value, rive::BlendMode* out)
    {
        switch (value)
        {
        case rive_renderer_blend_mode_t::src_over:
            *out = rive::BlendMode::srcOver;
            return true;
        case rive_renderer_blend_mode_t::screen:
            *out = rive::BlendMode::screen;
            return true;
        case rive_renderer_blend_mode_t::overlay:
            *out = rive::BlendMode::overlay;
            return true;
        case rive_renderer_blend_mode_t::darken:
            *out = rive::BlendMode::darken;
            return true;
        case rive_renderer_blend_mode_t::lighten:
            *out = rive::BlendMode::lighten;
            return true;
        case rive_renderer_blend_mode_t::color_dodge:
            *out = rive::BlendMode::colorDodge;
            return true;
        case rive_renderer_blend_mode_t::color_burn:
            *out = rive::BlendMode::colorBurn;
            return true;
        case rive_renderer_blend_mode_t::hard_light:
            *out = rive::BlendMode::hardLight;
            return true;
        case rive_renderer_blend_mode_t::soft_light:
            *out = rive::BlendMode::softLight;
            return true;
        case rive_renderer_blend_mode_t::difference:
            *out = rive::BlendMode::difference;
            return true;
        case rive_renderer_blend_mode_t::exclusion:
            *out = rive::BlendMode::exclusion;
            return true;
        case rive_renderer_blend_mode_t::multiply:
            *out = rive::BlendMode::multiply;
            return true;
        case rive_renderer_blend_mode_t::hue:
            *out = rive::BlendMode::hue;
            return true;
        case rive_renderer_blend_mode_t::saturation:
            *out = rive::BlendMode::saturation;
            return true;
        case rive_renderer_blend_mode_t::color:
            *out = rive::BlendMode::color;
            return true;
        case rive_renderer_blend_mode_t::luminosity:
            *out = rive::BlendMode::luminosity;
            return true;
        }
        return false;
    }

    bool ConvertBufferType(rive_renderer_buffer_type_t value, rive::RenderBufferType* out)
    {
        switch (value)
        {
        case rive_renderer_buffer_type_t::index:
            *out = rive::RenderBufferType::index;
            return true;
        case rive_renderer_buffer_type_t::vertex:
            *out = rive::RenderBufferType::vertex;
            return true;
        }
        return false;
    }

    rive::RenderBufferFlags ConvertBufferFlags(rive_renderer_buffer_flags_t value)
    {
        rive::RenderBufferFlags flags = rive::RenderBufferFlags::none;
        const std::uint32_t     bits  = static_cast<std::uint32_t>(value);
        if ((bits & static_cast<std::uint32_t>(rive_renderer_buffer_flags_t::mapped_once_at_initialization)) != 0)
        {
            flags |= rive::RenderBufferFlags::mappedOnceAtInitialization;
        }
        return flags;
    }

    rive::ImageSampler ConvertImageSampler(const rive_renderer_image_sampler_t* sampler)
    {
        if (sampler == nullptr)
        {
            return rive::ImageSampler::LinearClamp();
        }

        rive::ImageSampler result;
        result.wrapX  = static_cast<rive::ImageWrap>(static_cast<std::uint8_t>(sampler->wrap_x));
        result.wrapY  = static_cast<rive::ImageWrap>(static_cast<std::uint8_t>(sampler->wrap_y));
        result.filter = static_cast<rive::ImageFilter>(static_cast<std::uint8_t>(sampler->filter));
        return result;
    }

#if defined(WITH_RIVE_TEXT)
    bool ConvertTextAlign(rive_renderer_text_align_t value, rive::TextAlign* out)
    {
        switch (value)
        {
        case rive_renderer_text_align_t::left:
            *out = rive::TextAlign::left;
            return true;
        case rive_renderer_text_align_t::right:
            *out = rive::TextAlign::right;
            return true;
        case rive_renderer_text_align_t::center:
            *out = rive::TextAlign::center;
            return true;
        }
        return false;
    }

    bool ConvertTextWrap(rive_renderer_text_wrap_t value, rive::TextWrap* out)
    {
        switch (value)
        {
        case rive_renderer_text_wrap_t::wrap:
            *out = rive::TextWrap::wrap;
            return true;
        case rive_renderer_text_wrap_t::no_wrap:
            *out = rive::TextWrap::noWrap;
            return true;
        }
        return false;
    }

    std::uint8_t DirectionLevelFromStyle(rive_renderer_text_direction_t value)
    {
        switch (value)
        {
        case rive_renderer_text_direction_t::rtl:
            return 1;
        case rive_renderer_text_direction_t::ltr:
        case rive_renderer_text_direction_t::automatic:
        default:
            return 0;
        }
    }
#endif
    rive::Mat2D ToMat2D(const rive_renderer_mat2d_t* mat)
    {
        if (mat == nullptr)
        {
            return rive::Mat2D();
        }
        return rive::Mat2D(mat->xx, mat->xy, mat->yx, mat->yy, mat->tx, mat->ty);
    }

    rive_renderer_adapter_desc_t MakeNullAdapter()
    {
        rive_renderer_adapter_desc_t desc {};
        desc.backend                = rive_renderer_backend_t::null;
        desc.backend_padding        = 0;
        desc.vendor_id              = 0xffff;
        desc.device_id              = 0xffff;
        desc.subsys_id              = 0;
        desc.revision               = 1;
        desc.dedicated_video_memory = 0;
        desc.shared_system_memory   = 0;
        desc.flags                  = static_cast<std::uint32_t>(rive_renderer_feature_flags_t::headless_supported);
        desc.reserved               = 0;
        const char name[]           = "Null Renderer";
        std::memcpy(desc.name, name, sizeof(name));
        std::memset(desc.reserved_padding, 0, sizeof(desc.reserved_padding));
        return desc;
    }

#if defined(__APPLE__) && !defined(RIVE_UNREAL)
    rive_renderer_adapter_desc_t MakeMetalAdapter()
    {
        rive_renderer_adapter_desc_t desc {};
        desc.backend                = rive_renderer_backend_t::metal;
        desc.backend_padding        = 0;
        desc.vendor_id              = 0;
        desc.device_id              = 0;
        desc.subsys_id              = 0;
        desc.revision               = 1;
        desc.dedicated_video_memory = 0;
        desc.shared_system_memory   = 0;
        desc.flags                  = static_cast<std::uint32_t>(rive_renderer_feature_flags_t::headless_supported);
        desc.reserved               = 0;
        const char name[]           = "Metal Default Device";
        std::memcpy(desc.name, name, sizeof(name));
        std::memset(desc.reserved_padding, 0, sizeof(desc.reserved_padding));
        return desc;
    }
#endif

    bool ValidateContextSize(std::uint32_t width, std::uint32_t height)
    {
        return width > 0 && height > 0;
    }

} // namespace

extern "C"
{

    rive_renderer_status_t rive_renderer_enumerate_adapters(rive_renderer_adapter_desc_t* adapters,
                                                            std::size_t capacity, std::size_t* count)
    {
#if defined(_WIN32) && !defined(RIVE_UNREAL)
        if (count == nullptr)
        {
            SetLastError("count pointer is null");
            return rive_renderer_status_t::null_pointer;
        }

        auto status = EnsureD3D12Adapters();
        if (status != rive_renderer_status_t::ok)
        {
            return status;
        }

        std::scoped_lock  lock(g_adapterMutex);
        const std::size_t adapterCount = g_d3d12Adapters.size();
        const std::size_t total        = adapterCount + 1; // include null adapter
        *count                         = total;

        if (adapters != nullptr && capacity > 0)
        {
            const std::size_t to_copy = std::min(capacity, adapterCount);
            for (std::size_t i = 0; i < to_copy; ++i)
            {
                adapters[i] = g_d3d12Adapters[i].desc;
            }
            if (capacity > to_copy)
            {
                adapters[to_copy] = MakeNullAdapter();
            }
        }

        ClearLastError();
        return rive_renderer_status_t::ok;
#else
        if (count == nullptr)
        {
            SetLastError("count pointer is null");
            return rive_renderer_status_t::null_pointer;
        }

        const rive_renderer_adapter_desc_t candidates[] = {
#if defined(__APPLE__) && !defined(RIVE_UNREAL)
            MakeMetalAdapter(),
#endif
            MakeNullAdapter()};
        const std::size_t                  total        = sizeof(candidates) / sizeof(candidates[0]);
        *count                                          = total;

        if (adapters != nullptr && capacity > 0)
        {
            const std::size_t to_copy = capacity < total ? capacity : total;
            std::memcpy(adapters, candidates, sizeof(rive_renderer_adapter_desc_t) * to_copy);
        }

        ClearLastError();
        return rive_renderer_status_t::ok;
#endif
    }

    rive_renderer_status_t rive_renderer_device_create(const rive_renderer_device_create_info_t* info,
                                                       rive_renderer_device_t*                   out_device)
    {
#if defined(_WIN32) && !defined(RIVE_UNREAL)
        if (info != nullptr && info->backend == rive_renderer_backend_t::d3d12)
        {
            auto ensureStatus = EnsureD3D12Adapters();
            if (ensureStatus != rive_renderer_status_t::ok)
            {
                return ensureStatus;
            }

            std::scoped_lock lock(g_adapterMutex);
            if (info->adapter_index >= g_d3d12Adapters.size())
            {
                SetLastError("adapter index out of range");
                return rive_renderer_status_t::invalid_parameter;
            }

            auto& record = g_d3d12Adapters[info->adapter_index];

            Microsoft::WRL::ComPtr<ID3D12Device> device;
            HRESULT hr = D3D12CreateDevice(record.adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
            if (FAILED(hr))
            {
                SetLastError("D3D12CreateDevice failed");
                return rive_renderer_status_t::unsupported;
            }

            D3D12_COMMAND_QUEUE_DESC queueDesc {};
            queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            Microsoft::WRL::ComPtr<ID3D12CommandQueue> directQueue;
            hr = device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&directQueue));
            if (FAILED(hr))
            {
                SetLastError("Failed to create direct command queue");
                return rive_renderer_status_t::internal_error;
            }

            queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
            Microsoft::WRL::ComPtr<ID3D12CommandQueue> copyQueue;
            hr = device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&copyQueue));
            if (FAILED(hr))
            {
                SetLastError("Failed to create copy command queue");
                return rive_renderer_status_t::internal_error;
            }

            auto* handle = new (std::nothrow) DeviceHandle();
            if (handle == nullptr)
            {
                SetLastError("allocation failed");
                return rive_renderer_status_t::out_of_memory;
            }

            handle->backend     = rive_renderer_backend_t::d3d12;
            handle->adapter     = record.adapter;
            handle->d3d12Device = device;
            handle->directQueue = directQueue;
            handle->copyQueue   = copyQueue;
            handle->isIntel     = record.isIntel;
            PopulateCapabilitiesFromD3D12(device.Get(), handle->capabilities, record.isIntel);

            out_device->handle = handle;
            ClearLastError();
            return rive_renderer_status_t::ok;
        }
#endif

        if (info != nullptr && info->backend == rive_renderer_backend_t::metal)
        {
#if defined(__APPLE__) && !defined(RIVE_UNREAL)
            auto* handle = new (std::nothrow) DeviceHandle();
            if (handle == nullptr)
            {
                SetLastError("allocation failed");
                return rive_renderer_status_t::out_of_memory;
            }

            rive_renderer_capabilities_t caps {};
            void* metalDevice = rive_metal_device_new(&caps);
            if (metalDevice == nullptr)
            {
                delete handle;
                if (g_lastError.empty())
                {
                    SetLastError("Metal device creation failed");
                }
                return rive_renderer_status_t::internal_error;
            }

            handle->backend      = rive_renderer_backend_t::metal;
            handle->metalDevice  = metalDevice;
            handle->capabilities = caps;

            out_device->handle = handle;
            ClearLastError();
            return rive_renderer_status_t::ok;
#else
            SetLastError("requested backend is not yet implemented");
            return rive_renderer_status_t::unsupported;
#endif
        }

        if (info == nullptr || out_device == nullptr)
        {
            SetLastError("device_create received null pointer");
            return rive_renderer_status_t::null_pointer;
        }

        if (info->backend == rive_renderer_backend_t::unknown)
        {
            SetLastError("backend must be specified");
            return rive_renderer_status_t::invalid_parameter;
        }

        if (info->backend != rive_renderer_backend_t::null)
        {
            SetLastError("requested backend is not yet implemented");
            return rive_renderer_status_t::unsupported;
        }

        auto* device = new (std::nothrow) DeviceHandle();
        if (device == nullptr)
        {
            SetLastError("allocation failed");
            return rive_renderer_status_t::out_of_memory;
        }

        device->backend                               = info->backend;
        device->capabilities.backend                  = info->backend;
        device->capabilities.feature_flags            = rive_renderer_feature_flags_t::headless_supported;
        device->capabilities.max_buffer_size          = 256ull * 1024ull * 1024ull;
        device->capabilities.max_texture_dimension    = 4096;
        device->capabilities.max_texture_array_layers = 1;
        device->capabilities.max_sampler_anisotropy   = 1.0f;
        device->capabilities.supports_hdr             = 0;
        device->capabilities.supports_presentation    = 0;

        out_device->handle = device;
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_device_create_vulkan(const rive_renderer_device_create_info_vulkan_t* info,
                                                              rive_renderer_device_t* out_device)
    {
        if (out_device == nullptr)
        {
            SetLastError("device output pointer is null");
            return rive_renderer_status_t::null_pointer;
        }

        out_device->handle = nullptr;

        if (info == nullptr)
        {
            SetLastError("device create info is null");
            return rive_renderer_status_t::null_pointer;
        }

#if defined(RIVE_RENDERER_FFI_HAS_VULKAN)
        if (info->instance == nullptr || info->physical_device == nullptr || info->device == nullptr ||
            info->graphics_queue == nullptr)
        {
            SetLastError("Vulkan device create info is missing required handles");
            return rive_renderer_status_t::invalid_parameter;
        }

        auto* handle = new (std::nothrow) DeviceHandle();
        if (handle == nullptr)
        {
            SetLastError("allocation failed");
            return rive_renderer_status_t::out_of_memory;
        }

        handle->backend                   = rive_renderer_backend_t::vulkan;
        handle->vkInstance                = reinterpret_cast<VkInstance>(info->instance);
        handle->vkPhysicalDevice          = reinterpret_cast<VkPhysicalDevice>(info->physical_device);
        handle->vkDevice                  = reinterpret_cast<VkDevice>(info->device);
        handle->graphicsQueue             = reinterpret_cast<VkQueue>(info->graphics_queue);
        handle->graphicsQueueFamilyIndex  = info->graphics_queue_family_index;
        handle->presentQueue              = info->present_queue != nullptr
                                                ? reinterpret_cast<VkQueue>(info->present_queue)
                                                : handle->graphicsQueue;
        handle->presentQueueFamilyIndex =
            info->present_queue != nullptr ? info->present_queue_family_index : handle->graphicsQueueFamilyIndex;
        handle->getInstanceProcAddr =
            reinterpret_cast<PFN_vkGetInstanceProcAddr>(info->get_instance_proc_addr);
        handle->vkFeatures = ConvertVulkanFeatures(info->features);

        handle->capabilities.backend = rive_renderer_backend_t::vulkan;
        auto featureFlags =
            static_cast<std::uint32_t>(rive_renderer_feature_flags_t::headless_supported);
        auto setFlag = [&](rive_renderer_feature_flags_t flag)
        {
            featureFlags |= static_cast<std::uint32_t>(flag);
        };
        if (handle->vkFeatures.fragmentStoresAndAtomics)
        {
            setFlag(rive_renderer_feature_flags_t::atomic_path_rendering);
        }
        if (handle->vkFeatures.rasterizationOrderColorAttachmentAccess ||
            handle->vkFeatures.fragmentShaderPixelInterlock)
        {
            setFlag(rive_renderer_feature_flags_t::raster_ordering);
        }
        handle->capabilities.feature_flags =
            static_cast<rive_renderer_feature_flags_t>(featureFlags);
        handle->capabilities.max_buffer_size          = 0;
        handle->capabilities.max_texture_dimension    = 0;
        handle->capabilities.max_texture_array_layers = 0;
        handle->capabilities.max_sampler_anisotropy   = 1.0f;
        handle->capabilities.supports_hdr             = 0;
        handle->capabilities.supports_presentation =
            handle->presentQueue != VK_NULL_HANDLE ? 1 : 0;

        out_device->handle = handle;
        ClearLastError();
        return rive_renderer_status_t::ok;
#else
        (void)info;
        SetLastError("Vulkan backend is not available in this build");
        return rive_renderer_status_t::unsupported;
#endif
    }

    rive_renderer_status_t rive_renderer_device_retain(rive_renderer_device_t device)
    {
        auto* handle = ToDevice(device);
        if (handle == nullptr)
        {
            SetLastError("device handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

        handle->ref_count.fetch_add(1, std::memory_order_relaxed);
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_device_release(rive_renderer_device_t device)
    {
        auto* handle = ToDevice(device);
        if (handle == nullptr)
        {
            SetLastError("device handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

        const std::uint32_t previous = handle->ref_count.fetch_sub(1, std::memory_order_acq_rel);
        if (previous == 0)
        {
            SetLastError("device handle refcount underflow");
            return rive_renderer_status_t::internal_error;
        }

        if (previous == 1)
        {
#if defined(_WIN32) && !defined(RIVE_UNREAL)
            handle->adapter.Reset();
            handle->d3d12Device.Reset();
            handle->directQueue.Reset();
            handle->copyQueue.Reset();
#elif defined(__APPLE__) && !defined(RIVE_UNREAL)
            if (handle->metalDevice != nullptr)
            {
                rive_metal_device_release(handle->metalDevice);
                handle->metalDevice = nullptr;
            }
#elif defined(RIVE_RENDERER_FFI_HAS_VULKAN)
            handle->vkInstance               = VK_NULL_HANDLE;
            handle->vkPhysicalDevice         = VK_NULL_HANDLE;
            handle->vkDevice                 = VK_NULL_HANDLE;
            handle->graphicsQueue            = VK_NULL_HANDLE;
            handle->presentQueue             = VK_NULL_HANDLE;
            handle->graphicsQueueFamilyIndex = 0;
            handle->presentQueueFamilyIndex  = 0;
            handle->getInstanceProcAddr      = nullptr;
#endif
            delete handle;
        }

        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_device_capabilities(rive_renderer_device_t        device,
                                                             rive_renderer_capabilities_t* out_capabilities)
    {
        if (out_capabilities == nullptr)
        {
            SetLastError("capabilities pointer is null");
            return rive_renderer_status_t::null_pointer;
        }

        auto* handle = ToDevice(device);
        if (handle == nullptr)
        {
            SetLastError("device handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

        *out_capabilities = handle->capabilities;
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_context_create(rive_renderer_device_t device, std::uint32_t width,
                                                        std::uint32_t height, rive_renderer_context_t* out_context)
    {
        if (out_context == nullptr)
        {
            SetLastError("context output pointer is null");
            return rive_renderer_status_t::null_pointer;
        }

        auto* device_handle = ToDevice(device);
        if (device_handle == nullptr)
        {
            SetLastError("device handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

#if defined(_WIN32) && !defined(RIVE_UNREAL)
        if (device_handle->backend == rive_renderer_backend_t::d3d12)
        {
            if (!ValidateContextSize(width, height))
            {
                SetLastError("context dimensions must be non-zero");
                return rive_renderer_status_t::invalid_parameter;
            }

            auto* contextHandle = new (std::nothrow) ContextHandle();
            if (contextHandle == nullptr)
            {
                SetLastError("allocation failed");
                return rive_renderer_status_t::out_of_memory;
            }

            contextHandle->device = device_handle;

            auto initStatus = InitializeD3D12Context(device_handle, contextHandle, width, height);
            if (initStatus != rive_renderer_status_t::ok)
            {
                ReleaseD3D12Context(contextHandle);
                delete contextHandle;
                return initStatus;
            }

            rive::gpu::D3DContextOptions contextOptions;
            contextOptions.isIntel = device_handle->isIntel;

            auto renderContext = rive::gpu::RenderContextD3D12Impl::MakeContext(
                device_handle->d3d12Device, contextHandle->copyCommandList.Get(), contextOptions);
            if (!renderContext)
            {
                ReleaseD3D12Context(contextHandle);
                delete contextHandle;
                SetLastError("RenderContextD3D12Impl::MakeContext failed");
                return rive_renderer_status_t::internal_error;
            }

            contextHandle->renderContext = std::move(renderContext);

            HRESULT hr = contextHandle->copyCommandList->Close();
            if (FAILED(hr))
            {
                ReleaseD3D12Context(contextHandle);
                delete contextHandle;
                SetLastError("copy command list close failed");
                return rive_renderer_status_t::internal_error;
            }

            ID3D12CommandList* copyLists[] = {contextHandle->copyCommandList.Get()};
            device_handle->copyQueue->ExecuteCommandLists(1, copyLists);

            contextHandle->fenceValue = 1;
            hr = device_handle->copyQueue->Signal(contextHandle->copyFence.Get(), contextHandle->fenceValue);
            if (FAILED(hr))
            {
                ReleaseD3D12Context(contextHandle);
                delete contextHandle;
                SetLastError("Queue signal failed");
                return rive_renderer_status_t::internal_error;
            }

            hr = contextHandle->copyFence->SetEventOnCompletion(contextHandle->fenceValue, contextHandle->fenceEvent);
            if (FAILED(hr))
            {
                ReleaseD3D12Context(contextHandle);
                delete contextHandle;
                SetLastError("SetEventOnCompletion failed");
                return rive_renderer_status_t::internal_error;
            }
            WaitForSingleObject(contextHandle->fenceEvent, INFINITE);

            contextHandle->copyAllocator->Reset();
            contextHandle->directAllocator->Reset();
            contextHandle->copyCommandList->Reset(contextHandle->copyAllocator.Get(), nullptr);
            contextHandle->directCommandList->Reset(contextHandle->directAllocator.Get(), nullptr);
            contextHandle->copyCommandList->Close();
            contextHandle->directCommandList->Close();

            auto targetStatus = EnsureD3D12RenderTarget(contextHandle);
            if (targetStatus != rive_renderer_status_t::ok)
            {
                ReleaseD3D12Context(contextHandle);
                delete contextHandle;
                return targetStatus;
            }

            device_handle->ref_count.fetch_add(1, std::memory_order_relaxed);

            out_context->handle = contextHandle;
            ClearLastError();
            return rive_renderer_status_t::ok;
        }
#endif

#if defined(__APPLE__) && !defined(RIVE_UNREAL)
        if (device_handle->backend == rive_renderer_backend_t::metal)
        {
            if (!ValidateContextSize(width, height))
            {
                SetLastError("context dimensions must be non-zero");
                return rive_renderer_status_t::invalid_parameter;
            }

            auto* contextHandle = new (std::nothrow) ContextHandle();
            if (contextHandle == nullptr)
            {
                SetLastError("allocation failed");
                return rive_renderer_status_t::out_of_memory;
            }

            contextHandle->device = device_handle;

            std::unique_ptr<rive::gpu::RenderContext> renderContext;
            void*                                      metalContext = nullptr;
            auto status = rive_metal_context_create(device_handle->metalDevice, width, height, &metalContext,
                                                    &renderContext);
            if (status != rive_renderer_status_t::ok)
            {
                delete contextHandle;
                if (g_lastError.empty())
                {
                    SetLastError("Metal context creation failed");
                }
                return status;
            }

            contextHandle->renderContext = std::move(renderContext);
            contextHandle->metalContext  = metalContext;
            contextHandle->width         = width;
            contextHandle->height        = height;

            device_handle->ref_count.fetch_add(1, std::memory_order_relaxed);

            out_context->handle = contextHandle;
            ClearLastError();
            return rive_renderer_status_t::ok;
        }
#endif

#if defined(RIVE_RENDERER_FFI_HAS_VULKAN)
        if (device_handle->backend == rive_renderer_backend_t::vulkan)
        {
            if (!ValidateContextSize(width, height))
            {
                SetLastError("context dimensions must be non-zero");
                return rive_renderer_status_t::invalid_parameter;
            }

            auto* contextHandle = new (std::nothrow) ContextHandle();
            if (contextHandle == nullptr)
            {
                SetLastError("allocation failed");
                return rive_renderer_status_t::out_of_memory;
            }

            contextHandle->device = device_handle;
            contextHandle->width  = width;
            contextHandle->height = height;

            rive::gpu::RenderContextVulkanImpl::ContextOptions options {};
            auto renderContext = rive::gpu::RenderContextVulkanImpl::MakeContext(
                device_handle->vkInstance,
                device_handle->vkPhysicalDevice,
                device_handle->vkDevice,
                device_handle->vkFeatures,
                device_handle->getInstanceProcAddr,
                options);
            if (!renderContext)
            {
                delete contextHandle;
                SetLastError("RenderContextVulkanImpl::MakeContext failed");
                return rive_renderer_status_t::internal_error;
            }

            contextHandle->renderContext = std::move(renderContext);

            device_handle->ref_count.fetch_add(1, std::memory_order_relaxed);

            out_context->handle = contextHandle;
            ClearLastError();
            return rive_renderer_status_t::ok;
        }
#endif

        if (!ValidateContextSize(width, height))
        {
            SetLastError("context dimensions must be non-zero");
            return rive_renderer_status_t::invalid_parameter;
        }

        auto* context = new (std::nothrow) ContextHandle();
        if (context == nullptr)
        {
            SetLastError("allocation failed");
            return rive_renderer_status_t::out_of_memory;
        }

        context->device        = device_handle;
        context->width         = width;
        context->height        = height;
        context->renderContext = nullptr;
        context->cpuFramebuffer.assign(static_cast<size_t>(width) * height * 4, 0);
        context->cpuRenderTarget.reset();
        context->cpuFrameRecording  = false;
        context->commandListsClosed = false;

        device_handle->ref_count.fetch_add(1, std::memory_order_relaxed);

        out_context->handle = context;
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_context_retain(rive_renderer_context_t context)
    {
        auto* handle = ToContext(context);
        if (handle == nullptr)
        {
            SetLastError("context handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

        handle->ref_count.fetch_add(1, std::memory_order_relaxed);
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_context_release(rive_renderer_context_t context)
    {
        auto* handle = ToContext(context);
        if (handle == nullptr)
        {
            SetLastError("context handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

        if (handle->surface != nullptr)
        {
            SetLastError("context has an active surface; release the surface before destroying the context");
            return rive_renderer_status_t::invalid_parameter;
        }

        const std::uint32_t previous = handle->ref_count.fetch_sub(1, std::memory_order_acq_rel);
        if (previous == 0)
        {
            SetLastError("context handle refcount underflow");
            return rive_renderer_status_t::internal_error;
        }

        if (previous == 1)
        {
#if defined(_WIN32) && !defined(RIVE_UNREAL)
            ReturnSurfaceRenderTarget(handle);
#elif defined(__APPLE__) && !defined(RIVE_UNREAL)
            if (handle->metalContext != nullptr)
            {
                rive_metal_context_destroy(handle->metalContext);
                handle->metalContext = nullptr;
            }
#endif
            if (handle->device != nullptr)
            {
                DeviceHandle*       device      = handle->device;
                const std::uint32_t device_prev = device->ref_count.fetch_sub(1, std::memory_order_acq_rel);
                if (device_prev == 1)
                {
#if defined(_WIN32) && !defined(RIVE_UNREAL)
                    device->adapter.Reset();
                    device->d3d12Device.Reset();
                    device->directQueue.Reset();
                    device->copyQueue.Reset();
#elif defined(__APPLE__) && !defined(RIVE_UNREAL)
                    if (device->metalDevice != nullptr)
                    {
                        rive_metal_device_release(device->metalDevice);
                        device->metalDevice = nullptr;
                    }
#endif
                    delete device;
                }
            }

#if defined(_WIN32) && !defined(RIVE_UNREAL)
            ReleaseD3D12Context(handle);
#else
            handle->renderContext.reset();
            handle->cpuRenderTarget.reset();
            handle->cpuFramebuffer.clear();
            handle->cpuFrameRecording  = false;
            handle->commandListsClosed = false;
#endif
            handle->surface = nullptr;
            delete handle;
        }

        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_context_get_size(rive_renderer_context_t context, std::uint32_t* out_width,
                                                          std::uint32_t* out_height)
    {
        if (out_width == nullptr || out_height == nullptr)
        {
            SetLastError("size output pointers are null");
            return rive_renderer_status_t::null_pointer;
        }

        auto* handle = ToContext(context);
        if (handle == nullptr)
        {
            SetLastError("context handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

        *out_width  = handle->width;
        *out_height = handle->height;
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_context_resize(rive_renderer_context_t context, std::uint32_t width,
                                                        std::uint32_t height)
    {
        auto* handle = ToContext(context);
        if (handle == nullptr)
        {
            SetLastError("context handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

        if (!ValidateContextSize(width, height))
        {
            SetLastError("context dimensions must be non-zero");
            return rive_renderer_status_t::invalid_parameter;
        }

        handle->width  = width;
        handle->height = height;
        if (handle->device != nullptr && handle->device->backend == rive_renderer_backend_t::null)
        {
            handle->cpuFramebuffer.assign(static_cast<size_t>(width) * height * 4, 0);
        }
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_context_begin_frame(rive_renderer_context_t              context,
                                                             const rive_renderer_frame_options_t* options)
    {
        auto* handle = ToContext(context);
        if (handle == nullptr)
        {
            SetLastError("context handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

        std::uint32_t width  = handle->width;
        std::uint32_t height = handle->height;
        if (options != nullptr)
        {
            if (options->width != 0)
            {
                width = options->width;
            }
            if (options->height != 0)
            {
                height = options->height;
            }
        }

        if (!ValidateContextSize(width, height))
        {
            SetLastError("context dimensions must be non-zero");
            return rive_renderer_status_t::invalid_parameter;
        }

        handle->width  = width;
        handle->height = height;

#if defined(__APPLE__) && !defined(RIVE_UNREAL)
        if (handle->device != nullptr && handle->device->backend == rive_renderer_backend_t::metal)
        {
            auto status = rive_metal_context_begin_frame(handle->metalContext, handle->renderContext.get(),
                                                         &handle->width, &handle->height, options,
                                                         handle->surface ? handle->surface->metalSurface : nullptr);
            if (status != rive_renderer_status_t::ok)
            {
                return status;
            }

            handle->hasActiveFrame     = true;
            handle->commandListsClosed = false;
            handle->pendingFrameNumber = handle->frameCounter;
            ClearLastError();
            return rive_renderer_status_t::ok;
        }
#endif

#if defined(_WIN32) && !defined(RIVE_UNREAL)
        if (handle->device != nullptr && handle->device->backend == rive_renderer_backend_t::d3d12)
        {
            auto status = EnsureD3D12RenderTarget(handle);
            if (status != rive_renderer_status_t::ok)
            {
                return status;
            }

            HRESULT hr = handle->directAllocator->Reset();
            if (FAILED(hr))
            {
                SetLastError("Reset direct allocator failed");
                return rive_renderer_status_t::internal_error;
            }
            hr = handle->copyAllocator->Reset();
            if (FAILED(hr))
            {
                SetLastError("Reset copy allocator failed");
                return rive_renderer_status_t::internal_error;
            }

            hr = handle->directCommandList->Reset(handle->directAllocator.Get(), nullptr);
            if (FAILED(hr))
            {
                SetLastError("Reset direct command list failed");
                return rive_renderer_status_t::internal_error;
            }
            hr = handle->copyCommandList->Reset(handle->copyAllocator.Get(), nullptr);
            if (FAILED(hr))
            {
                SetLastError("Reset copy command list failed");
                return rive_renderer_status_t::internal_error;
            }

            rive::gpu::RenderContext::FrameDescriptor descriptor;
            descriptor.renderTargetWidth     = handle->width;
            descriptor.renderTargetHeight    = handle->height;
            descriptor.loadAction            = rive::gpu::LoadAction::clear;
            descriptor.clearColor            = 0;
            descriptor.msaaSampleCount       = 0;
            descriptor.disableRasterOrdering = false;

            handle->renderContext->beginFrame(descriptor);
            handle->hasActiveFrame     = true;
            handle->commandListsClosed = false;
            handle->pendingFrameNumber = handle->frameCounter;

            ClearLastError();
            return rive_renderer_status_t::ok;
        }
#endif

        if (handle->device != nullptr && handle->device->backend == rive_renderer_backend_t::null)
        {
            const size_t required = static_cast<size_t>(handle->width) * handle->height * 4;
            if (handle->cpuFramebuffer.size() != required)
            {
                handle->cpuFramebuffer.assign(required, 0);
            }
            else
            {
                std::fill(handle->cpuFramebuffer.begin(), handle->cpuFramebuffer.end(), 0);
            }
            handle->cpuFrameRecording  = true;
            handle->commandListsClosed = false;
            ClearLastError();
            return rive_renderer_status_t::ok;
        }

        SetLastError("context_begin_frame not implemented for this backend");
        return rive_renderer_status_t::unimplemented;
    }

    rive_renderer_status_t rive_renderer_context_end_frame(rive_renderer_context_t context)
    {
        auto* handle = ToContext(context);
        if (handle == nullptr)
        {
            SetLastError("context handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

#if defined(_WIN32) && !defined(RIVE_UNREAL)
        if (handle->device != nullptr && handle->device->backend == rive_renderer_backend_t::d3d12)
        {
            if (!handle->hasActiveFrame)
            {
                SetLastError("begin_frame must be called before end_frame");
                return rive_renderer_status_t::invalid_parameter;
            }
            auto* impl = GetD3D12Impl(handle);
            if (impl == nullptr || handle->renderTarget == nullptr)
            {
                SetLastError("render context not initialized");
                return rive_renderer_status_t::internal_error;
            }

            rive::gpu::RenderContextD3D12Impl::CommandLists cmdLists {handle->copyCommandList.Get(),
                                                                      handle->directCommandList.Get()};

            rive::gpu::RenderContext::FlushResources resources {};
            resources.renderTarget          = handle->renderTarget.get();
            resources.externalCommandBuffer = &cmdLists;
            resources.currentFrameNumber    = handle->frameCounter;
            resources.safeFrameNumber       = handle->lastCompletedFrame;

            handle->renderContext->flush(resources);

            HRESULT hr = handle->copyCommandList->Close();
            if (FAILED(hr))
            {
                SetLastError("copy command list close failed");
                return rive_renderer_status_t::internal_error;
            }
            hr = handle->directCommandList->Close();
            if (FAILED(hr))
            {
                SetLastError("direct command list close failed");
                return rive_renderer_status_t::internal_error;
            }

            handle->hasActiveFrame     = false;
            handle->commandListsClosed = true;

            ClearLastError();
            return rive_renderer_status_t::ok;
        }
#else
#if defined(__APPLE__) && !defined(RIVE_UNREAL)
        if (handle->device != nullptr && handle->device->backend == rive_renderer_backend_t::metal)
        {
            if (!handle->hasActiveFrame)
            {
                SetLastError("begin_frame must be called before end_frame");
                return rive_renderer_status_t::invalid_parameter;
            }

            auto status = rive_metal_context_end_frame(handle->metalContext, handle->renderContext.get(),
                                                       handle->surface ? handle->surface->metalSurface : nullptr);
            if (status != rive_renderer_status_t::ok)
            {
                return status;
            }

            handle->hasActiveFrame     = false;
            handle->commandListsClosed = true;
            ClearLastError();
            return rive_renderer_status_t::ok;
        }
#endif
#endif

        if (handle->device != nullptr && handle->device->backend == rive_renderer_backend_t::null)
        {
            if (!handle->cpuFrameRecording)
            {
                SetLastError("begin_frame must be called before end_frame");
                return rive_renderer_status_t::invalid_parameter;
            }
            handle->cpuFrameRecording  = false;
            handle->commandListsClosed = true;
            ClearLastError();
            return rive_renderer_status_t::ok;
        }

        SetLastError("context_end_frame not implemented for this backend");
        return rive_renderer_status_t::unimplemented;
    }

    rive_renderer_status_t rive_renderer_context_submit(rive_renderer_context_t context)
    {
        auto* handle = ToContext(context);
        if (handle == nullptr)
        {
            SetLastError("context handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

#if defined(_WIN32) && !defined(RIVE_UNREAL)
        if (handle->device != nullptr && handle->device->backend == rive_renderer_backend_t::d3d12)
        {
            if (!handle->commandListsClosed)
            {
                SetLastError("end_frame must be called before submit");
                return rive_renderer_status_t::invalid_parameter;
            }

            auto* device = handle->device;

            ID3D12CommandList* copyLists[] = {handle->copyCommandList.Get()};
            device->copyQueue->ExecuteCommandLists(1, copyLists);

            handle->fenceValue += 1;
            HRESULT hr = device->copyQueue->Signal(handle->copyFence.Get(), handle->fenceValue);
            if (FAILED(hr))
            {
                SetLastError("copy queue signal failed");
                return rive_renderer_status_t::internal_error;
            }

            hr = device->directQueue->Wait(handle->copyFence.Get(), handle->fenceValue);
            if (FAILED(hr))
            {
                SetLastError("direct queue wait failed");
                return rive_renderer_status_t::internal_error;
            }

            ID3D12CommandList* directLists[] = {handle->directCommandList.Get()};
            device->directQueue->ExecuteCommandLists(1, directLists);

            hr = device->directQueue->Signal(handle->directFence.Get(), handle->fenceValue);
            if (FAILED(hr))
            {
                SetLastError("direct queue signal failed");
                return rive_renderer_status_t::internal_error;
            }

            hr = handle->copyFence->SetEventOnCompletion(handle->fenceValue, handle->fenceEvent);
            if (FAILED(hr))
            {
                SetLastError("copy fence wait failed");
                return rive_renderer_status_t::internal_error;
            }
            WaitForSingleObject(handle->fenceEvent, INFINITE);

            hr = handle->directFence->SetEventOnCompletion(handle->fenceValue, handle->fenceEvent);
            if (FAILED(hr))
            {
                SetLastError("direct fence wait failed");
                return rive_renderer_status_t::internal_error;
            }
            WaitForSingleObject(handle->fenceEvent, INFINITE);

            handle->lastCompletedFrame = handle->frameCounter;
            handle->frameCounter += 1;
            handle->pendingFrameNumber = 0;
            handle->commandListsClosed = false;

            handle->copyAllocator->Reset();
            handle->directAllocator->Reset();
            handle->copyCommandList->Reset(handle->copyAllocator.Get(), nullptr);
            handle->directCommandList->Reset(handle->directAllocator.Get(), nullptr);

            handle->copyCommandList->Close();
            handle->directCommandList->Close();

            ClearLastError();
            return rive_renderer_status_t::ok;
        }
#endif

#if defined(__APPLE__) && !defined(RIVE_UNREAL)
        if (handle->device != nullptr && handle->device->backend == rive_renderer_backend_t::metal)
        {
            if (!handle->commandListsClosed)
            {
                SetLastError("end_frame must be called before submit");
                return rive_renderer_status_t::invalid_parameter;
            }

            auto status = rive_metal_context_submit(handle->metalContext, handle->surface != nullptr);
            if (status != rive_renderer_status_t::ok)
            {
                return status;
            }

            handle->pendingFrameNumber = 0;

            if (handle->surface == nullptr)
            {
                handle->lastCompletedFrame = handle->frameCounter;
                handle->frameCounter += 1;
            }

            handle->commandListsClosed = false;
            ClearLastError();
            return rive_renderer_status_t::ok;
        }
#endif

        if (handle->device != nullptr && handle->device->backend == rive_renderer_backend_t::null)
        {
            if (!handle->commandListsClosed)
            {
                SetLastError("end_frame must be called before submit");
                return rive_renderer_status_t::invalid_parameter;
            }
            handle->lastCompletedFrame = handle->frameCounter;
            handle->frameCounter += 1;
            handle->commandListsClosed = false;
            ClearLastError();
            return rive_renderer_status_t::ok;
        }

        SetLastError("context_submit not implemented for this backend");
        return rive_renderer_status_t::unimplemented;
    }

    rive_renderer_status_t rive_renderer_surface_create_d3d12_hwnd(
        rive_renderer_device_t device, rive_renderer_context_t context,
        const rive_renderer_surface_create_info_d3d12_hwnd_t* info, rive_renderer_surface_t* out_surface)
    {
        if (out_surface == nullptr)
        {
            SetLastError("surface output pointer is null");
            return rive_renderer_status_t::null_pointer;
        }

        out_surface->handle = nullptr;

        if (info == nullptr)
        {
            SetLastError("surface create info is null");
            return rive_renderer_status_t::null_pointer;
        }

        auto* device_handle  = ToDevice(device);
        auto* context_handle = ToContext(context);
        if (device_handle == nullptr || context_handle == nullptr)
        {
            SetLastError("device or context handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

#if defined(_WIN32) && !defined(RIVE_UNREAL)
        if (device_handle->backend == rive_renderer_backend_t::d3d12 &&
            context_handle->device == device_handle)
        {
            if (info->hwnd == nullptr)
            {
                SetLastError("HWND pointer is null");
                return rive_renderer_status_t::invalid_parameter;
            }

            if (context_handle->surface != nullptr)
            {
                SetLastError("context already has an attached surface");
                return rive_renderer_status_t::invalid_parameter;
            }

            std::uint32_t width  = info->width != 0 ? info->width : context_handle->width;
            std::uint32_t height = info->height != 0 ? info->height : context_handle->height;
            if (!ValidateContextSize(width, height))
            {
                SetLastError("surface dimensions must be non-zero");
                return rive_renderer_status_t::invalid_parameter;
            }

            std::uint32_t bufferCount = info->buffer_count != 0 ? info->buffer_count : 2;
            if (bufferCount < 2)
            {
                bufferCount = 2;
            }

            Microsoft::WRL::ComPtr<IDXGIFactory4> factory4;
            HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory4));
            if (FAILED(hr))
            {
                SetLastError("CreateDXGIFactory2 failed");
                return rive_renderer_status_t::internal_error;
            }

            DXGI_SWAP_CHAIN_DESC1 desc {};
            desc.Width       = width;
            desc.Height      = height;
            desc.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            desc.SampleDesc.Count   = 1;
            desc.SampleDesc.Quality = 0;
            desc.Scaling            = DXGI_SCALING_NONE;
            desc.BufferCount        = bufferCount;
            desc.AlphaMode          = DXGI_ALPHA_MODE_IGNORE;

            bool allowTearing = false;
            if ((static_cast<std::uint32_t>(info->flags) &
                 static_cast<std::uint32_t>(rive_renderer_surface_flags_t::allow_tearing)) != 0)
            {
                allowTearing = CheckTearingSupport();
                if (allowTearing)
                {
                    desc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
                }
            }

            Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;
            hr = factory4->CreateSwapChainForHwnd(device_handle->directQueue.Get(),
                                                  static_cast<HWND>(info->hwnd), &desc, nullptr, nullptr,
                                                  &swapChain1);
            if (FAILED(hr))
            {
                SetLastError("CreateSwapChainForHwnd failed");
                return rive_renderer_status_t::internal_error;
            }

            factory4->MakeWindowAssociation(static_cast<HWND>(info->hwnd), DXGI_MWA_NO_ALT_ENTER);

            Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain3;
            hr = swapChain1.As(&swapChain3);
            if (FAILED(hr))
            {
                SetLastError("IDXGISwapChain3 query failed");
                return rive_renderer_status_t::internal_error;
            }

            auto* surface = new (std::nothrow) SurfaceHandle();
            if (surface == nullptr)
            {
                SetLastError("allocation failed");
                return rive_renderer_status_t::out_of_memory;
            }

            surface->backend          = rive_renderer_backend_t::d3d12;
            surface->device           = device_handle;
            surface->context          = context_handle;
            surface->width            = width;
            surface->height           = height;
            surface->buffer_count     = bufferCount;
            surface->flags            = info->flags;
            surface->present_interval = info->present_interval != 0 ? info->present_interval : 1;
            surface->hwnd             = info->hwnd;
            surface->swapChain        = swapChain3;
            surface->supportsTearing  = allowTearing;

            context_handle->renderTarget.reset();
            context_handle->renderTargetTexture.Reset();

            rive_renderer_status_t targetsStatus = CreateSurfaceRenderTargets(surface, width, height);
            if (targetsStatus != rive_renderer_status_t::ok)
            {
                delete surface;
                return targetsStatus;
            }

            rive_renderer_status_t retainDeviceStatus = rive_renderer_device_retain(device);
            if (retainDeviceStatus != rive_renderer_status_t::ok)
            {
                delete surface;
                return retainDeviceStatus;
            }

            rive_renderer_status_t retainContextStatus = rive_renderer_context_retain(context);
            if (retainContextStatus != rive_renderer_status_t::ok)
            {
                rive_renderer_device_release(device);
                delete surface;
                return retainContextStatus;
            }

            context_handle->surface = surface;
            context_handle->width   = width;
            context_handle->height  = height;

            out_surface->handle = surface;
            ClearLastError();
            return rive_renderer_status_t::ok;
        }
        SetLastError("surface creation not supported for this backend");
        return rive_renderer_status_t::unsupported;
#else
        (void)info;
        SetLastError("surface creation not supported on this platform");
        return rive_renderer_status_t::unsupported;
#endif
    }

    rive_renderer_status_t rive_renderer_surface_create_metal_layer(
        rive_renderer_device_t device, rive_renderer_context_t context,
        const rive_renderer_surface_create_info_metal_layer_t* info, rive_renderer_surface_t* out_surface)
    {
        (void)device;
        (void)context;
        (void)info;
        if (out_surface != nullptr)
        {
            out_surface->handle = nullptr;
        }
#if defined(__APPLE__) && !defined(RIVE_UNREAL)
        auto* device_handle  = ToDevice(device);
        auto* context_handle = ToContext(context);
        if (device_handle == nullptr || context_handle == nullptr)
        {
            SetLastError("device or context handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

        if (device_handle->backend != rive_renderer_backend_t::metal ||
            context_handle->device != device_handle)
        {
            SetLastError("surface creation not supported for this backend");
            return rive_renderer_status_t::unsupported;
        }

        if (info == nullptr)
        {
            SetLastError("surface create info is null");
            return rive_renderer_status_t::null_pointer;
        }

        if (context_handle->surface != nullptr)
        {
            SetLastError("context already has an attached surface");
            return rive_renderer_status_t::invalid_parameter;
        }

        if (info->layer == nullptr)
        {
            SetLastError("CAMetalLayer pointer is null");
            return rive_renderer_status_t::invalid_parameter;
        }

        std::uint32_t width  = info->width != 0 ? info->width : context_handle->width;
        std::uint32_t height = info->height != 0 ? info->height : context_handle->height;
        if (!ValidateContextSize(width, height))
        {
            SetLastError("surface dimensions must be non-zero");
            return rive_renderer_status_t::invalid_parameter;
            }

        auto* surface = new (std::nothrow) SurfaceHandle();
        if (surface == nullptr)
        {
            SetLastError("allocation failed");
            return rive_renderer_status_t::out_of_memory;
        }

        surface->backend          = rive_renderer_backend_t::metal;
        surface->device           = device_handle;
        surface->context          = context_handle;
        surface->width            = width;
        surface->height           = height;
        surface->buffer_count     = 1;
        surface->flags            = info->flags;
        surface->present_interval = 1;

        rive_renderer_surface_create_info_metal_layer_t adjustedInfo = *info;
        adjustedInfo.width  = width;
        adjustedInfo.height = height;

        void* metalSurface = nullptr;
        rive_renderer_status_t status = rive_metal_surface_create(device_handle->metalDevice, context_handle->metalContext,
                                                                  &adjustedInfo, &metalSurface);
        if (status != rive_renderer_status_t::ok)
        {
            delete surface;
            if (g_lastError.empty())
            {
                SetLastError("Metal surface creation failed");
            }
            return status;
        }

        surface->metalSurface = metalSurface;

        rive_renderer_status_t retainDeviceStatus = rive_renderer_device_retain(device);
        if (retainDeviceStatus != rive_renderer_status_t::ok)
        {
            rive_metal_surface_destroy(surface->metalSurface);
            delete surface;
            return retainDeviceStatus;
        }

        rive_renderer_status_t retainContextStatus = rive_renderer_context_retain(context);
        if (retainContextStatus != rive_renderer_status_t::ok)
        {
            rive_renderer_device_release(device);
            rive_metal_surface_destroy(surface->metalSurface);
            delete surface;
            return retainContextStatus;
        }

        context_handle->surface = surface;
        context_handle->width   = width;
        context_handle->height  = height;

        out_surface->handle = surface;
        ClearLastError();
        return rive_renderer_status_t::ok;
#else
        SetLastError("Metal surface creation not supported on this platform");
        return rive_renderer_status_t::unsupported;
#endif
    }

    rive_renderer_status_t rive_renderer_surface_create_vulkan(
        rive_renderer_device_t device,
        rive_renderer_context_t context,
        const rive_renderer_surface_create_info_vulkan_t* info,
        rive_renderer_surface_t* out_surface)
    {
        if (out_surface == nullptr)
        {
            SetLastError("surface output pointer is null");
            return rive_renderer_status_t::null_pointer;
        }

        out_surface->handle = nullptr;

        if (info == nullptr)
        {
            SetLastError("surface create info is null");
            return rive_renderer_status_t::null_pointer;
        }

        auto* device_handle  = ToDevice(device);
        auto* context_handle = ToContext(context);
        if (device_handle == nullptr || context_handle == nullptr)
        {
            SetLastError("device or context handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

#if defined(RIVE_RENDERER_FFI_HAS_VULKAN)
        if (device_handle->backend != rive_renderer_backend_t::vulkan ||
            context_handle->device != device_handle)
        {
            SetLastError("surface creation not supported for this backend");
            return rive_renderer_status_t::unsupported;
        }

        SetLastError("Vulkan surface creation not yet implemented");
        return rive_renderer_status_t::unimplemented;
#else
        (void)device;
        (void)context;
        (void)info;
        SetLastError("Vulkan backend is not available in this build");
        return rive_renderer_status_t::unsupported;
#endif
    }

    rive_renderer_status_t rive_renderer_surface_retain(rive_renderer_surface_t surface)
    {
        auto* handle = ToSurface(surface);
        if (handle == nullptr)
        {
            SetLastError("surface handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

        handle->ref_count.fetch_add(1, std::memory_order_relaxed);
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_surface_release(rive_renderer_surface_t surface)
    {
        auto* handle = ToSurface(surface);
        if (handle == nullptr)
        {
            SetLastError("surface handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

        const std::uint32_t previous = handle->ref_count.fetch_sub(1, std::memory_order_acq_rel);
        if (previous == 0)
        {
            handle->ref_count.fetch_add(1, std::memory_order_relaxed);
            SetLastError("surface handle refcount underflow");
            return rive_renderer_status_t::internal_error;
        }

        if (previous == 1)
        {
#if defined(_WIN32) && !defined(RIVE_UNREAL)
            if (handle->context != nullptr)
            {
                ReturnSurfaceRenderTarget(handle->context);
                if (handle->context->surface == handle)
                {
                    handle->context->surface = nullptr;
                }
            }
            handle->renderTargets.clear();
            handle->backBuffers.clear();
            handle->swapChain.Reset();
#elif defined(__APPLE__) && !defined(RIVE_UNREAL)
            if (handle->metalSurface != nullptr)
            {
                rive_metal_surface_destroy(handle->metalSurface);
                handle->metalSurface = nullptr;
            }
            if (handle->context != nullptr && handle->context->surface == handle)
            {
                handle->context->surface = nullptr;
            }
#endif
            if (handle->context != nullptr)
            {
                rive_renderer_context_t ctx {};
                ctx.handle = handle->context;
                rive_renderer_context_release(ctx);
            }
            if (handle->device != nullptr)
            {
                rive_renderer_device_t dev {};
                dev.handle = handle->device;
                rive_renderer_device_release(dev);
            }
            handle->context = nullptr;
            handle->device  = nullptr;
            delete handle;
        }

        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_surface_get_size(rive_renderer_surface_t surface, std::uint32_t* out_width,
                                                          std::uint32_t* out_height)
    {
        if (out_width == nullptr || out_height == nullptr)
        {
            SetLastError("surface size output pointers are null");
            return rive_renderer_status_t::null_pointer;
        }

        auto* handle = ToSurface(surface);
        if (handle == nullptr)
        {
            SetLastError("surface handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

        *out_width  = handle->width;
        *out_height = handle->height;
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_surface_resize(rive_renderer_surface_t surface, std::uint32_t width,
                                                        std::uint32_t height)
    {
#if defined(_WIN32) && !defined(RIVE_UNREAL)
        auto* handle = ToSurface(surface);
        if (handle == nullptr)
        {
            SetLastError("surface handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

        if (handle->backend != rive_renderer_backend_t::d3d12)
        {
            SetLastError("surface resize not supported for this backend");
            return rive_renderer_status_t::unsupported;
        }

        if (!ValidateContextSize(width, height))
        {
            SetLastError("surface dimensions must be non-zero");
            return rive_renderer_status_t::invalid_parameter;
        }

        if (handle->swapChain == nullptr || handle->context == nullptr)
        {
            SetLastError("surface not initialized");
            return rive_renderer_status_t::invalid_handle;
        }

        auto* context = handle->context;
        if (context->hasActiveFrame)
        {
            SetLastError("cannot resize while a frame is active");
            return rive_renderer_status_t::invalid_parameter;
        }

        ReturnSurfaceRenderTarget(context);
        context->renderTarget.reset();
        context->renderTargetTexture.Reset();

        handle->renderTargets.clear();
        handle->backBuffers.clear();

        UINT resizeFlags = handle->supportsTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
        HRESULT hr = handle->swapChain->ResizeBuffers(handle->buffer_count, width, height, DXGI_FORMAT_R8G8B8A8_UNORM,
                                                      resizeFlags);
        if (FAILED(hr))
        {
            SetLastError("swapchain resize failed");
            return rive_renderer_status_t::internal_error;
        }

        rive_renderer_status_t targetsStatus = CreateSurfaceRenderTargets(handle, width, height);
        if (targetsStatus != rive_renderer_status_t::ok)
        {
            return targetsStatus;
        }

        handle->width  = width;
        handle->height = height;
        context->width  = width;
        context->height = height;

        ClearLastError();
        return rive_renderer_status_t::ok;
#elif defined(__APPLE__) && !defined(RIVE_UNREAL)
        auto* handle = ToSurface(surface);
        if (handle == nullptr)
        {
            SetLastError("surface handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

        if (handle->backend != rive_renderer_backend_t::metal)
        {
            SetLastError("surface resize not supported for this backend");
            return rive_renderer_status_t::unsupported;
        }

        if (!ValidateContextSize(width, height))
        {
            SetLastError("surface dimensions must be non-zero");
            return rive_renderer_status_t::invalid_parameter;
        }

        if (handle->metalSurface == nullptr || handle->context == nullptr)
        {
            SetLastError("surface not initialized");
            return rive_renderer_status_t::invalid_handle;
        }

        auto* context = handle->context;
        if (context->hasActiveFrame)
        {
            SetLastError("cannot resize while a frame is active");
            return rive_renderer_status_t::invalid_parameter;
        }

        rive_renderer_status_t status = rive_metal_surface_resize(handle->metalSurface, width, height);
        if (status != rive_renderer_status_t::ok)
        {
            return status;
        }

        handle->width  = width;
        handle->height = height;
        context->width  = width;
        context->height = height;

        ClearLastError();
        return rive_renderer_status_t::ok;
#else
        (void)surface;
        (void)width;
        (void)height;
        SetLastError("surface resize not supported on this platform");
        return rive_renderer_status_t::unsupported;
#endif
    }

    rive_renderer_status_t rive_renderer_surface_present(rive_renderer_surface_t surface, std::uint32_t present_interval,
                                                         rive_renderer_present_flags_t flags)
    {
#if defined(_WIN32) && !defined(RIVE_UNREAL)
        auto* handle = ToSurface(surface);
        if (handle == nullptr)
        {
            SetLastError("surface handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

        if (handle->backend != rive_renderer_backend_t::d3d12)
        {
            SetLastError("surface present not supported for this backend");
            return rive_renderer_status_t::unsupported;
        }

        if (handle->swapChain == nullptr || handle->context == nullptr)
        {
            SetLastError("surface not initialized");
            return rive_renderer_status_t::invalid_handle;
        }

        auto* context = handle->context;
        if (context->hasActiveFrame || context->pendingFrameNumber != 0)
        {
            SetLastError("submit must be called before present");
            return rive_renderer_status_t::invalid_parameter;
        }

        ReturnSurfaceRenderTarget(context);

        UINT syncInterval = present_interval != 0 ? present_interval : handle->present_interval;
        UINT presentFlags = 0;
        if ((static_cast<std::uint32_t>(flags) &
             static_cast<std::uint32_t>(rive_renderer_present_flags_t::allow_tearing)) != 0 &&
            handle->supportsTearing && syncInterval == 0)
        {
            presentFlags |= DXGI_PRESENT_ALLOW_TEARING;
        }

        HRESULT hr = handle->swapChain->Present(syncInterval, presentFlags);
        if (FAILED(hr))
        {
            if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
            {
                SetLastError("device lost during present");
                return rive_renderer_status_t::device_lost;
            }
            SetLastError("swapchain present failed");
            return rive_renderer_status_t::internal_error;
        }

        context->renderTarget.reset();
        context->renderTargetTexture.Reset();
        handle->borrowedIndex = std::numeric_limits<UINT>::max();

        ClearLastError();
        return rive_renderer_status_t::ok;
#elif defined(__APPLE__) && !defined(RIVE_UNREAL)
        auto* handle = ToSurface(surface);
        if (handle == nullptr)
        {
            SetLastError("surface handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

        if (handle->backend != rive_renderer_backend_t::metal)
        {
            SetLastError("surface present not supported for this backend");
            return rive_renderer_status_t::unsupported;
        }

        if (handle->metalSurface == nullptr || handle->context == nullptr)
        {
            SetLastError("surface not initialized");
            return rive_renderer_status_t::invalid_handle;
        }

        auto* context = handle->context;
        if (context->hasActiveFrame || context->pendingFrameNumber != 0)
        {
            SetLastError("submit must be called before present");
            return rive_renderer_status_t::invalid_parameter;
        }

        rive_renderer_status_t status = rive_metal_surface_present(handle->metalSurface, context->metalContext,
                                                                   context->renderContext.get(), flags,
                                                                   present_interval);
        if (status != rive_renderer_status_t::ok)
        {
            return status;
        }

        context->lastCompletedFrame = context->frameCounter;
        context->frameCounter += 1;
        context->commandListsClosed = false;
        context->pendingFrameNumber = 0;

        ClearLastError();
        return rive_renderer_status_t::ok;
#else
        (void)surface;
        (void)present_interval;
        (void)flags;
        SetLastError("surface present not supported on this platform");
        return rive_renderer_status_t::unsupported;
#endif
    }

    rive_renderer_status_t rive_renderer_fence_create(rive_renderer_device_t device, rive_renderer_fence_t* out_fence)
    {
        if (out_fence == nullptr)
        {
            SetLastError("fence output pointer is null");
            return rive_renderer_status_t::null_pointer;
        }

        out_fence->handle = nullptr;

        auto* device_handle = ToDevice(device);
        if (device_handle == nullptr)
        {
            SetLastError("device handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

#if defined(_WIN32) && !defined(RIVE_UNREAL)
        if (device_handle->backend != rive_renderer_backend_t::d3d12)
        {
            SetLastError("fence creation not supported for this backend");
            return rive_renderer_status_t::unsupported;
        }

        auto* fenceHandle = new (std::nothrow) FenceHandle();
        if (fenceHandle == nullptr)
        {
            SetLastError("allocation failed");
            return rive_renderer_status_t::out_of_memory;
        }

        HRESULT hr = device_handle->d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fenceHandle->fence));
        if (FAILED(hr))
        {
            delete fenceHandle;
            SetLastError("CreateFence failed");
            return rive_renderer_status_t::internal_error;
        }

        fenceHandle->eventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (fenceHandle->eventHandle == nullptr)
        {
            fenceHandle->fence.Reset();
            delete fenceHandle;
            SetLastError("CreateEvent failed");
            return rive_renderer_status_t::internal_error;
        }

        fenceHandle->device = device_handle;
        fenceHandle->lastValue.store(0, std::memory_order_relaxed);
        device_handle->ref_count.fetch_add(1, std::memory_order_relaxed);

        out_fence->handle = fenceHandle;
        ClearLastError();
        return rive_renderer_status_t::ok;
#else
        (void)device;
        (void)device_handle;
        SetLastError("fence creation not supported on this platform");
        return rive_renderer_status_t::unsupported;
#endif
    }

    rive_renderer_status_t rive_renderer_fence_retain(rive_renderer_fence_t fence)
    {
        auto* handle = ToFence(fence);
        if (handle == nullptr)
        {
            SetLastError("fence handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

        handle->ref_count.fetch_add(1, std::memory_order_relaxed);
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_fence_release(rive_renderer_fence_t fence)
    {
        auto* handle = ToFence(fence);
        if (handle == nullptr)
        {
            SetLastError("fence handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

        const std::uint32_t previous = handle->ref_count.fetch_sub(1, std::memory_order_acq_rel);
        if (previous == 0)
        {
            handle->ref_count.fetch_add(1, std::memory_order_relaxed);
            SetLastError("fence handle refcount underflow");
            return rive_renderer_status_t::internal_error;
        }

        if (previous == 1)
        {
#if defined(_WIN32) && !defined(RIVE_UNREAL)
            if (handle->eventHandle != nullptr)
            {
                CloseHandle(handle->eventHandle);
                handle->eventHandle = nullptr;
            }
            handle->fence.Reset();
#endif
            if (handle->device != nullptr)
            {
                DeviceHandle*       device      = handle->device;
                const std::uint32_t device_prev = device->ref_count.fetch_sub(1, std::memory_order_acq_rel);
                if (device_prev == 0)
                {
                    device->ref_count.fetch_add(1, std::memory_order_relaxed);
                }
                else if (device_prev == 1)
                {
#if defined(_WIN32) && !defined(RIVE_UNREAL)
                    device->adapter.Reset();
                    device->d3d12Device.Reset();
                    device->directQueue.Reset();
                    device->copyQueue.Reset();
#endif
                    delete device;
                }
            }
            delete handle;
        }

        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_fence_get_completed_value(rive_renderer_fence_t fence,
                                                                   std::uint64_t* out_value)
    {
        if (out_value == nullptr)
        {
            SetLastError("completed value pointer is null");
            return rive_renderer_status_t::null_pointer;
        }

        auto* handle = ToFence(fence);
        if (handle == nullptr)
        {
            SetLastError("fence handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

#if defined(_WIN32) && !defined(RIVE_UNREAL)
        if (!handle->fence)
        {
            SetLastError("fence not initialized");
            return rive_renderer_status_t::internal_error;
        }

        *out_value = handle->fence->GetCompletedValue();
        ClearLastError();
        return rive_renderer_status_t::ok;
#else
        (void)out_value;
        SetLastError("fence operations not supported on this platform");
        return rive_renderer_status_t::unsupported;
#endif
    }

    rive_renderer_status_t rive_renderer_fence_wait(rive_renderer_fence_t fence, std::uint64_t value,
                                                    std::uint64_t timeout_ms)
    {
        auto* handle = ToFence(fence);
        if (handle == nullptr)
        {
            SetLastError("fence handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

#if defined(_WIN32) && !defined(RIVE_UNREAL)
        if (!handle->fence)
        {
            SetLastError("fence not initialized");
            return rive_renderer_status_t::internal_error;
        }

        if (handle->fence->GetCompletedValue() >= value)
        {
            ClearLastError();
            return rive_renderer_status_t::ok;
        }

        if (value == 0)
        {
            SetLastError("fence wait value must be non-zero");
            return rive_renderer_status_t::invalid_parameter;
        }

        if (timeout_ms == 0)
        {
            SetLastError("fence wait timed out");
            return rive_renderer_status_t::invalid_parameter;
        }

        HRESULT hr = handle->fence->SetEventOnCompletion(value, handle->eventHandle);
        if (FAILED(hr))
        {
            SetLastError("SetEventOnCompletion failed");
            return rive_renderer_status_t::internal_error;
        }

        DWORD timeoutValue = INFINITE;
        if (timeout_ms != std::numeric_limits<std::uint64_t>::max())
        {
            timeoutValue = timeout_ms >= static_cast<std::uint64_t>(INFINITE - 1)
                               ? INFINITE
                               : static_cast<DWORD>(timeout_ms);
        }

        DWORD waitResult = WaitForSingleObject(handle->eventHandle, timeoutValue);
        if (waitResult == WAIT_OBJECT_0)
        {
            ClearLastError();
            return rive_renderer_status_t::ok;
        }
        if (waitResult == WAIT_TIMEOUT)
        {
            SetLastError("fence wait timed out");
            return rive_renderer_status_t::invalid_parameter;
        }

        SetLastError("fence wait failed");
        return rive_renderer_status_t::internal_error;
#else
        (void)value;
        (void)timeout_ms;
        SetLastError("fence operations not supported on this platform");
        return rive_renderer_status_t::unsupported;
#endif
    }

    rive_renderer_status_t rive_renderer_context_signal_fence(rive_renderer_context_t context,
                                                              rive_renderer_fence_t   fence, std::uint64_t value)
    {
        auto* context_handle = ToContext(context);
        if (context_handle == nullptr)
        {
            SetLastError("context handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

        auto* fence_handle = ToFence(fence);
        if (fence_handle == nullptr)
        {
            SetLastError("fence handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

#if defined(_WIN32) && !defined(RIVE_UNREAL)
        if (context_handle->device == nullptr || fence_handle->device == nullptr ||
            context_handle->device != fence_handle->device)
        {
            SetLastError("fence and context must share the same device");
            return rive_renderer_status_t::invalid_parameter;
        }

        if (context_handle->device->backend != rive_renderer_backend_t::d3d12)
        {
            SetLastError("fence signaling not supported for this backend");
            return rive_renderer_status_t::unsupported;
        }

        auto* device = context_handle->device;
        if (!device->directQueue)
        {
            SetLastError("direct queue unavailable");
            return rive_renderer_status_t::internal_error;
        }

        std::uint64_t previousValue = fence_handle->lastValue.load(std::memory_order_acquire);
        std::uint64_t targetValue   = value;
        if (targetValue == 0)
        {
            targetValue = previousValue + 1;
        }
        else if (targetValue <= previousValue)
        {
            SetLastError("fence signal value must be greater than the last signaled value");
            return rive_renderer_status_t::invalid_parameter;
        }

        fence_handle->lastValue.store(targetValue, std::memory_order_release);

        HRESULT hr = device->directQueue->Signal(fence_handle->fence.Get(), targetValue);
        if (FAILED(hr))
        {
            fence_handle->lastValue.store(previousValue, std::memory_order_release);
            SetLastError("queue signal failed");
            return rive_renderer_status_t::internal_error;
        }

        ClearLastError();
        return rive_renderer_status_t::ok;
#else
        (void)context;
        (void)fence;
        (void)value;
        SetLastError("fence signaling not supported on this platform");
        return rive_renderer_status_t::unsupported;
#endif
    }

    rive_renderer_status_t rive_renderer_path_create(rive_renderer_context_t   context,
                                                     rive_renderer_fill_rule_t fill_rule,
                                                     rive_renderer_path_t*     out_path)
    {
        if (out_path == nullptr)
        {
            SetLastError("path output pointer is null");
            return rive_renderer_status_t::null_pointer;
        }

        auto* ctx = ToContext(context);
        if (ctx == nullptr)
        {
            SetLastError("context handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

        if (!ctx->renderContext)
        {
            SetLastError("render context unavailable");
            return rive_renderer_status_t::unsupported;
        }

        rive::FillRule rule;
        if (!ConvertFillRule(fill_rule, &rule))
        {
            SetLastError("invalid fill rule");
            return rive_renderer_status_t::invalid_parameter;
        }

        rive::rcp<rive::RenderPath> path = ctx->renderContext->makeEmptyRenderPath();
        if (!path)
        {
            SetLastError("makeRenderPath failed");
            return rive_renderer_status_t::internal_error;
        }

        path->fillRule(rule);

        auto* handle = new (std::nothrow) PathHandle();
        if (handle == nullptr)
        {
            SetLastError("allocation failed");
            return rive_renderer_status_t::out_of_memory;
        }

        handle->path     = std::move(path);
        out_path->handle = handle;
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_path_retain(rive_renderer_path_t path)
    {
        auto* handle = ToPath(path);
        if (handle == nullptr)
        {
            SetLastError("path handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

        handle->ref_count.fetch_add(1, std::memory_order_relaxed);
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_path_release(rive_renderer_path_t path)
    {
        auto* handle = ToPath(path);
        if (handle == nullptr)
        {
            SetLastError("path handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

        const std::uint32_t previous = handle->ref_count.fetch_sub(1, std::memory_order_acq_rel);
        if (previous == 0)
        {
            SetLastError("path handle refcount underflow");
            return rive_renderer_status_t::internal_error;
        }

        if (previous == 1)
        {
            delete handle;
        }

        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_path_rewind(rive_renderer_path_t path)
    {
        auto* handle = ToPath(path);
        if (handle == nullptr || !handle->path)
        {
            SetLastError("path handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

        handle->path->rewind();
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_path_set_fill_rule(rive_renderer_path_t      path,
                                                            rive_renderer_fill_rule_t fill_rule)
    {
        auto* handle = ToPath(path);
        if (handle == nullptr || !handle->path)
        {
            SetLastError("path handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

        rive::FillRule rule;
        if (!ConvertFillRule(fill_rule, &rule))
        {
            SetLastError("invalid fill rule");
            return rive_renderer_status_t::invalid_parameter;
        }

        handle->path->fillRule(rule);
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_path_move_to(rive_renderer_path_t path, float x, float y)
    {
        auto* handle = ToPath(path);
        if (handle == nullptr || !handle->path)
        {
            SetLastError("path handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

        handle->path->moveTo(x, y);
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_path_line_to(rive_renderer_path_t path, float x, float y)
    {
        auto* handle = ToPath(path);
        if (handle == nullptr || !handle->path)
        {
            SetLastError("path handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

        handle->path->lineTo(x, y);
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_path_cubic_to(rive_renderer_path_t path, float ox, float oy, float ix,
                                                       float iy, float x, float y)
    {
        auto* handle = ToPath(path);
        if (handle == nullptr || !handle->path)
        {
            SetLastError("path handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

        handle->path->cubicTo(ox, oy, ix, iy, x, y);
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_path_close(rive_renderer_path_t path)
    {
        auto* handle = ToPath(path);
        if (handle == nullptr || !handle->path)
        {
            SetLastError("path handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

        handle->path->close();
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_path_add_path(rive_renderer_path_t destination, rive_renderer_path_t source,
                                                       const rive_renderer_mat2d_t* transform)
    {
        auto* dstHandle = ToPath(destination);
        auto* srcHandle = ToPath(source);
        if (dstHandle == nullptr || !dstHandle->path || srcHandle == nullptr || !srcHandle->path)
        {
            SetLastError("path handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

        rive::Mat2D mat = ToMat2D(transform);
        dstHandle->path->addPath(srcHandle->path.get(), mat);
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_paint_create(rive_renderer_context_t context, rive_renderer_paint_t* out_paint)
    {
        if (out_paint == nullptr)
        {
            SetLastError("paint output pointer is null");
            return rive_renderer_status_t::null_pointer;
        }

        auto* ctx = ToContext(context);
        if (ctx == nullptr)
        {
            SetLastError("context handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

        if (!ctx->renderContext)
        {
            SetLastError("render context unavailable");
            return rive_renderer_status_t::unsupported;
        }

        rive::rcp<rive::RenderPaint> paint = ctx->renderContext->makeRenderPaint();
        if (!paint)
        {
            SetLastError("makeRenderPaint failed");
            return rive_renderer_status_t::internal_error;
        }

        auto* handle = new (std::nothrow) PaintHandle();
        if (handle == nullptr)
        {
            SetLastError("allocation failed");
            return rive_renderer_status_t::out_of_memory;
        }

        handle->paint     = std::move(paint);
        out_paint->handle = handle;
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_paint_retain(rive_renderer_paint_t paint)
    {
        auto* handle = ToPaint(paint);
        if (handle == nullptr)
        {
            SetLastError("paint handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

        handle->ref_count.fetch_add(1, std::memory_order_relaxed);
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_paint_release(rive_renderer_paint_t paint)
    {
        auto* handle = ToPaint(paint);
        if (handle == nullptr)
        {
            SetLastError("paint handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

        const std::uint32_t previous = handle->ref_count.fetch_sub(1, std::memory_order_acq_rel);
        if (previous == 0)
        {
            SetLastError("paint handle refcount underflow");
            return rive_renderer_status_t::internal_error;
        }

        if (previous == 1)
        {
            delete handle;
        }

        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_paint_set_style(rive_renderer_paint_t paint, rive_renderer_paint_style_t style)
    {
        auto* handle = ToPaint(paint);
        if (handle == nullptr || !handle->paint)
        {
            SetLastError("paint handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

        rive::RenderPaintStyle cppStyle;
        if (!ConvertPaintStyle(style, &cppStyle))
        {
            SetLastError("invalid paint style");
            return rive_renderer_status_t::invalid_parameter;
        }

        handle->paint->style(cppStyle);
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_paint_set_color(rive_renderer_paint_t paint, rive_renderer_color_t color)
    {
        auto* handle = ToPaint(paint);
        if (handle == nullptr || !handle->paint)
        {
            SetLastError("paint handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

        handle->paint->color(static_cast<rive::ColorInt>(color));
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_paint_set_thickness(rive_renderer_paint_t paint, float thickness)
    {
        auto* handle = ToPaint(paint);
        if (handle == nullptr || !handle->paint)
        {
            SetLastError("paint handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

        handle->paint->thickness(thickness);
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_paint_set_join(rive_renderer_paint_t paint, rive_renderer_stroke_join_t join)
    {
        auto* handle = ToPaint(paint);
        if (handle == nullptr || !handle->paint)
        {
            SetLastError("paint handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

        rive::StrokeJoin cppJoin;
        if (!ConvertStrokeJoin(join, &cppJoin))
        {
            SetLastError("invalid stroke join");
            return rive_renderer_status_t::invalid_parameter;
        }

        handle->paint->join(cppJoin);
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_paint_set_cap(rive_renderer_paint_t paint, rive_renderer_stroke_cap_t cap)
    {
        auto* handle = ToPaint(paint);
        if (handle == nullptr || !handle->paint)
        {
            SetLastError("paint handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

        rive::StrokeCap cppCap;
        if (!ConvertStrokeCap(cap, &cppCap))
        {
            SetLastError("invalid stroke cap");
            return rive_renderer_status_t::invalid_parameter;
        }

        handle->paint->cap(cppCap);
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_paint_set_feather(rive_renderer_paint_t paint, float feather)
    {
        auto* handle = ToPaint(paint);
        if (handle == nullptr || !handle->paint)
        {
            SetLastError("paint handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

        handle->paint->feather(feather);
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_paint_set_blend_mode(rive_renderer_paint_t      paint,
                                                              rive_renderer_blend_mode_t blend_mode)
    {
        auto* handle = ToPaint(paint);
        if (handle == nullptr || !handle->paint)
        {
            SetLastError("paint handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

        rive::BlendMode cppBlend;
        if (!ConvertBlendMode(blend_mode, &cppBlend))
        {
            SetLastError("invalid blend mode");
            return rive_renderer_status_t::invalid_parameter;
        }

        handle->paint->blendMode(cppBlend);
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_renderer_create(rive_renderer_context_t   context,
                                                         rive_renderer_renderer_t* out_renderer)
    {
        if (out_renderer == nullptr)
        {
            SetLastError("renderer output pointer is null");
            return rive_renderer_status_t::null_pointer;
        }

        auto* ctx = ToContext(context);
        if (ctx == nullptr)
        {
            SetLastError("context handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

        if (!ctx->renderContext)
        {
            SetLastError("render context unavailable");
            return rive_renderer_status_t::unsupported;
        }

        auto renderer = std::make_unique<rive::RiveRenderer>(ctx->renderContext.get());
        if (!renderer)
        {
            SetLastError("renderer allocation failed");
            return rive_renderer_status_t::out_of_memory;
        }

        auto* handle = new (std::nothrow) RendererHandle();
        if (handle == nullptr)
        {
            SetLastError("allocation failed");
            return rive_renderer_status_t::out_of_memory;
        }

        handle->context  = ctx;
        handle->renderer = std::move(renderer);
        ctx->ref_count.fetch_add(1, std::memory_order_relaxed);

        out_renderer->handle = handle;
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_renderer_retain(rive_renderer_renderer_t renderer)
    {
        auto* handle = ToRenderer(renderer);
        if (handle == nullptr)
        {
            SetLastError("renderer handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

        handle->ref_count.fetch_add(1, std::memory_order_relaxed);
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_renderer_release(rive_renderer_renderer_t renderer)
    {
        auto* handle = ToRenderer(renderer);
        if (handle == nullptr)
        {
            SetLastError("renderer handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

        const std::uint32_t previous = handle->ref_count.fetch_sub(1, std::memory_order_acq_rel);
        if (previous == 0)
        {
            SetLastError("renderer handle refcount underflow");
            return rive_renderer_status_t::internal_error;
        }

        if (previous == 1)
        {
            if (handle->context)
            {
                handle->context->ref_count.fetch_sub(1, std::memory_order_acq_rel);
            }
            delete handle;
        }

        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_renderer_save(rive_renderer_renderer_t renderer)
    {
        auto* handle = ToRenderer(renderer);
        if (handle == nullptr || !handle->renderer)
        {
            SetLastError("renderer handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

        handle->renderer->save();
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_renderer_restore(rive_renderer_renderer_t renderer)
    {
        auto* handle = ToRenderer(renderer);
        if (handle == nullptr || !handle->renderer)
        {
            SetLastError("renderer handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

        handle->renderer->restore();
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_renderer_transform(rive_renderer_renderer_t     renderer,
                                                            const rive_renderer_mat2d_t* transform)
    {
        auto* handle = ToRenderer(renderer);
        if (handle == nullptr || !handle->renderer)
        {
            SetLastError("renderer handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

        if (transform == nullptr)
        {
            SetLastError("transform pointer is null");
            return rive_renderer_status_t::null_pointer;
        }

        rive::Mat2D mat = ToMat2D(transform);
        handle->renderer->transform(mat);
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_renderer_draw_path(rive_renderer_renderer_t renderer,
                                                            rive_renderer_path_t path, rive_renderer_paint_t paint)
    {
        auto* rendererHandle = ToRenderer(renderer);
        auto* pathHandle     = ToPath(path);
        auto* paintHandle    = ToPaint(paint);
        if (rendererHandle == nullptr || !rendererHandle->renderer || pathHandle == nullptr || !pathHandle->path ||
            paintHandle == nullptr || !paintHandle->paint)
        {
            SetLastError("renderer/path/paint handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

        rendererHandle->renderer->drawPath(pathHandle->path.get(), paintHandle->paint.get());
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_renderer_clip_path(rive_renderer_renderer_t renderer,
                                                            rive_renderer_path_t     path)
    {
        auto* rendererHandle = ToRenderer(renderer);
        auto* pathHandle     = ToPath(path);
        if (rendererHandle == nullptr || !rendererHandle->renderer || pathHandle == nullptr || !pathHandle->path)
        {
            SetLastError("renderer/path handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

        rendererHandle->renderer->clipPath(pathHandle->path.get());
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_buffer_create(rive_renderer_context_t      context,
                                                       rive_renderer_buffer_type_t  type,
                                                       rive_renderer_buffer_flags_t flags, std::size_t size_in_bytes,
                                                       rive_renderer_buffer_t* out_buffer)
    {
        if (out_buffer == nullptr)
        {
            SetLastError("buffer output pointer is null");
            return rive_renderer_status_t::null_pointer;
        }

        auto* ctx = ToContext(context);
        if (ctx == nullptr)
        {
            SetLastError("context handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

        if (!ctx->renderContext)
        {
            SetLastError("render context unavailable");
            return rive_renderer_status_t::unsupported;
        }

        rive::RenderBufferType bufferType;
        if (!ConvertBufferType(type, &bufferType))
        {
            SetLastError("invalid buffer type");
            return rive_renderer_status_t::invalid_parameter;
        }

        rive::RenderBufferFlags nativeFlags = ConvertBufferFlags(flags);

        rive::rcp<rive::RenderBuffer> buffer =
            ctx->renderContext->makeRenderBuffer(bufferType, nativeFlags, size_in_bytes);
        if (!buffer)
        {
            SetLastError("makeRenderBuffer failed");
            return rive_renderer_status_t::internal_error;
        }

        auto* handle = new (std::nothrow) BufferHandle();
        if (handle == nullptr)
        {
            SetLastError("allocation failed");
            return rive_renderer_status_t::out_of_memory;
        }

        handle->buffer        = std::move(buffer);
        handle->type          = type;
        handle->size_in_bytes = handle->buffer->sizeInBytes();
        out_buffer->handle    = handle;
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_buffer_retain(rive_renderer_buffer_t buffer)
    {
        auto* handle = ToBuffer(buffer);
        if (handle == nullptr || !handle->buffer)
        {
            SetLastError("buffer handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

        handle->ref_count.fetch_add(1, std::memory_order_relaxed);
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_buffer_release(rive_renderer_buffer_t buffer)
    {
        auto* handle = ToBuffer(buffer);
        if (handle == nullptr || !handle->buffer)
        {
            SetLastError("buffer handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

        const std::uint32_t previous = handle->ref_count.fetch_sub(1, std::memory_order_acq_rel);
        if (previous == 0)
        {
            SetLastError("buffer handle refcount underflow");
            return rive_renderer_status_t::internal_error;
        }

        if (previous == 1)
        {
            delete handle;
        }

        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_buffer_upload(rive_renderer_buffer_t buffer, const void* data,
                                                       std::size_t data_length, std::size_t offset)
    {
        auto* handle = ToBuffer(buffer);
        if (handle == nullptr || !handle->buffer)
        {
            SetLastError("buffer handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

        if (data_length == 0)
        {
            ClearLastError();
            return rive_renderer_status_t::ok;
        }

        if (data == nullptr)
        {
            SetLastError("data pointer is null");
            return rive_renderer_status_t::null_pointer;
        }

        if (offset > handle->size_in_bytes || data_length > handle->size_in_bytes ||
            offset + data_length > handle->size_in_bytes)
        {
            SetLastError("upload exceeds buffer bounds");
            return rive_renderer_status_t::invalid_parameter;
        }

        void* mapped = handle->buffer->map();
        if (mapped == nullptr)
        {
            SetLastError("buffer map failed");
            return rive_renderer_status_t::internal_error;
        }

        std::memcpy(static_cast<std::uint8_t*>(mapped) + offset, data, data_length);
        handle->buffer->unmap();
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_buffer_map(rive_renderer_buffer_t buffer,
                                                    rive_renderer_buffer_map_flags_t /*flags*/,
                                                    rive_renderer_mapped_memory_t* out_mapping)
    {
        if (out_mapping == nullptr)
        {
            SetLastError("mapped memory output pointer is null");
            return rive_renderer_status_t::null_pointer;
        }

        auto* handle = ToBuffer(buffer);
        if (handle == nullptr || !handle->buffer)
        {
            SetLastError("buffer handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

        if (handle->mapped_ptr != nullptr)
        {
            SetLastError("buffer is already mapped");
            return rive_renderer_status_t::invalid_parameter;
        }

        void* mapped = handle->buffer->map();
        if (mapped == nullptr)
        {
            SetLastError("buffer map failed");
            return rive_renderer_status_t::internal_error;
        }

        handle->mapped_ptr = mapped;
        out_mapping->data  = mapped;
        out_mapping->length =
            handle->size_in_bytes == 0 ? handle->buffer->sizeInBytes() : handle->size_in_bytes;
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_buffer_unmap(rive_renderer_buffer_t buffer,
                                                      const rive_renderer_mapped_memory_t* /*mapping*/,
                                                      std::size_t written_bytes)
    {
        auto* handle = ToBuffer(buffer);
        if (handle == nullptr || !handle->buffer)
        {
            SetLastError("buffer handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

        if (handle->mapped_ptr == nullptr)
        {
            SetLastError("buffer is not mapped");
            return rive_renderer_status_t::invalid_parameter;
        }

        handle->buffer->unmap();
        handle->mapped_ptr = nullptr;
        static_cast<void>(written_bytes);
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_image_decode(rive_renderer_context_t context, const std::uint8_t* encoded_data,
                                                      std::size_t encoded_length, rive_renderer_image_t* out_image)
    {
        if (out_image == nullptr)
        {
            SetLastError("image output pointer is null");
            return rive_renderer_status_t::null_pointer;
        }

        auto* ctx = ToContext(context);
        if (ctx == nullptr)
        {
            SetLastError("context handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

        if (!ctx->renderContext)
        {
            SetLastError("render context unavailable");
            return rive_renderer_status_t::unsupported;
        }

        if (encoded_data == nullptr || encoded_length == 0)
        {
            SetLastError("encoded image data is invalid");
            return rive_renderer_status_t::invalid_parameter;
        }

        rive::Span<const std::uint8_t> bytes(encoded_data, encoded_length);
        rive::rcp<rive::RenderImage>   image = ctx->renderContext->decodeImage(bytes);
        if (!image)
        {
            SetLastError("decodeImage failed");
            return rive_renderer_status_t::internal_error;
        }

        auto* handle = new (std::nothrow) ImageHandle();
        if (handle == nullptr)
        {
            SetLastError("allocation failed");
            return rive_renderer_status_t::out_of_memory;
        }

        handle->image     = std::move(image);
        out_image->handle = handle;
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_image_retain(rive_renderer_image_t image)
    {
        auto* handle = ToImage(image);
        if (handle == nullptr || !handle->image)
        {
            SetLastError("image handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

        handle->ref_count.fetch_add(1, std::memory_order_relaxed);
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_image_release(rive_renderer_image_t image)
    {
        auto* handle = ToImage(image);
        if (handle == nullptr || !handle->image)
        {
            SetLastError("image handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

        const std::uint32_t previous = handle->ref_count.fetch_sub(1, std::memory_order_acq_rel);
        if (previous == 0)
        {
            SetLastError("image handle refcount underflow");
            return rive_renderer_status_t::internal_error;
        }

        if (previous == 1)
        {
            delete handle;
        }

        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_image_get_size(rive_renderer_image_t image, std::uint32_t* out_width,
                                                        std::uint32_t* out_height)
    {
        auto* handle = ToImage(image);
        if (handle == nullptr || !handle->image)
        {
            SetLastError("image handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

        if (out_width == nullptr || out_height == nullptr)
        {
            SetLastError("size output pointers are null");
            return rive_renderer_status_t::null_pointer;
        }

        *out_width  = static_cast<std::uint32_t>(handle->image->width());
        *out_height = static_cast<std::uint32_t>(handle->image->height());
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_renderer_draw_image(rive_renderer_renderer_t             renderer,
                                                             rive_renderer_image_t                image,
                                                             const rive_renderer_image_sampler_t* sampler,
                                                             rive_renderer_blend_mode_t blend_mode, float opacity)
    {
        auto* rendererHandle = ToRenderer(renderer);
        auto* imageHandle    = ToImage(image);
        if (rendererHandle == nullptr || !rendererHandle->renderer || imageHandle == nullptr || !imageHandle->image)
        {
            SetLastError("renderer/image handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

        rive::BlendMode mode;
        if (!ConvertBlendMode(blend_mode, &mode))
        {
            SetLastError("invalid blend mode");
            return rive_renderer_status_t::invalid_parameter;
        }

        rive::ImageSampler nativeSampler = ConvertImageSampler(sampler);
        rendererHandle->renderer->drawImage(imageHandle->image.get(), nativeSampler, mode, opacity);
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_renderer_draw_image_mesh(
        rive_renderer_renderer_t renderer, rive_renderer_image_t image, const rive_renderer_image_sampler_t* sampler,
        rive_renderer_buffer_t vertices, rive_renderer_buffer_t uvs, rive_renderer_buffer_t indices,
        std::uint32_t vertex_count, std::uint32_t index_count, rive_renderer_blend_mode_t blend_mode, float opacity)
    {
        auto* rendererHandle = ToRenderer(renderer);
        auto* imageHandle    = ToImage(image);
        auto* verticesHandle = ToBuffer(vertices);
        auto* uvsHandle      = ToBuffer(uvs);
        auto* indicesHandle  = ToBuffer(indices);
        if (rendererHandle == nullptr || !rendererHandle->renderer || imageHandle == nullptr || !imageHandle->image ||
            verticesHandle == nullptr || !verticesHandle->buffer || uvsHandle == nullptr || !uvsHandle->buffer ||
            indicesHandle == nullptr || !indicesHandle->buffer)
        {
            SetLastError("renderer/image/buffer handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

        if (verticesHandle->type != rive_renderer_buffer_type_t::vertex ||
            uvsHandle->type != rive_renderer_buffer_type_t::vertex)
        {
            SetLastError("vertex/uv buffers must be vertex buffers");
            return rive_renderer_status_t::invalid_parameter;
        }

        if (indicesHandle->type != rive_renderer_buffer_type_t::index)
        {
            SetLastError("index buffer must be an index buffer");
            return rive_renderer_status_t::invalid_parameter;
        }

        if (vertex_count == 0 || index_count == 0)
        {
            SetLastError("vertex/index counts must be non-zero");
            return rive_renderer_status_t::invalid_parameter;
        }

        rive::BlendMode mode;
        if (!ConvertBlendMode(blend_mode, &mode))
        {
            SetLastError("invalid blend mode");
            return rive_renderer_status_t::invalid_parameter;
        }

        rive::ImageSampler nativeSampler = ConvertImageSampler(sampler);

        rendererHandle->renderer->drawImageMesh(imageHandle->image.get(), nativeSampler, verticesHandle->buffer,
                                                uvsHandle->buffer, indicesHandle->buffer, vertex_count, index_count,
                                                mode, opacity);
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_font_decode(rive_renderer_context_t context, const std::uint8_t* font_data,
                                                     std::size_t font_length, rive_renderer_font_t* out_font)
    {
        if (out_font == nullptr)
        {
            SetLastError("font output pointer is null");
            return rive_renderer_status_t::null_pointer;
        }

#if !defined(WITH_RIVE_TEXT)
        (void) context;
        (void) font_data;
        (void) font_length;
        SetLastError("text support not available");
        return rive_renderer_status_t::unsupported;
#else
        auto* ctx = ToContext(context);
        if (ctx == nullptr)
        {
            SetLastError("context handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

        if (!ctx->renderContext)
        {
            SetLastError("render context unavailable");
            return rive_renderer_status_t::unsupported;
        }

        if (font_data == nullptr || font_length == 0)
        {
            SetLastError("font data is invalid");
            return rive_renderer_status_t::invalid_parameter;
        }

        rive::Span<const std::uint8_t> bytes(font_data, font_length);
        rive::rcp<rive::Font>          font = ctx->renderContext->decodeFont(bytes);
        if (!font)
        {
            SetLastError("decodeFont failed");
            return rive_renderer_status_t::internal_error;
        }

        auto* handle = new (std::nothrow) FontHandle();
        if (handle == nullptr)
        {
            SetLastError("allocation failed");
            return rive_renderer_status_t::out_of_memory;
        }

        handle->font     = std::move(font);
        out_font->handle = handle;
        ClearLastError();
        return rive_renderer_status_t::ok;
#endif
    }

    rive_renderer_status_t rive_renderer_font_retain(rive_renderer_font_t font)
    {
#if !defined(WITH_RIVE_TEXT)
        (void) font;
        SetLastError("text support not available");
        return rive_renderer_status_t::unsupported;
#else
        auto* handle = ToFont(font);
        if (handle == nullptr || !handle->font)
        {
            SetLastError("font handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

        handle->ref_count.fetch_add(1, std::memory_order_relaxed);
        ClearLastError();
        return rive_renderer_status_t::ok;
#endif
    }

    rive_renderer_status_t rive_renderer_font_release(rive_renderer_font_t font)
    {
#if !defined(WITH_RIVE_TEXT)
        (void) font;
        SetLastError("text support not available");
        return rive_renderer_status_t::unsupported;
#else
        auto* handle = ToFont(font);
        if (handle == nullptr || !handle->font)
        {
            SetLastError("font handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

        const std::uint32_t previous = handle->ref_count.fetch_sub(1, std::memory_order_acq_rel);
        if (previous == 0)
        {
            SetLastError("font handle refcount underflow");
            return rive_renderer_status_t::internal_error;
        }

        if (previous == 1)
        {
            delete handle;
        }

        ClearLastError();
        return rive_renderer_status_t::ok;
#endif
    }

    rive_renderer_status_t rive_renderer_text_create_path(rive_renderer_context_t context, rive_renderer_font_t font,
                                                          const char* utf8_text, std::size_t utf8_length,
                                                          const rive_renderer_text_style_t* style,
                                                          rive_renderer_fill_rule_t         fill_rule,
                                                          rive_renderer_path_t*             out_path)
    {
        if (out_path == nullptr)
        {
            SetLastError("path output pointer is null");
            return rive_renderer_status_t::null_pointer;
        }

#if !defined(WITH_RIVE_TEXT)
        (void) context;
        (void) font;
        (void) utf8_text;
        (void) utf8_length;
        (void) style;
        (void) fill_rule;
        SetLastError("text support not available");
        return rive_renderer_status_t::unsupported;
#else
        auto* ctx = ToContext(context);
        if (ctx == nullptr)
        {
            SetLastError("context handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

        if (!ctx->renderContext)
        {
            SetLastError("render context unavailable");
            return rive_renderer_status_t::unsupported;
        }

        auto* fontHandle = ToFont(font);
        if (fontHandle == nullptr || !fontHandle->font)
        {
            SetLastError("font handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

        if (style == nullptr)
        {
            SetLastError("text style pointer is null");
            return rive_renderer_status_t::null_pointer;
        }

        if (utf8_length > 0 && utf8_text == nullptr)
        {
            SetLastError("text pointer is null");
            return rive_renderer_status_t::null_pointer;
        }

        if (style->size <= 0.0f)
        {
            SetLastError("text size must be positive");
            return rive_renderer_status_t::invalid_parameter;
        }

        rive::FillRule rule;
        if (!ConvertFillRule(fill_rule, &rule))
        {
            SetLastError("invalid fill rule");
            return rive_renderer_status_t::invalid_parameter;
        }

        rive::rcp<rive::RenderPath> renderPath = ctx->renderContext->makeEmptyRenderPath();
        if (!renderPath)
        {
            SetLastError("makeRenderPath failed");
            return rive_renderer_status_t::internal_error;
        }
        renderPath->fillRule(rule);

        std::vector<std::uint8_t>  utf8Buffer;
        std::vector<rive::Unichar> codepoints;
        if (utf8_text != nullptr && utf8_length > 0)
        {
            utf8Buffer.resize(utf8_length + 1);
            std::memcpy(utf8Buffer.data(), utf8_text, utf8_length);
            utf8Buffer[utf8_length] = 0;

            const std::uint8_t* cursor = utf8Buffer.data();
            while (*cursor != 0)
            {
                codepoints.push_back(rive::UTF::NextUTF8(&cursor));
            }
        }

        if (!codepoints.empty())
        {
            float lineHeight       = style->line_height > 0.0f ? style->line_height : -1.0f;
            float letterSpacing    = style->letter_spacing;
            float paragraphSpacing = style->paragraph_spacing >= 0.0f ? style->paragraph_spacing : 0.0f;
            float maxWidth         = style->width > 0.0f ? style->width : -1.0f;

            rive::TextAlign align;
            if (!ConvertTextAlign(style->align, &align))
            {
                SetLastError("invalid text alignment");
                return rive_renderer_status_t::invalid_parameter;
            }

            rive::TextWrap wrap;
            if (!ConvertTextWrap(style->wrap, &wrap))
            {
                SetLastError("invalid text wrap mode");
                return rive_renderer_status_t::invalid_parameter;
            }

            std::vector<rive::TextRun> runs;
            runs.resize(1);
            runs[0].font          = fontHandle->font;
            runs[0].size          = style->size;
            runs[0].lineHeight    = lineHeight;
            runs[0].letterSpacing = letterSpacing;
            runs[0].unicharCount  = static_cast<std::uint32_t>(codepoints.size());
            runs[0].script        = 0;
            runs[0].styleId       = 0;
            runs[0].level         = DirectionLevelFromStyle(style->direction);

            rive::Span<const rive::Unichar> textSpan(codepoints.data(), codepoints.size());
            rive::Span<const rive::TextRun> runSpan(runs.data(), runs.size());

            rive::SimpleArray<rive::Paragraph> paragraphs = fontHandle->font->shapeText(textSpan, runSpan);
            if (!paragraphs.empty())
            {
                rive::SimpleArray<rive::SimpleArray<rive::GlyphLine>> lines =
                    rive::Text::BreakLines(paragraphs, maxWidth, align, wrap);

                float baselineShift = 0.0f;
                for (const auto& paragraphLines : lines)
                {
                    if (!paragraphLines.empty())
                    {
                        baselineShift = paragraphLines[0].baseline;
                        break;
                    }
                }

                float paragraphOffset = 0.0f;

                for (std::size_t paragraphIndex = 0; paragraphIndex < paragraphs.size(); ++paragraphIndex)
                {
                    const rive::Paragraph& paragraph      = paragraphs[paragraphIndex];
                    const auto&            paragraphLines = lines[paragraphIndex];

                    for (const rive::GlyphLine& line : paragraphLines)
                    {
                        float renderY = paragraphOffset + line.baseline - baselineShift;

                        float lineWidth = 0.0f;
                        if (!paragraph.runs.empty())
                        {
                            const rive::GlyphRun& endRun   = paragraph.runs[line.endRunIndex];
                            const rive::GlyphRun& startRun = paragraph.runs[line.startRunIndex];
                            lineWidth = endRun.xpos[line.endGlyphIndex] - startRun.xpos[line.startGlyphIndex];
                        }

                        rive::OrderedLine orderedLine(paragraph, line, lineWidth, false, false, nullptr, renderY);
                        float             curX = line.startX;
                        for (auto glyphEntry : orderedLine)
                        {
                            const rive::GlyphRun* glyphRun;
                            std::uint32_t         glyphIndex;
                            std::tie(glyphRun, glyphIndex) = glyphEntry;
                            if (glyphRun == nullptr || glyphIndex >= glyphRun->glyphs.size())
                            {
                                continue;
                            }

                            float         advance   = glyphRun->advances[glyphIndex];
                            rive::RawPath glyphPath = glyphRun->font->getPath(glyphRun->glyphs[glyphIndex]);

                            rive::TransformComponents components;
                            components.scaleX(glyphRun->size);
                            components.scaleY(glyphRun->size);
                            components.x(-advance * 0.5f);

                            rive::Mat2D        glyphMatrix = rive::Mat2D::compose(components);
                            const rive::Vec2D& offset      = glyphRun->offsets[glyphIndex];
                            glyphMatrix =
                                rive::Mat2D::fromTranslate(curX + advance * 0.5f + offset.x, renderY + offset.y) *
                                glyphMatrix;
                            glyphPath.transformInPlace(glyphMatrix);
                            renderPath->addRawPath(glyphPath);
                            curX += advance;
                        }
                    }

                    if (!paragraphLines.empty())
                    {
                        paragraphOffset += paragraphLines.back().bottom - baselineShift;
                    }
                    paragraphOffset += paragraphSpacing;
                }
            }
        }

        auto* handle = new (std::nothrow) PathHandle();
        if (handle == nullptr)
        {
            SetLastError("allocation failed");
            return rive_renderer_status_t::out_of_memory;
        }

        handle->path     = std::move(renderPath);
        out_path->handle = handle;
        ClearLastError();
        return rive_renderer_status_t::ok;
#endif
    }

    rive_renderer_status_t rive_renderer_context_copy_cpu_framebuffer(rive_renderer_context_t context,
                                                                      std::uint8_t*           out_pixels,
                                                                      std::size_t             buffer_length)
    {
        auto* handle = ToContext(context);
        if (handle == nullptr)
        {
            SetLastError("context handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

        if (out_pixels == nullptr)
        {
            SetLastError("output pixel buffer is null");
            return rive_renderer_status_t::null_pointer;
        }

        const std::size_t required = static_cast<std::size_t>(handle->width) * handle->height * 4;
        if (buffer_length < required)
        {
            SetLastError("output buffer too small");
            return rive_renderer_status_t::invalid_parameter;
        }

        if (handle->device == nullptr || handle->device->backend != rive_renderer_backend_t::null)
        {
            SetLastError("cpu framebuffer capture not supported for this backend");
            return rive_renderer_status_t::unsupported;
        }

        if (handle->cpuFramebuffer.size() != required)
        {
            SetLastError("cpu framebuffer not initialized");
            return rive_renderer_status_t::internal_error;
        }

        std::memcpy(out_pixels, handle->cpuFramebuffer.data(), required);
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_shader_linear_gradient_create(rive_renderer_context_t context, float start_x,
                                                                       float start_y, float end_x, float end_y,
                                                                       const rive_renderer_color_t* colors,
                                                                       const float* stops, std::size_t stop_count,
                                                                       rive_renderer_shader_t* out_shader)
    {
        if (out_shader == nullptr)
        {
            SetLastError("shader output pointer is null");
            return rive_renderer_status_t::null_pointer;
        }

        auto* ctx = ToContext(context);
        if (ctx == nullptr)
        {
            SetLastError("context handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

        if (!ctx->renderContext)
        {
            SetLastError("render context unavailable");
            return rive_renderer_status_t::unsupported;
        }

        if (colors == nullptr || stops == nullptr || stop_count == 0)
        {
            SetLastError("gradient colors/stops invalid");
            return rive_renderer_status_t::invalid_parameter;
        }

        std::vector<rive::ColorInt> colorValues(stop_count);
        std::vector<float>          stopValues(stop_count);
        for (std::size_t i = 0; i < stop_count; ++i)
        {
            colorValues[i] = static_cast<rive::ColorInt>(colors[i]);
            stopValues[i]  = stops[i];
        }

        rive::rcp<rive::RenderShader> shader = ctx->renderContext->makeLinearGradient(
            start_x, start_y, end_x, end_y, colorValues.data(), stopValues.data(), stop_count);
        if (!shader)
        {
            SetLastError("makeLinearGradient failed");
            return rive_renderer_status_t::internal_error;
        }

        auto* handle = new (std::nothrow) ShaderHandle();
        if (handle == nullptr)
        {
            SetLastError("allocation failed");
            return rive_renderer_status_t::out_of_memory;
        }

        handle->shader     = std::move(shader);
        out_shader->handle = handle;
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_shader_radial_gradient_create(rive_renderer_context_t context, float center_x,
                                                                       float center_y, float radius,
                                                                       const rive_renderer_color_t* colors,
                                                                       const float* stops, std::size_t stop_count,
                                                                       rive_renderer_shader_t* out_shader)
    {
        if (out_shader == nullptr)
        {
            SetLastError("shader output pointer is null");
            return rive_renderer_status_t::null_pointer;
        }

        auto* ctx = ToContext(context);
        if (ctx == nullptr)
        {
            SetLastError("context handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

        if (!ctx->renderContext)
        {
            SetLastError("render context unavailable");
            return rive_renderer_status_t::unsupported;
        }

        if (colors == nullptr || stops == nullptr || stop_count == 0)
        {
            SetLastError("gradient colors/stops invalid");
            return rive_renderer_status_t::invalid_parameter;
        }

        std::vector<rive::ColorInt> colorValues(stop_count);
        std::vector<float>          stopValues(stop_count);
        for (std::size_t i = 0; i < stop_count; ++i)
        {
            colorValues[i] = static_cast<rive::ColorInt>(colors[i]);
            stopValues[i]  = stops[i];
        }

        rive::rcp<rive::RenderShader> shader = ctx->renderContext->makeRadialGradient(
            center_x, center_y, radius, colorValues.data(), stopValues.data(), stop_count);
        if (!shader)
        {
            SetLastError("makeRadialGradient failed");
            return rive_renderer_status_t::internal_error;
        }

        auto* handle = new (std::nothrow) ShaderHandle();
        if (handle == nullptr)
        {
            SetLastError("allocation failed");
            return rive_renderer_status_t::out_of_memory;
        }

        handle->shader     = std::move(shader);
        out_shader->handle = handle;
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_shader_retain(rive_renderer_shader_t shader)
    {
        auto* handle = ToShader(shader);
        if (handle == nullptr || !handle->shader)
        {
            SetLastError("shader handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

        handle->ref_count.fetch_add(1, std::memory_order_relaxed);
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_shader_release(rive_renderer_shader_t shader)
    {
        auto* handle = ToShader(shader);
        if (handle == nullptr || !handle->shader)
        {
            SetLastError("shader handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

        const std::uint32_t previous = handle->ref_count.fetch_sub(1, std::memory_order_acq_rel);
        if (previous == 0)
        {
            SetLastError("shader handle refcount underflow");
            return rive_renderer_status_t::internal_error;
        }

        if (previous == 1)
        {
            delete handle;
        }

        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_paint_set_shader(rive_renderer_paint_t paint, rive_renderer_shader_t shader)
    {
        auto* paintHandle = ToPaint(paint);
        if (paintHandle == nullptr || !paintHandle->paint)
        {
            SetLastError("paint handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

        auto* shaderHandle = ToShader(shader);
        if (shaderHandle == nullptr || !shaderHandle->shader)
        {
            SetLastError("shader handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

        paintHandle->paint->shader(shaderHandle->shader);
        paintHandle->paint->invalidateStroke();
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_paint_clear_shader(rive_renderer_paint_t paint)
    {
        auto* paintHandle = ToPaint(paint);
        if (paintHandle == nullptr || !paintHandle->paint)
        {
            SetLastError("paint handle is invalid");
            return rive_renderer_status_t::invalid_handle;
        }

        paintHandle->paint->shader(nullptr);
        paintHandle->paint->invalidateStroke();
        ClearLastError();
        return rive_renderer_status_t::ok;
    }

    rive_renderer_status_t rive_renderer_run_self_test()
    {
#if defined(_WIN32) && !defined(RIVE_UNREAL)
        std::size_t            count  = 0;
        rive_renderer_status_t status = rive_renderer_enumerate_adapters(nullptr, 0, &count);
        if (status != rive_renderer_status_t::ok)
        {
            return status;
        }
        if (count == 0)
        {
            SetLastError("no adapters available");
            return rive_renderer_status_t::unsupported;
        }

        std::vector<rive_renderer_adapter_desc_t> adapters(count);
        status = rive_renderer_enumerate_adapters(adapters.data(), adapters.size(), &count);
        if (status != rive_renderer_status_t::ok)
        {
            return status;
        }

        for (std::size_t i = 0; i < count; ++i)
        {
            if (adapters[i].backend != rive_renderer_backend_t::d3d12)
            {
                continue;
            }

            rive_renderer_device_create_info_t info {};
            info.backend       = rive_renderer_backend_t::d3d12;
            info.adapter_index = static_cast<std::uint16_t>(i);
            info.flags         = rive_renderer_device_flags_t::none;

            rive_renderer_device_t device {nullptr};
            status = rive_renderer_device_create(&info, &device);
            if (status != rive_renderer_status_t::ok)
            {
                continue;
            }

            rive_renderer_context_t ctx {nullptr};
            status = rive_renderer_context_create(device, 256, 256, &ctx);
            if (status != rive_renderer_status_t::ok)
            {
                rive_renderer_device_release(device);
                continue;
            }

            rive_renderer_frame_options_t options {};
            options.width  = 256;
            options.height = 256;
            options.vsync  = 0;

            status = rive_renderer_context_begin_frame(ctx, &options);
            if (status == rive_renderer_status_t::ok)
            {
                status = rive_renderer_context_end_frame(ctx);
                if (status == rive_renderer_status_t::ok)
                {
                    status = rive_renderer_context_submit(ctx);
                }
            }

            rive_renderer_context_release(ctx);
            rive_renderer_device_release(device);

            if (status == rive_renderer_status_t::ok)
            {
                ClearLastError();
                return rive_renderer_status_t::ok;
            }
        }

        // Try the headless null backend as a fallback so that CPU-only environments
        // still exercise the ABI surface.
        rive_renderer_device_create_info_t nullInfo {};
        nullInfo.backend = rive_renderer_backend_t::null;
        nullInfo.flags   = rive_renderer_device_flags_t::none;

        rive_renderer_device_t nullDevice {nullptr};
        status = rive_renderer_device_create(&nullInfo, &nullDevice);
        if (status == rive_renderer_status_t::ok)
        {
            rive_renderer_context_t nullCtx {nullptr};
            status = rive_renderer_context_create(nullDevice, 128, 128, &nullCtx);
            if (status == rive_renderer_status_t::ok)
            {
                rive_renderer_frame_options_t options {};
                options.width  = 128;
                options.height = 128;
                options.vsync  = 0;

                status = rive_renderer_context_begin_frame(nullCtx, &options);
                if (status == rive_renderer_status_t::ok)
                {
                    status = rive_renderer_context_end_frame(nullCtx);
                    if (status == rive_renderer_status_t::ok)
                    {
                        status = rive_renderer_context_submit(nullCtx);
                    }
                }
                rive_renderer_context_release(nullCtx);
            }
            rive_renderer_device_release(nullDevice);

            if (status == rive_renderer_status_t::ok)
            {
                ClearLastError();
                return rive_renderer_status_t::ok;
            }
        }

        SetLastError("self test failed for all adapters");
        return rive_renderer_status_t::unsupported;
#else
        SetLastError("self test not supported on this platform");
        return rive_renderer_status_t::unsupported;
#endif
    }

    std::size_t rive_renderer_get_last_error_message(char* buffer, std::size_t buffer_length)
    {
        const std::size_t required = g_lastError.size();
        if (buffer == nullptr || buffer_length == 0)
        {
            return required;
        }

        const std::size_t to_copy = (required < (buffer_length - 1)) ? required : (buffer_length - 1);
        std::memcpy(buffer, g_lastError.data(), to_copy);
        buffer[to_copy] = '\0';
        return required;
    }

    void rive_renderer_clear_last_error()
    {
        ClearLastError();
    }

} // extern "C"
