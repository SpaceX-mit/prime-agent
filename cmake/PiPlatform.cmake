# cmake/PiPlatform.cmake
# Detect platform and architecture for prime-agent.

function(pi_detect_platform)
    set(PI_PLATFORM_NAME "unknown")
    set(PI_ARCH "unknown")

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(PI_PLATFORM_NAME "linux")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(PI_PLATFORM_NAME "macos")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(PI_PLATFORM_NAME "windows")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
        set(PI_PLATFORM_NAME "freebsd")
    endif()

    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        set(PI_ARCH "x86_64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(PI_ARCH "aarch64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "riscv64")
        set(PI_ARCH "riscv64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "i.86|x86")
        set(PI_ARCH "x86")
    endif()

    set(PI_PLATFORM_NAME "${PI_PLATFORM_NAME}" PARENT_SCOPE)
    set(PI_ARCH "${PI_ARCH}" PARENT_SCOPE)
endfunction()
