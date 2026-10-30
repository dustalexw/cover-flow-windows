foreach(name tst_lyrics tst_library tst_lookup tst_player tst_decoder tst_engine)
    qt_add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE coverflow_core Qt6::Test)
    # Console programs on Windows so test output reaches ctest.
    set_target_properties(${name} PROPERTIES WIN32_EXECUTABLE FALSE)
    target_compile_definitions(${name} PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()
