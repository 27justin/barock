set(WAYLAND_SCANNER_EXECUTABLE "wayland-scanner")

function(generate_wayland_protocol TARGET_NAME XML_FILE)
    get_filename_component(PROTOCOL_NAME ${XML_FILE} NAME_WE)

    set(CODE_OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/wl)
    set(HEADER_OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include/wl)

    # Ensure the output directory exists
    file(MAKE_DIRECTORY ${CODE_OUTPUT_DIR})
    file(MAKE_DIRECTORY ${HEADER_OUTPUT_DIR})

    set(SERVER_HEADER ${HEADER_OUTPUT_DIR}/${PROTOCOL_NAME}-protocol.h)
    set(SERVER_CODE   ${CODE_OUTPUT_DIR}/${PROTOCOL_NAME}-protocol.c)

    # Generate server header
    add_custom_command(
        OUTPUT ${SERVER_HEADER}
        COMMAND ${WAYLAND_SCANNER_EXECUTABLE} server-header ${XML_FILE} ${SERVER_HEADER}
        DEPENDS ${XML_FILE}
        COMMENT "Generating ${SERVER_HEADER}"
        VERBATIM
    )

    # Generate protocol code
    add_custom_command(
        OUTPUT ${SERVER_CODE}
        COMMAND ${WAYLAND_SCANNER_EXECUTABLE} private-code ${XML_FILE} ${SERVER_CODE}
        DEPENDS ${XML_FILE}
        COMMENT "Generating ${SERVER_CODE}"
        VERBATIM
    )

    # Tell CMake that the private implementation file has to be
    # compiled with a C compiler, not C++.
    set_source_files_properties(${SERVER_CODE} PROPERTIES LANGUAGE C)

    # Add generated sources to the target
    target_sources(${TARGET_NAME} PRIVATE ${SERVER_CODE} ${SERVER_HEADER})
    target_include_directories(${TARGET_NAME} PRIVATE ${CODE_OUTPUT_DIR})
endfunction()
