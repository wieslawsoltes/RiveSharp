#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <TargetConditionals.h>

#if TARGET_OS_OSX
#import <AppKit/AppKit.h>
#else
#import <UIKit/UIKit.h>
#endif

#include <cstdint>
#include <cstring>
#include <memory>

#include "rive_renderer_ffi.h"
#include "rive/renderer/metal/render_context_metal_impl.h"

extern "C" void rive_renderer_set_last_error(const char* message);

namespace
{
    struct RiveMetalDevice
    {
        id<MTLDevice>       device;
        id<MTLCommandQueue> commandQueue;
    };

    struct RiveMetalContext
    {
        RiveMetalDevice*                        device {nullptr};
        rive::gpu::RenderContextMetalImpl*      impl {nullptr};
        id<MTLCommandBuffer>                    commandBuffer {nil};
        bool                                    hasActiveFrame {false};
        rive::rcp<rive::gpu::RenderTarget>      offscreenTarget;
    };

    struct RiveMetalSurface
    {
        RiveMetalDevice*                   device {nullptr};
        RiveMetalContext*                  context {nullptr};
        CAMetalLayer*                      layer {nil};
        rive::rcp<rive::gpu::RenderTarget> renderTarget;
        id<CAMetalDrawable>                currentDrawable {nil};
        std::uint32_t                      sampleCount {1};
        bool                               vsync {true};
    };

    rive::gpu::RenderContextMetalImpl* GetMetalImpl(rive::gpu::RenderContext* renderContext)
    {
        if (renderContext == nullptr)
        {
            return nullptr;
        }
        return renderContext->static_impl_cast<rive::gpu::RenderContextMetalImpl>();
    }

    CGSize MakeLayerSize(std::uint32_t width, std::uint32_t height)
    {
        return CGSizeMake(static_cast<CGFloat>(width), static_cast<CGFloat>(height));
    }
} // namespace

extern "C" void* rive_metal_device_new(rive_renderer_capabilities_t* caps)
{
    @autoreleasepool
    {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (device == nil)
        {
            rive_renderer_set_last_error("MTLCreateSystemDefaultDevice returned null");
            return nullptr;
        }

        id<MTLCommandQueue> queue = [device newCommandQueue];
        if (queue == nil)
        {
            rive_renderer_set_last_error("Failed to create Metal command queue");
            return nullptr;
        }

        auto* metalDevice = new (std::nothrow) RiveMetalDevice();
        if (metalDevice == nullptr)
        {
            rive_renderer_set_last_error("Allocation failed");
            return nullptr;
        }

        metalDevice->device       = device;
        metalDevice->commandQueue = queue;

        if (caps != nullptr)
        {
            caps->backend                  = rive_renderer_backend_t::metal;
            caps->backend_padding          = 0;
            caps->reserved                 = 0;
            caps->feature_flags            = rive_renderer_feature_flags_t::headless_supported;
            caps->max_buffer_size          = 4ull * 1024ull * 1024ull * 1024ull;
            caps->max_texture_dimension    = 16384;
            caps->max_texture_array_layers = 2048;
            caps->max_sampler_anisotropy   = 16.0f;
            caps->supports_hdr             = 0;
            caps->supports_presentation    = 1;
            std::memset(caps->reserved_padding, 0, sizeof(caps->reserved_padding));
            std::memset(caps->reserved_tail, 0, sizeof(caps->reserved_tail));
        }

        return metalDevice;
    }
}

extern "C" void rive_metal_device_release(void* device)
{
    @autoreleasepool
    {
        auto* metalDevice = static_cast<RiveMetalDevice*>(device);
        if (metalDevice == nullptr)
        {
            return;
        }
        metalDevice->commandQueue = nil;
        metalDevice->device       = nil;
        delete metalDevice;
    }
}

