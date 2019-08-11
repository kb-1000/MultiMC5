find_package(JNI REQUIRED)

# Called Minecraft to trick some drivers
add_executable(Minecraft WIN32 javalauncher.c)
target_include_directories(Minecraft SYSTEM PRIVATE ${JNI_INCLUDE_DIRS})
target_link_libraries(Minecraft ${JNI_LIBRARIES})

# TODO: add install targets
