# Applies the required warning policy to a target.
function(core_engine_set_warnings target)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(
      ${target}
      PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
  elseif(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
  else()
    message(FATAL_ERROR "Unsupported compiler: ${CMAKE_CXX_COMPILER_ID}")
  endif()
endfunction()
