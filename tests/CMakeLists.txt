find_package(Catch2 3 REQUIRED HINTS /opt/homebrew)

# Placeholder test until Task 2 adds real tests
add_executable(test_placeholder test_placeholder.cpp)
target_link_libraries(test_placeholder PRIVATE Catch2::Catch2WithMain)
add_test(NAME test_placeholder COMMAND test_placeholder)
