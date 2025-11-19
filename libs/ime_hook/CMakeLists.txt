qt_add_library(ime_hook SHARED
    src/ime_hook.cpp
    include/ime_hook/ime_hook.hpp
)

target_include_directories(ime_hook
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(ime_hook
    PRIVATE
        IME_HOOK_EXPORTS
)

target_link_libraries(ime_hook
    PRIVATE
        imm32
)

set_target_properties(ime_hook PROPERTIES
    OUTPUT_NAME "ime_hook"
    AUTOMOC_COMPILER_PREDEFINES OFF
)

