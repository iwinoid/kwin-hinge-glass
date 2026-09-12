add_executable(hingestate_test hingestate_test.cpp ../src/hingestate.cpp)
target_include_directories(hingestate_test PRIVATE ../src)
target_compile_options(hingestate_test PRIVATE -Wall -Wextra)

enable_testing()
add_test(NAME hingestate COMMAND hingestate_test)

# 集成测试：真的去读本机传感器（无硬件时自行跳过）
add_executable(hingesensor_test hingesensor_test.cpp ../src/hingesensor.cpp)
target_include_directories(hingesensor_test PRIVATE ../src)
target_compile_options(hingesensor_test PRIVATE -Wall -Wextra)
target_link_libraries(hingesensor_test PRIVATE Qt6::Core)
add_test(NAME hingesensor COMMAND hingesensor_test)
