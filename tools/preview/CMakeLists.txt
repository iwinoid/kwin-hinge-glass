add_executable(hinge_glass_preview main.cpp)
target_include_directories(hinge_glass_preview PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_options(hinge_glass_preview PRIVATE -Wall -Wextra)
target_link_libraries(hinge_glass_preview PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::OpenGL
)

add_executable(hinge_glass_lodprobe lodprobe.cpp)
target_compile_options(hinge_glass_lodprobe PRIVATE -Wall -Wextra)
target_link_libraries(hinge_glass_lodprobe PRIVATE Qt6::Core Qt6::Gui Qt6::OpenGL)
