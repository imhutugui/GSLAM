cmake_minimum_required(VERSION 2.8)
project(svar_nsq)

find_package(eCAL REQUIRED)
include(PICMake)
#include(${CMAKE_CURRENT_LIST_DIR}/cmake/FindeCAL.cmake)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

include_directories(ecal)
add_library(svar_ecal SHARED ecal/main.cpp ecal/json_serializer.cpp ecal/cbor_serializer.cpp)
add_executable(ecal_main ecal/main.cpp ecal/json_serializer.cpp ecal/cbor_serializer.cpp)
add_executable(ecal_pub ecal/pub.cpp ecal/json_serializer.cpp ecal/cbor_serializer.cpp)
target_link_libraries(svar_ecal eCAL::core)
target_link_libraries(ecal_main eCAL::core pthread dl)
target_link_libraries(ecal_pub eCAL::core pthread dl)

pi_report_target()

# Now do make install
pi_install(TARGETS svar_nsq)
