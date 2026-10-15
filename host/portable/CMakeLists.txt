# The portable host kit: builds the ZeroFG Independent Presenter of
# host/presenter, unchanged, in any Android host, as the static library `zerofg_host`
# (zerofg::host). Android only: the presenter moves frames as AHardwareBuffers
# with sync files and presents on an ANativeWindow.
#
# The host passes:
#   - ZEROFG_HOST_VULKAN_HEADERS_DIR: the Vulkan headers it builds with;
#   - ZEROFG_HOST_FMT_TARGET: a {fmt} target (default fmt::fmt);
# and links zerofg::host. See README.md.

if(NOT ANDROID)
  message(FATAL_ERROR "The ZeroFG portable host kit builds for Android only")
endif()

set(ZEROFG_HOST_VULKAN_HEADERS_DIR "" CACHE PATH
    "Directory holding vulkan/vulkan.h for the host kit")
set(ZEROFG_HOST_FMT_TARGET "fmt::fmt" CACHE STRING
    "The {fmt} target the host kit's logging links")
if(NOT ZEROFG_HOST_VULKAN_HEADERS_DIR)
  message(FATAL_ERROR "Set ZEROFG_HOST_VULKAN_HEADERS_DIR")
endif()
if(NOT TARGET ${ZEROFG_HOST_FMT_TARGET})
  message(FATAL_ERROR "The {fmt} target '${ZEROFG_HOST_FMT_TARGET}' does not exist")
endif()

# The presenter includes its own headers as xenia/ui/vulkan/zerofg_*.h, so they
# are staged under that path. configure_file re-runs the configure when one
# changes.
set(_zerofg_host_reference "${CMAKE_CURRENT_SOURCE_DIR}/../presenter")
set(_zerofg_host_stage "${CMAKE_CURRENT_BINARY_DIR}/stage")
foreach(_file
    zerofg_build_guard.h
    zerofg_completion_owner.h
    zerofg_config.h
    zerofg_device_handoff.h
    zerofg_device_handoff.inc
    zerofg_independent_presenter.h
    zerofg_main_surface_egress.h)
  configure_file("${_zerofg_host_reference}/${_file}"
                 "${_zerofg_host_stage}/xenia/ui/vulkan/${_file}" COPYONLY)
endforeach()

add_library(zerofg_host STATIC
  src/host.cc
  src/portable_device.cc
  src/device_handoff.cc
  "${_zerofg_host_reference}/zerofg_independent_presenter.cc"
  "${_zerofg_host_reference}/zerofg_main_surface_egress.cc"
)
add_library(zerofg::host ALIAS zerofg_host)

target_include_directories(zerofg_host
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${_zerofg_host_stage}"
    "${ZEROFG_HOST_VULKAN_HEADERS_DIR}"
)
# Private: a host's own sources keep their own Vulkan switches. The kit's public
# header defines VK_USE_PLATFORM_ANDROID_KHR itself when vulkan.h comes in
# through it first (vulkan_device.h).
target_compile_definitions(zerofg_host
  PRIVATE VK_USE_PLATFORM_ANDROID_KHR VK_NO_PROTOTYPES)
target_compile_features(zerofg_host PUBLIC cxx_std_20)
target_link_libraries(zerofg_host PUBLIC ${ZEROFG_HOST_FMT_TARGET} android log)
