#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#if defined(RIVE_RENDERER_FFI_IMPLEMENTATION)
#define RIVE_RENDERER_FFI_EXPORT __declspec(dllexport)
#else
#define RIVE_RENDERER_FFI_EXPORT __declspec(dllimport)
#endif
#else
#define RIVE_RENDERER_FFI_EXPORT __attribute__((visibility("default")))
#endif

extern "C"
{

    static constexpr std::size_t RIVE_RENDERER_MAX_ADAPTER_NAME = 256;

    enum class rive_renderer_status_t : std::int32_t
    {
        ok                = 0,
        null_pointer      = -1,
        invalid_handle    = -2,
        invalid_parameter = -3,
        out_of_memory     = -4,
        unsupported       = -5,
        device_lost       = -6,
        unimplemented     = -7,
        internal_error    = -8,
    };

    enum class rive_renderer_backend_t : std::uint8_t
    {
        unknown = 0,
        null    = 1,
        metal   = 2,
        vulkan  = 3,
        d3d12   = 4,
        d3d11   = 5,
        opengl  = 6,
        webgpu  = 7,
    };

    enum class rive_renderer_device_flags_t : std::uint32_t
    {
        none                 = 0,
        enable_validation    = 1 << 0,
        enable_debug_markers = 1 << 1,
        enable_diagnostics   = 1 << 2,
        headless             = 1 << 3,
    };

    enum class rive_renderer_feature_flags_t : std::uint32_t
    {
        none                    = 0,
        raster_ordering         = 1 << 0,
        atomic_path_rendering   = 1 << 1,
        clockwise_fill          = 1 << 2,
        advanced_blend          = 1 << 3,
        advanced_blend_coherent = 1 << 4,
        clip_planes             = 1 << 5,
        bottom_up_framebuffer   = 1 << 6,
        headless_supported      = 1 << 7,
    };

#pragma pack(push, 1)
    struct rive_renderer_adapter_desc_t
    {
        rive_renderer_backend_t backend;
        std::uint8_t            backend_padding;
        std::uint16_t           vendor_id;
        std::uint16_t           device_id;
        std::uint16_t           subsys_id;
        std::uint16_t           revision;
        std::uint64_t           dedicated_video_memory;
        std::uint64_t           shared_system_memory;
        std::uint32_t           flags;
        std::uint32_t           reserved;
        char                    name[RIVE_RENDERER_MAX_ADAPTER_NAME];
        std::uint8_t            reserved_padding[14];
    };

    struct rive_renderer_device_create_info_t
    {
        rive_renderer_backend_t      backend;
        std::uint8_t                 backend_padding;
        std::uint16_t                adapter_index;
        rive_renderer_device_flags_t flags;
    };

    struct rive_renderer_capabilities_t
    {
        rive_renderer_backend_t       backend;
        std::uint8_t                  backend_padding;
        std::uint16_t                 reserved;
        rive_renderer_feature_flags_t feature_flags;
        std::uint64_t                 max_buffer_size;
        std::uint32_t                 max_texture_dimension;
        std::uint32_t                 max_texture_array_layers;
        float                         max_sampler_anisotropy;
        std::uint8_t                  supports_hdr;
        std::uint8_t                  supports_presentation;
        std::uint8_t                  reserved_padding[6];
        std::uint8_t                  reserved_tail[4];
    };

    struct rive_renderer_frame_options_t
    {
        std::uint32_t width;
        std::uint32_t height;
        float         delta_time_ms;
        std::uint8_t  vsync;
        std::uint8_t  reserved[3];
    };
#pragma pack(pop)

    enum class rive_renderer_surface_flags_t : std::uint32_t
    {
        none          = 0,
        enable_vsync  = 1 << 0,
        allow_tearing = 1 << 1,
    };

    enum class rive_renderer_present_flags_t : std::uint32_t
    {
        none          = 0,
        allow_tearing = 1 << 0,
    };

#pragma pack(push, 1)
    struct rive_renderer_surface_create_info_d3d12_hwnd_t
    {
        void*                           hwnd;
        std::uint32_t                   width;
        std::uint32_t                   height;
        std::uint32_t                   buffer_count;
        rive_renderer_surface_flags_t   flags;
        std::uint32_t                   present_interval;
    };

    struct rive_renderer_surface_create_info_metal_layer_t
    {
        void*                         layer;
        std::uint32_t                 width;
        std::uint32_t                 height;
        std::uint32_t                 sample_count;
        rive_renderer_surface_flags_t flags;
    };

    struct rive_renderer_surface_create_info_vulkan_t
    {
        void*                         surface;
        std::uint32_t                 width;
        std::uint32_t                 height;
        std::uint32_t                 min_image_count;
        std::uint32_t                 present_mode;
        rive_renderer_surface_flags_t flags;
    };

    struct rive_renderer_vulkan_features_t
    {
        std::uint32_t api_version;
        std::uint8_t  independent_blend;
        std::uint8_t  fill_mode_non_solid;
        std::uint8_t  fragment_stores_and_atomics;
        std::uint8_t  shader_clip_distance;
        std::uint8_t  rasterization_order_color_attachment_access;
        std::uint8_t  fragment_shader_pixel_interlock;
        std::uint8_t  portability_subset;
        std::uint8_t  reserved[1];
    };

    typedef void* (*rive_renderer_vk_get_instance_proc_addr_t)(void* instance, const char* name);

    struct rive_renderer_device_create_info_vulkan_t
    {
        void*                                  instance;
        void*                                  physical_device;
        void*                                  device;
        rive_renderer_vulkan_features_t        features;
        rive_renderer_vk_get_instance_proc_addr_t get_instance_proc_addr;
        void*                                  graphics_queue;
        std::uint32_t                          graphics_queue_family_index;
        void*                                  present_queue;
        std::uint32_t                          present_queue_family_index;
        void*                                  allocator_callbacks;
    };
#pragma pack(pop)

    struct rive_renderer_surface_t
    {
        void* handle;
    };

    struct rive_renderer_fence_t
    {
        void* handle;
    };

    struct rive_renderer_mapped_memory_t
    {
        void*        data;
        std::size_t  length;
    };

    struct rive_renderer_device_t
    {
        void* handle;
    };

    struct rive_renderer_context_t
    {
        void* handle;
    };

    enum class rive_renderer_fill_rule_t : std::uint8_t
    {
        non_zero  = 0,
        even_odd  = 1,
        clockwise = 2,
    };

    enum class rive_renderer_paint_style_t : std::uint8_t
    {
        fill   = 0,
        stroke = 1,
    };

    enum class rive_renderer_buffer_type_t : std::uint8_t
    {
        index  = 0,
        vertex = 1,
    };

    enum class rive_renderer_buffer_flags_t : std::uint32_t
    {
        none                          = 0,
        mapped_once_at_initialization = 1 << 0,
    };

    enum class rive_renderer_buffer_map_flags_t : std::uint32_t
    {
        none             = 0,
        invalidate_range = 1 << 0,
        discard_range    = 1 << 1,
    };

    enum class rive_renderer_image_filter_t : std::uint8_t
    {
        bilinear = 0,
        nearest  = 1,
    };

    enum class rive_renderer_image_wrap_t : std::uint8_t
    {
        clamp  = 0,
        repeat = 1,
        mirror = 2,
    };

    struct rive_renderer_image_sampler_t
    {
        rive_renderer_image_wrap_t   wrap_x;
        rive_renderer_image_wrap_t   wrap_y;
        rive_renderer_image_filter_t filter;
        std::uint8_t                 reserved;
    };

    enum class rive_renderer_stroke_cap_t : std::uint8_t
    {
        butt   = 0,
        round  = 1,
        square = 2,
    };

    enum class rive_renderer_stroke_join_t : std::uint8_t
    {
        miter = 0,
        round = 1,
        bevel = 2,
    };

    enum class rive_renderer_blend_mode_t : std::uint8_t
    {
        src_over    = 3,
        screen      = 14,
        overlay     = 15,
        darken      = 16,
        lighten     = 17,
        color_dodge = 18,
        color_burn  = 19,
        hard_light  = 20,
        soft_light  = 21,
        difference  = 22,
        exclusion   = 23,
        multiply    = 24,
        hue         = 25,
        saturation  = 26,
        color       = 27,
        luminosity  = 28,
    };

    typedef std::uint32_t rive_renderer_color_t;

    struct rive_renderer_mat2d_t
    {
        float xx;
        float xy;
        float yx;
        float yy;
        float tx;
        float ty;
    };

    struct rive_renderer_path_t
    {
        void* handle;
    };

    struct rive_renderer_paint_t
    {
        void* handle;
    };

    struct rive_renderer_renderer_t
    {
        void* handle;
    };

    struct rive_renderer_buffer_t
    {
        void* handle;
    };

    struct rive_renderer_image_t
    {
        void* handle;
    };

    struct rive_renderer_font_t
    {
        void* handle;
    };

    struct rive_renderer_shader_t
    {
        void* handle;
    };

    enum class rive_renderer_text_align_t : std::uint8_t
    {
        left   = 0,
        right  = 1,
        center = 2,
    };

    enum class rive_renderer_text_wrap_t : std::uint8_t
    {
        wrap    = 0,
        no_wrap = 1,
    };

    enum class rive_renderer_text_direction_t : std::uint8_t
    {
        automatic = 0,
        ltr       = 1,
        rtl       = 2,
    };

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t
    rive_renderer_enumerate_adapters(rive_renderer_adapter_desc_t* adapters, std::size_t capacity, std::size_t* count);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t
    rive_renderer_device_create(const rive_renderer_device_create_info_t* info, rive_renderer_device_t* out_device);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t
    rive_renderer_device_create_vulkan(const rive_renderer_device_create_info_vulkan_t* info,
                                       rive_renderer_device_t* out_device);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_device_retain(rive_renderer_device_t device);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_device_release(rive_renderer_device_t device);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t
    rive_renderer_device_capabilities(rive_renderer_device_t device, rive_renderer_capabilities_t* out_capabilities);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_context_create(rive_renderer_device_t   device,
                                                                                 std::uint32_t            width,
                                                                                 std::uint32_t            height,
                                                                                 rive_renderer_context_t* out_context);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_context_retain(rive_renderer_context_t context);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_context_release(rive_renderer_context_t context);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_context_get_size(rive_renderer_context_t context,
                                                                                   std::uint32_t*          out_width,
                                                                                   std::uint32_t*          out_height);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_context_resize(rive_renderer_context_t context,
                                                                                 std::uint32_t           width,
                                                                                 std::uint32_t           height);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t
    rive_renderer_context_begin_frame(rive_renderer_context_t context, const rive_renderer_frame_options_t* options);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_context_end_frame(rive_renderer_context_t context);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_context_submit(rive_renderer_context_t context);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t
    rive_renderer_surface_create_d3d12_hwnd(rive_renderer_device_t device, rive_renderer_context_t context,
                                            const rive_renderer_surface_create_info_d3d12_hwnd_t* info,
                                            rive_renderer_surface_t* out_surface);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t
    rive_renderer_surface_create_metal_layer(rive_renderer_device_t device, rive_renderer_context_t context,
                                             const rive_renderer_surface_create_info_metal_layer_t* info,
                                             rive_renderer_surface_t* out_surface);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t
    rive_renderer_surface_create_vulkan(rive_renderer_device_t device,
                                        rive_renderer_context_t context,
                                        const rive_renderer_surface_create_info_vulkan_t* info,
                                        rive_renderer_surface_t* out_surface);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_surface_retain(rive_renderer_surface_t surface);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_surface_release(rive_renderer_surface_t surface);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_surface_get_size(rive_renderer_surface_t surface,
                                                                                   std::uint32_t*          out_width,
                                                                                   std::uint32_t*          out_height);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_surface_resize(rive_renderer_surface_t surface,
                                                                                 std::uint32_t            width,
                                                                                 std::uint32_t            height);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t
    rive_renderer_surface_present(rive_renderer_surface_t surface, std::uint32_t present_interval,
                                  rive_renderer_present_flags_t flags);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_fence_create(rive_renderer_device_t device,
                                                                               rive_renderer_fence_t* out_fence);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_fence_retain(rive_renderer_fence_t fence);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_fence_release(rive_renderer_fence_t fence);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t
    rive_renderer_fence_get_completed_value(rive_renderer_fence_t fence, std::uint64_t* out_value);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t
    rive_renderer_fence_wait(rive_renderer_fence_t fence, std::uint64_t value, std::uint64_t timeout_ms);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t
    rive_renderer_context_signal_fence(rive_renderer_context_t context, rive_renderer_fence_t fence,
                                       std::uint64_t value);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_path_create(rive_renderer_context_t   context,
                                                                              rive_renderer_fill_rule_t fill_rule,
                                                                              rive_renderer_path_t*     out_path);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_path_retain(rive_renderer_path_t path);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_path_release(rive_renderer_path_t path);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_path_rewind(rive_renderer_path_t path);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t
    rive_renderer_path_set_fill_rule(rive_renderer_path_t path, rive_renderer_fill_rule_t fill_rule);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_path_move_to(rive_renderer_path_t path, float x,
                                                                               float y);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_path_line_to(rive_renderer_path_t path, float x,
                                                                               float y);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_path_cubic_to(rive_renderer_path_t path, float ox,
                                                                                float oy, float ix, float iy, float x,
                                                                                float y);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_path_close(rive_renderer_path_t path);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_path_add_path(rive_renderer_path_t destination,
                                                                                rive_renderer_path_t source,
                                                                                const rive_renderer_mat2d_t* transform);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_paint_create(rive_renderer_context_t context,
                                                                               rive_renderer_paint_t*  out_paint);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_paint_retain(rive_renderer_paint_t paint);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_paint_release(rive_renderer_paint_t paint);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_paint_set_style(rive_renderer_paint_t       paint,
                                                                                  rive_renderer_paint_style_t style);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_paint_set_color(rive_renderer_paint_t paint,
                                                                                  rive_renderer_color_t color);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_paint_set_thickness(rive_renderer_paint_t paint,
                                                                                      float                 thickness);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_paint_set_join(rive_renderer_paint_t       paint,
                                                                                 rive_renderer_stroke_join_t join);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_paint_set_cap(rive_renderer_paint_t      paint,
                                                                                rive_renderer_stroke_cap_t cap);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_paint_set_feather(rive_renderer_paint_t paint,
                                                                                    float                 feather);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t
    rive_renderer_paint_set_blend_mode(rive_renderer_paint_t paint, rive_renderer_blend_mode_t blend_mode);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t
    rive_renderer_renderer_create(rive_renderer_context_t context, rive_renderer_renderer_t* out_renderer);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_renderer_retain(rive_renderer_renderer_t renderer);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_renderer_release(rive_renderer_renderer_t renderer);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_renderer_save(rive_renderer_renderer_t renderer);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_renderer_restore(rive_renderer_renderer_t renderer);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t
    rive_renderer_renderer_transform(rive_renderer_renderer_t renderer, const rive_renderer_mat2d_t* transform);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_renderer_draw_path(rive_renderer_renderer_t renderer,
                                                                                     rive_renderer_path_t     path,
                                                                                     rive_renderer_paint_t    paint);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_renderer_clip_path(rive_renderer_renderer_t renderer,
                                                                                     rive_renderer_path_t     path);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_buffer_create(rive_renderer_context_t      context,
                                                                                rive_renderer_buffer_type_t  type,
                                                                                rive_renderer_buffer_flags_t flags,
                                                                                std::size_t             size_in_bytes,
                                                                                rive_renderer_buffer_t* out_buffer);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_buffer_retain(rive_renderer_buffer_t buffer);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_buffer_release(rive_renderer_buffer_t buffer);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_buffer_upload(rive_renderer_buffer_t buffer,
                                                                                const void*            data,
                                                                                std::size_t            data_length,
                                                                                std::size_t            offset);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t
    rive_renderer_buffer_map(rive_renderer_buffer_t buffer, rive_renderer_buffer_map_flags_t flags,
                             rive_renderer_mapped_memory_t* out_mapping);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t
    rive_renderer_buffer_unmap(rive_renderer_buffer_t buffer, const rive_renderer_mapped_memory_t* mapping,
                               std::size_t written_bytes);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_image_decode(rive_renderer_context_t context,
                                                                               const std::uint8_t*     encoded_data,
                                                                               std::size_t             encoded_length,
                                                                               rive_renderer_image_t*  out_image);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_image_retain(rive_renderer_image_t image);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_image_release(rive_renderer_image_t image);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_image_get_size(rive_renderer_image_t image,
                                                                                 std::uint32_t*        out_width,
                                                                                 std::uint32_t*        out_height);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_renderer_draw_image(
        rive_renderer_renderer_t renderer, rive_renderer_image_t image, const rive_renderer_image_sampler_t* sampler,
        rive_renderer_blend_mode_t blend_mode, float opacity);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_renderer_draw_image_mesh(
        rive_renderer_renderer_t renderer, rive_renderer_image_t image, const rive_renderer_image_sampler_t* sampler,
        rive_renderer_buffer_t vertices, rive_renderer_buffer_t uvs, rive_renderer_buffer_t indices,
        std::uint32_t vertex_count, std::uint32_t index_count, rive_renderer_blend_mode_t blend_mode, float opacity);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_font_decode(rive_renderer_context_t context,
                                                                              const std::uint8_t*     font_data,
                                                                              std::size_t             font_length,
                                                                              rive_renderer_font_t*   out_font);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_font_retain(rive_renderer_font_t font);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_font_release(rive_renderer_font_t font);

#pragma pack(push, 1)
    struct rive_renderer_text_style_t
    {
        float                          size;
        float                          line_height;
        float                          letter_spacing;
        float                          width;
        float                          paragraph_spacing;
        rive_renderer_text_align_t     align;
        rive_renderer_text_wrap_t      wrap;
        rive_renderer_text_direction_t direction;
        std::uint8_t                   reserved;
    };
#pragma pack(pop)

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_text_create_path(
        rive_renderer_context_t context, rive_renderer_font_t font, const char* utf8_text, std::size_t utf8_length,
        const rive_renderer_text_style_t* style, rive_renderer_fill_rule_t fill_rule, rive_renderer_path_t* out_path);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_context_copy_cpu_framebuffer(
        rive_renderer_context_t context, std::uint8_t* out_pixels, std::size_t buffer_length);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_shader_linear_gradient_create(
        rive_renderer_context_t context, float start_x, float start_y, float end_x, float end_y,
        const rive_renderer_color_t* colors, const float* stops, std::size_t stop_count,
        rive_renderer_shader_t* out_shader);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t
    rive_renderer_shader_radial_gradient_create(rive_renderer_context_t context, float center_x, float center_y,
                                                float radius, const rive_renderer_color_t* colors, const float* stops,
                                                std::size_t stop_count, rive_renderer_shader_t* out_shader);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_shader_retain(rive_renderer_shader_t shader);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_shader_release(rive_renderer_shader_t shader);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_paint_set_shader(rive_renderer_paint_t  paint,
                                                                                   rive_renderer_shader_t shader);

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_paint_clear_shader(rive_renderer_paint_t paint);

    RIVE_RENDERER_FFI_EXPORT std::size_t rive_renderer_get_last_error_message(char* buffer, std::size_t buffer_length);

    RIVE_RENDERER_FFI_EXPORT void rive_renderer_clear_last_error();

    RIVE_RENDERER_FFI_EXPORT rive_renderer_status_t rive_renderer_run_self_test();

} // extern "C"

static_assert(sizeof(rive_renderer_adapter_desc_t) == 304, "Adapter descriptor size mismatch");
static_assert(sizeof(rive_renderer_capabilities_t) == 40, "Capabilities size mismatch");
static_assert(sizeof(rive_renderer_device_create_info_t) == 8, "Device create info size mismatch");
static_assert(sizeof(rive_renderer_vulkan_features_t) == 12, "Vulkan features size mismatch");
static_assert(sizeof(rive_renderer_device_create_info_vulkan_t) == 76,
              "Vulkan device create info size mismatch");
static_assert(sizeof(rive_renderer_surface_create_info_vulkan_t) == 28,
              "Vulkan surface create info size mismatch");
static_assert(sizeof(rive_renderer_frame_options_t) == 16, "Frame options size mismatch");
static_assert(sizeof(rive_renderer_text_style_t) == 24, "Text style size mismatch");
