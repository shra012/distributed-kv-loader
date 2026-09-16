# Configures optional address, undefined-behavior, and thread sanitizers.
set(CORE_ENGINE_SANITIZER "None" CACHE STRING "Sanitizer mode: None, Asan, or Tsan")
set_property(CACHE CORE_ENGINE_SANITIZER PROPERTY STRINGS None Asan Tsan)

function(core_engine_enable_sanitizers target)
  if(CORE_ENGINE_SANITIZER STREQUAL "None")
    return()
  endif()

  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    message(FATAL_ERROR "Sanitizers require Clang or GCC")
  endif()

  if(CORE_ENGINE_SANITIZER STREQUAL "Asan")
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  elseif(CORE_ENGINE_SANITIZER STREQUAL "Tsan")
    target_compile_options(${target} PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
  else()
    message(FATAL_ERROR "Invalid CORE_ENGINE_SANITIZER: ${CORE_ENGINE_SANITIZER}")
  endif()
endfunction()
