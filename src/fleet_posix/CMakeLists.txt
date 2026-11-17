cmake_minimum_required(VERSION 3.8)
project(fleet_posix)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

find_package(ament_cmake REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(rclcpp REQUIRED)
find_package(Threads REQUIRED)
find_package(turtlesim REQUIRED)

add_executable(fleet_supervisor src/supervisor.cpp)
target_compile_features(fleet_supervisor PRIVATE cxx_std_17)
target_include_directories(fleet_supervisor PRIVATE include)
ament_target_dependencies(fleet_supervisor ament_index_cpp)
target_link_libraries(fleet_supervisor Threads::Threads rt)

add_executable(amr_agent src/amr_agent.cpp)
target_compile_features(amr_agent PRIVATE cxx_std_17)
target_include_directories(amr_agent PRIVATE include)
ament_target_dependencies(amr_agent geometry_msgs rclcpp turtlesim)
target_link_libraries(amr_agent Threads::Threads rt)

install(TARGETS fleet_supervisor amr_agent
  DESTINATION lib/${PROJECT_NAME}
)

install(PROGRAMS scripts/demo.sh
  DESTINATION lib/${PROJECT_NAME}
)

install(FILES README.md
  DESTINATION share/${PROJECT_NAME}
)

ament_package()
