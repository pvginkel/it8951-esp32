# Minimum CMake version required
cmake_minimum_required(VERSION 3.5)

# Project name
project(it8951-esp32-docs)

# Find the Doxygen package
find_package(Doxygen REQUIRED)

if(DOXYGEN_FOUND)
    message(STATUS "Doxygen found: ${DOXYGEN_EXECUTABLE}")

    # Define paths
    # Adjust these paths based on your project structure
    set(PROJECT_NAME "it8951-esp32")
    set(DOXYGEN_INPUT_DIR "${CMAKE_SOURCE_DIR}/../src/include")
    set(DOXYGEN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/docs_output")

    # Configure the Doxyfile by replacing variables in Doxyfile.in
    set(DOXYGEN_CONFIG_FILE "${CMAKE_BINARY_DIR}/Doxyfile")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in" "${DOXYGEN_CONFIG_FILE}" @ONLY)

    # Add a custom target to generate the documentation
    add_custom_target(doc
        COMMAND ${DOXYGEN_EXECUTABLE} "${DOXYGEN_CONFIG_FILE}"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )

    # Optionally, add dependencies or additional targets here
else()
    message(WARNING "Doxygen not found. Documentation will not be generated.")
endif()
