if (GLIBC_COMPATIBILITY)
    add_subdirectory(memcpy)
    if(TARGET memcpy)
        set(MEMCPY_LIBRARY memcpy)
    endif()

    enable_language(ASM)
    include(CheckIncludeFile)

    check_include_file("sys/random.h" HAVE_SYS_RANDOM_H)

    add_headers_and_sources(glibc_compatibility .)
    add_headers_and_sources(glibc_compatibility musl)
    if (ARCH_AARCH64)
        list (APPEND glibc_compatibility_sources musl/aarch64/syscall.s musl/aarch64/longjmp.s)
        set (musl_arch_include_dir musl/aarch64)
    elseif (ARCH_AMD64)
        list (APPEND glibc_compatibility_sources musl/x86_64/syscall.s musl/x86_64/longjmp.s)
        set (musl_arch_include_dir musl/x86_64)
    else ()
        message (FATAL_ERROR "glibc_compatibility can only be used on x86_64 or aarch64.")
    endif ()

    list(REMOVE_ITEM glibc_compatibility_sources musl/getentropy.c)
    if(HAVE_SYS_RANDOM_H)
        list(APPEND glibc_compatibility_sources musl/getentropy.c)
    endif()

    # Need to omit frame pointers to match the performance of glibc
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fomit-frame-pointer")

    add_library(glibc-compatibility STATIC ${glibc_compatibility_sources})

    target_no_warning(glibc-compatibility unused-command-line-argument)
    target_no_warning(glibc-compatibility unused-but-set-variable)
    target_no_warning(glibc-compatibility builtin-requires-header)

    target_include_directories(glibc-compatibility PRIVATE libcxxabi ${musl_arch_include_dir})

    if (( NOT USE_STATIC_LIBRARIES AND NOT USE_STATIC_LIBRARIES ) OR ENABLE_OPENSSL_DYNAMIC)
        target_compile_options(glibc-compatibility PRIVATE -fPIC)
    endif ()

    target_link_libraries(global-libs INTERFACE glibc-compatibility ${MEMCPY_LIBRARY})

    install(
        TARGETS glibc-compatibility ${MEMCPY_LIBRARY}
        EXPORT global
        ARCHIVE DESTINATION lib
    )

    message (STATUS "Some symbols from glibc will be replaced for compatibility")

elseif (CLICKHOUSE_OFFICIAL_BUILD)
    message (WARNING "Option GLIBC_COMPATIBILITY must be turned on for production builds.")
endif ()