extern "C" rive_renderer_status_t
rive_metal_context_create(void* device, std::uint32_t width, std::uint32_t height, void** out_context,
                          std::unique_ptr<rive::gpu::RenderContext>* out_render_context)
{
    if (out_context == nullptr || out_render_context == nullptr)
    {
        rive_renderer_set_last_error("output pointers are null");
        return rive_renderer_status_t::null_pointer;
    }

    @autoreleasepool
    {
        auto* metalDevice = static_cast<RiveMetalDevice*>(device);
        if (metalDevice == nullptr)
        {
            rive_renderer_set_last_error("Metal device handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

        rive::gpu::RenderContextMetalImpl::ContextOptions contextOptions;
        auto renderContext = rive::gpu::RenderContextMetalImpl::MakeContext(metalDevice->device, contextOptions);
        if (!renderContext)
        {
            rive_renderer_set_last_error("RenderContextMetalImpl::MakeContext failed");
            return rive_renderer_status_t::internal_error;
        }

        auto* metalContext = new (std::nothrow) RiveMetalContext();
        if (metalContext == nullptr)
        {
            rive_renderer_set_last_error("Allocation failed");
            return rive_renderer_status_t::out_of_memory;
        }

        metalContext->device = metalDevice;
        metalContext->impl   = GetMetalImpl(renderContext.get());

        if (metalContext->impl == nullptr)
        {
            delete metalContext;
            rive_renderer_set_last_error("RenderContextMetalImpl unavailable");
            return rive_renderer_status_t::internal_error;
        }

        metalContext->offscreenTarget.reset();
        metalContext->commandBuffer = nil;
        metalContext->hasActiveFrame = false;

        *out_context        = metalContext;
        *out_render_context = std::move(renderContext);

        (void)width;
        (void)height;

        return rive_renderer_status_t::ok;
    }
}

extern "C" void rive_metal_context_destroy(void* context)
{
    @autoreleasepool
    {
        auto* metalContext = static_cast<RiveMetalContext*>(context);
        if (metalContext == nullptr)
        {
            return;
        }
        metalContext->commandBuffer  = nil;
        metalContext->offscreenTarget.reset();
        metalContext->impl           = nullptr;
        metalContext->device         = nullptr;
        delete metalContext;
    }
}

extern "C" rive_renderer_status_t
rive_metal_context_begin_frame(void* context, rive::gpu::RenderContext* render_context, std::uint32_t* width,
                               std::uint32_t* height, const rive_renderer_frame_options_t* options, void* surface)
{
    if (context == nullptr || render_context == nullptr || width == nullptr || height == nullptr)
    {
        rive_renderer_set_last_error("invalid arguments to metal begin_frame");
        return rive_renderer_status_t::invalid_handle;
    }

    @autoreleasepool
    {
        auto* metalContext = static_cast<RiveMetalContext*>(context);
        if (metalContext->hasActiveFrame)
        {
            rive_renderer_set_last_error("begin_frame called while frame active");
            return rive_renderer_status_t::invalid_parameter;
        }

        std::uint32_t targetWidth  = *width;
        std::uint32_t targetHeight = *height;
        if (options != nullptr)
        {
            if (options->width != 0)
            {
                targetWidth = options->width;
            }
            if (options->height != 0)
            {
                targetHeight = options->height;
            }
        }

        if (targetWidth == 0 || targetHeight == 0)
        {
            rive_renderer_set_last_error("frame dimensions must be non-zero");
            return rive_renderer_status_t::invalid_parameter;
        }

        id<MTLCommandBuffer> commandBuffer = [metalContext->device->commandQueue commandBuffer];
        if (commandBuffer == nil)
        {
            rive_renderer_set_last_error("failed to create Metal command buffer");
            return rive_renderer_status_t::internal_error;
        }

        metalContext->commandBuffer = commandBuffer;

        rive::rcp<rive::gpu::RenderTarget> target;

        if (surface != nullptr)
        {
            auto* metalSurface = static_cast<RiveMetalSurface*>(surface);
            metalSurface->device  = metalContext->device;
            metalSurface->context = metalContext;

            CAMetalLayer* layer = metalSurface->layer;
            if (layer == nil)
            {
                rive_renderer_set_last_error("CAMetalLayer pointer is null");
                return rive_renderer_status_t::invalid_parameter;
            }

            layer.device        = metalContext->device->device;
            layer.pixelFormat   = MTLPixelFormatBGRA8Unorm;
            layer.framebufferOnly = YES;
            layer.drawableSize  = MakeLayerSize(targetWidth, targetHeight);
#if TARGET_OS_OSX
            layer.contentsScale = [NSScreen mainScreen].backingScaleFactor;
            layer.displaySyncEnabled = metalSurface->vsync;
#else
            layer.contentsScale = [UIScreen mainScreen].scale;
#endif

            metalSurface->currentDrawable = [layer nextDrawable];
            if (metalSurface->currentDrawable == nil)
            {
                rive_renderer_set_last_error("CAMetalLayer did not return a drawable");
                return rive_renderer_status_t::device_lost;
            }

            if (!metalSurface->renderTarget || metalSurface->renderTarget->width() != targetWidth ||
                metalSurface->renderTarget->height() != targetHeight)
            {
                metalSurface->renderTarget =
                    metalContext->impl->makeRenderTarget(MTLPixelFormatBGRA8Unorm, targetWidth, targetHeight);
                if (!metalSurface->renderTarget)
                {
                    rive_renderer_set_last_error("failed to create Metal render target");
                    return rive_renderer_status_t::internal_error;
                }
            }

            if (auto* metalTarget = static_cast<rive::gpu::RenderTargetMetal*>(metalSurface->renderTarget.get()))
            {
                metalTarget->setTargetTexture(metalSurface->currentDrawable.texture);
            }
            target = metalSurface->renderTarget;
        }
        else
        {
            if (!metalContext->offscreenTarget || metalContext->offscreenTarget->width() != targetWidth ||
                metalContext->offscreenTarget->height() != targetHeight)
            {
                metalContext->offscreenTarget =
                    metalContext->impl->makeRenderTarget(MTLPixelFormatBGRA8Unorm, targetWidth, targetHeight);
                if (!metalContext->offscreenTarget)
                {
                    rive_renderer_set_last_error("failed to create Metal offscreen target");
                    return rive_renderer_status_t::internal_error;
                }
            }

            target = metalContext->offscreenTarget;
        }

        rive::gpu::RenderContext::FrameDescriptor descriptor {};
        descriptor.renderTargetWidth  = targetWidth;
        descriptor.renderTargetHeight = targetHeight;
        descriptor.loadAction         = rive::gpu::LoadAction::clear;
        descriptor.clearColor         = 0;
        descriptor.msaaSampleCount    = 0;
        descriptor.disableRasterOrdering = false;

        render_context->beginFrame(descriptor);
        metalContext->hasActiveFrame = true;

        *width  = targetWidth;
        *height = targetHeight;

        return rive_renderer_status_t::ok;
    }
}

extern "C" rive_renderer_status_t
rive_metal_context_end_frame(void* context, rive::gpu::RenderContext* render_context, void* surface)
{
    if (context == nullptr || render_context == nullptr)
    {
        rive_renderer_set_last_error("invalid context in metal end_frame");
        return rive_renderer_status_t::invalid_handle;
    }

    @autoreleasepool
    {
        auto* metalContext = static_cast<RiveMetalContext*>(context);
        if (!metalContext->hasActiveFrame)
        {
            rive_renderer_set_last_error("begin_frame must be called before end_frame");
            return rive_renderer_status_t::invalid_parameter;
        }

        rive::gpu::RenderContext::FlushResources resources {};
        if (surface != nullptr)
        {
            auto* metalSurface = static_cast<RiveMetalSurface*>(surface);
            if (!metalSurface->renderTarget)
            {
                rive_renderer_set_last_error("Metal surface render target missing");
                return rive_renderer_status_t::invalid_handle;
            }
            resources.renderTarget = metalSurface->renderTarget.get();
        }
        else
        {
            if (!metalContext->offscreenTarget)
            {
                rive_renderer_set_last_error("Metal context offscreen target missing");
                return rive_renderer_status_t::invalid_handle;
            }
            resources.renderTarget = metalContext->offscreenTarget.get();
        }

        resources.externalCommandBuffer = (__bridge void*)metalContext->commandBuffer;
        render_context->flush(resources);
        metalContext->hasActiveFrame = false;
        return rive_renderer_status_t::ok;
    }
}

extern "C" rive_renderer_status_t rive_metal_context_submit(void* context, bool has_surface)
{
    @autoreleasepool
    {
        auto* metalContext = static_cast<RiveMetalContext*>(context);
        if (metalContext == nullptr)
        {
            rive_renderer_set_last_error("Metal context handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

        if (!has_surface)
        {
            if (metalContext->commandBuffer != nil)
            {
                [metalContext->commandBuffer commit];
                [metalContext->commandBuffer waitUntilCompleted];
                metalContext->commandBuffer = nil;
            }
        }

        return rive_renderer_status_t::ok;
    }
}

extern "C" rive_renderer_status_t
rive_metal_surface_create(void* device, void* context, const rive_renderer_surface_create_info_metal_layer_t* info,
                          void** out_surface)
{
    if (info == nullptr || out_surface == nullptr)
    {
        rive_renderer_set_last_error("invalid Metal surface arguments");
        return rive_renderer_status_t::null_pointer;
    }

    @autoreleasepool
    {
        auto* metalDevice  = static_cast<RiveMetalDevice*>(device);
        auto* metalContext = static_cast<RiveMetalContext*>(context);
        if (metalDevice == nullptr || metalContext == nullptr)
        {
            rive_renderer_set_last_error("Metal device or context handle is null");
            return rive_renderer_status_t::invalid_handle;
        }

        CAMetalLayer* layer = (__bridge CAMetalLayer*)info->layer;
        if (layer == nil)
        {
            rive_renderer_set_last_error("CAMetalLayer pointer is null");
            return rive_renderer_status_t::invalid_parameter;
        }

        auto* surface = new (std::nothrow) RiveMetalSurface();
        if (surface == nullptr)
        {
            rive_renderer_set_last_error("Allocation failed");
            return rive_renderer_status_t::out_of_memory;
        }

        surface->device      = metalDevice;
        surface->context     = metalContext;
        surface->layer       = layer;
        surface->sampleCount = info->sample_count != 0 ? info->sample_count : 1;
        surface->vsync       = (static_cast<std::uint32_t>(info->flags) &
                          static_cast<std::uint32_t>(rive_renderer_surface_flags_t::enable_vsync)) != 0;

        layer.device          = metalDevice->device;
        layer.pixelFormat     = MTLPixelFormatBGRA8Unorm;
        layer.framebufferOnly = YES;
        layer.drawableSize    = MakeLayerSize(info->width, info->height);
#if TARGET_OS_OSX
        layer.contentsScale     = [NSScreen mainScreen].backingScaleFactor;
        layer.displaySyncEnabled = surface->vsync;
#else
        layer.contentsScale = [UIScreen mainScreen].scale;
#endif

        surface->renderTarget =
            metalContext->impl->makeRenderTarget(MTLPixelFormatBGRA8Unorm,
                                                 info->width == 0 ? 1u : info->width,
                                                 info->height == 0 ? 1u : info->height);
        if (!surface->renderTarget)
        {
            delete surface;
            rive_renderer_set_last_error("failed to create Metal render target");
            return rive_renderer_status_t::internal_error;
        }

        surface->currentDrawable = nil;
        *out_surface             = surface;
        return rive_renderer_status_t::ok;
    }
}

extern "C" void rive_metal_surface_destroy(void* surface)
{
    @autoreleasepool
    {
        auto* metalSurface = static_cast<RiveMetalSurface*>(surface);
        if (metalSurface == nullptr)
        {
            return;
        }

        if (metalSurface->renderTarget)
        {
            if (auto* metalTarget = static_cast<rive::gpu::RenderTargetMetal*>(metalSurface->renderTarget.get()))
            {
                metalTarget->setTargetTexture(nil);
            }
            metalSurface->renderTarget.reset();
        }
        metalSurface->currentDrawable = nil;
        metalSurface->layer           = nil;
        metalSurface->context         = nullptr;
        metalSurface->device          = nullptr;
        delete metalSurface;
    }
}

extern "C" rive_renderer_status_t rive_metal_surface_resize(void* surface, std::uint32_t width, std::uint32_t height)
{
    if (surface == nullptr)
    {
        rive_renderer_set_last_error("Metal surface handle is null");
        return rive_renderer_status_t::invalid_handle;
    }

    if (width == 0 || height == 0)
    {
        rive_renderer_set_last_error("surface dimensions must be non-zero");
        return rive_renderer_status_t::invalid_parameter;
    }

    @autoreleasepool
    {
        auto* metalSurface = static_cast<RiveMetalSurface*>(surface);
        if (metalSurface->layer == nil)
        {
            rive_renderer_set_last_error("CAMetalLayer not initialized");
            return rive_renderer_status_t::invalid_handle;
        }

        metalSurface->layer.drawableSize = MakeLayerSize(width, height);
        metalSurface->renderTarget.reset();
        metalSurface->currentDrawable = nil;
        return rive_renderer_status_t::ok;
    }
}

extern "C" rive_renderer_status_t
rive_metal_surface_present(void* surface, void* context, rive::gpu::RenderContext* render_context,
                           rive_renderer_present_flags_t flags, std::uint32_t present_interval)
{
    (void)render_context;
    (void)present_interval;

    if (surface == nullptr || context == nullptr)
    {
        rive_renderer_set_last_error("Metal surface/context handle is null");
        return rive_renderer_status_t::invalid_handle;
    }

    @autoreleasepool
    {
        auto* metalSurface = static_cast<RiveMetalSurface*>(surface);
        auto* metalContext = static_cast<RiveMetalContext*>(context);

        id<MTLCommandBuffer> commandBuffer = metalContext->commandBuffer;
        if (metalSurface->currentDrawable == nil)
        {
            rive_renderer_set_last_error("no drawable available to present");
            return rive_renderer_status_t::invalid_handle;
        }

        if (commandBuffer == nil)
        {
            commandBuffer = [metalContext->device->commandQueue commandBuffer];
            if (commandBuffer == nil)
            {
                rive_renderer_set_last_error("failed to create Metal command buffer for present");
                return rive_renderer_status_t::internal_error;
            }
        }

        [commandBuffer presentDrawable:metalSurface->currentDrawable];
        [commandBuffer commit];

        bool allowTearing =
            (static_cast<std::uint32_t>(flags) &
             static_cast<std::uint32_t>(rive_renderer_present_flags_t::allow_tearing)) != 0;

        if (!metalSurface->vsync || allowTearing)
        {
            [commandBuffer waitUntilScheduled];
        }

        metalContext->commandBuffer    = nil;
        metalSurface->currentDrawable  = nil;
        if (auto* metalTarget = static_cast<rive::gpu::RenderTargetMetal*>(metalSurface->renderTarget.get()))
        {
            metalTarget->setTargetTexture(nil);
        }
        return rive_renderer_status_t::ok;
    }
}
