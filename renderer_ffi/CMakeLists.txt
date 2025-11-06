cmake_minimum_required(VERSION 3.20)
project(rive_renderer_ffi LANGUAGES CXX)

if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out/${CMAKE_BUILD_TYPE})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out/${CMAKE_BUILD_TYPE})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out/${CMAKE_BUILD_TYPE})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(RIVE_RENDERER_ROOT "" CACHE PATH "Path to the river-renderer source tree")
if(NOT RIVE_RENDERER_ROOT)
    set(_rive_candidates
        "${CMAKE_SOURCE_DIR}/extern/river-renderer"
        "${CMAKE_CURRENT_LIST_DIR}/../extern/river-renderer"
    )
    foreach(_candidate IN LISTS _rive_candidates)
        if(EXISTS "${_candidate}")
            set(RIVE_RENDERER_ROOT "${_candidate}")
            break()
        endif()
    endforeach()
endif()

if(NOT RIVE_RENDERER_ROOT OR NOT EXISTS "${RIVE_RENDERER_ROOT}")
    message(FATAL_ERROR
        "Could not locate the river-renderer submodule. "
        "Set RIVE_RENDERER_ROOT to the river-renderer checkout or ensure extern/river-renderer is present.")
endif()

add_library(rive_renderer_ffi SHARED
    src/rive_renderer_ffi.cpp
)

if(APPLE)
    target_sources(rive_renderer_ffi PRIVATE src/metal_surface.mm)
    set_source_files_properties(src/metal_surface.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    find_library(METAL_FRAMEWORK Metal)
    find_library(FOUNDATION_FRAMEWORK Foundation)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore)
    find_library(APPKIT_FRAMEWORK AppKit)
    target_link_libraries(rive_renderer_ffi PRIVATE
        ${METAL_FRAMEWORK}
        ${FOUNDATION_FRAMEWORK}
        ${QUARTZCORE_FRAMEWORK}
        ${APPKIT_FRAMEWORK}
    )
endif()

target_include_directories(rive_renderer_ffi
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${RIVE_RENDERER_ROOT}/include
        ${RIVE_RENDERER_ROOT}/renderer/include
)

target_compile_definitions(rive_renderer_ffi PRIVATE RIVE_RENDERER_FFI_IMPLEMENTATION)

set(RIVE_RENDERER_OUT_DIRS
    "${RIVE_RENDERER_ROOT}/out"
    "${RIVE_RENDERER_ROOT}/renderer/out"
    "${RIVE_RENDERER_ROOT}/decoders/out"
)

if(WIN32)
    set(_RIVE_LIB_EXT "lib")
else()
    set(_RIVE_LIB_EXT "a")
endif()

function(rive_renderer_locate_library LIB_NAME OUT_VAR)
    set(_candidates "")
    if(WIN32)
        set(_lib_filename "${LIB_NAME}.${_RIVE_LIB_EXT}")
    else()
        set(_lib_filename "lib${LIB_NAME}.${_RIVE_LIB_EXT}")
    endif()

    foreach(_dir IN LISTS RIVE_RENDERER_OUT_DIRS)
        file(GLOB_RECURSE _found
            LIST_DIRECTORIES FALSE
            "${_dir}/**/${_lib_filename}"
        )
        if(_found)
            list(SORT _found)
            list(GET _found 0 _selected)
            set(_candidates "${_selected}")
            break()
        endif()
    endforeach()

    if(NOT _candidates)
        message(FATAL_ERROR
            "Failed to locate ${_lib_filename}. Ensure river-renderer (including renderer sub-project) is built.")
    endif()

    set(${OUT_VAR} "${_candidates}" PARENT_SCOPE)
endfunction()

set(_RIVE_STATIC_LIBS_ORDER
    rive_pls_renderer
    rive
    rive_decoders
    rive_harfbuzz
    rive_sheenbidi
    rive_yoga
    miniaudio
    libpng
    libjpeg
    libwebp
    zlib
)

set(RIVE_RENDERER_NATIVE_LIBS "")
foreach(_lib IN LISTS _RIVE_STATIC_LIBS_ORDER)
    rive_renderer_locate_library("${_lib}" _lib_path)
    list(APPEND RIVE_RENDERER_NATIVE_LIBS "${_lib_path}")
endforeach()

target_link_libraries(rive_renderer_ffi PRIVATE ${RIVE_RENDERER_NATIVE_LIBS})

if (WIN32)
    target_compile_definitions(rive_renderer_ffi PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(rive_renderer_ffi PRIVATE d3d12 dxgi dxguid)
endif()

set_target_properties(rive_renderer_ffi PROPERTIES
    OUTPUT_NAME "rive_renderer_ffi"
)

include(GNUInstallDirs)

install(TARGETS rive_renderer_ffi
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES include/rive_renderer_ffi.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
