# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.
cmake_minimum_required (VERSION 3.13)

project(HemeLB)

set(HEMELB_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)

list(INSERT CMAKE_MODULE_PATH 0 "${HEMELB_ROOT_DIR}/CMake")
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake" "${HEMELB_DEPENDENCIES_PATH}/Modules/")
list(REMOVE_DUPLICATES CMAKE_MODULE_PATH)

# Set the language standard strictly.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

#---- OPTION switches accepted by the build -------
include(GlobalOptions)
include(HemeLbOptions)

# Add warnings flags to development build types
if (HEMELB_USE_ALL_WARNINGS_GNU)
  set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
  set( CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Wall")
endif()

add_definitions(-DHEMELB_CODE)
add_definitions(-DHEMELB_COMPUTE_ARCHITECTURE=${HEMELB_COMPUTE_ARCHITECTURE})

if(HEMELB_VALIDATE_GEOMETRY)
  add_definitions(-DHEMELB_VALIDATE_GEOMETRY)
endif()

if (HEMELB_USE_SSE3)
  add_definitions(-DHEMELB_USE_SSE3)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse3")
endif()

if (HEMELB_BUILD_RBC)
  add_definitions(-DHEMELB_BUILD_RBC)
endif()

if (HEMELB_CALLGRIND)
  add_definitions(-DHEMELB_CALLGRIND)
endif()

if (HEMELB_USE_VELOCITY_WEIGHTS_FILE)
  add_definitions(-DHEMELB_USE_VELOCITY_WEIGHTS_FILE)
endif()

if (HEMELB_USE_KRUEGER_ORDERING)
  add_definitions(-DHEMELB_USE_KRUEGER_ORDERING)
endif()

list(APPEND CMAKE_PREFIX_PATH ${HEMELB_DEPENDENCIES_INSTALL_PREFIX})
# list(APPEND CMAKE_INCLUDE_PATH ${HEMELB_DEPENDENCIES_INSTALL_PREFIX}/include)
# list(APPEND CMAKE_LIBRARY_PATH ${HEMELB_DEPENDENCIES_INSTALL_PREFIX}/lib)

#--- Set up runtime search path for DLLs -----
if(HEMELB_DEPENDENCIES_SET_RPATH)
  SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
  SET(CMAKE_INSTALL_RPATH "${HEMELB_DEPENDENCIES_INSTALL_PREFIX}/lib")
endif()

include(build_environment)
include(platform_checks)
include(codesign)
include(mpi)
include(HemeLbDependency)

find_hemelb_dependency(Boost REQUIRED)
find_hemelb_dependency(TinyXML REQUIRED)

find_hemelb_dependency(ParMETIS REQUIRED)
find_hemelb_dependency(CTemplate REQUIRED)
find_hemelb_dependency(ZLIB REQUIRED)

# MPI and boost should always be available
# Note this is NOT the C++ bindings, this is the C bindings from C++
# (ensures helpful MPI implementations do not sneak the MPI 2 C++ bindings back in)
link_libraries(MPI::MPI_CXX)
link_libraries(Boost::headers)

if(HEMELB_BUILD_RBC)
  # Work around some installs of HDF5 having proper targets and others
  # not...
  include(UseHDF5)
  find_hemelb_dependency(VTK REQUIRED)
endif()

#-------------Resources -----------------------

set(BUILD_RESOURCE_PATH ${PROJECT_BINARY_DIR}/resources)
file(MAKE_DIRECTORY ${BUILD_RESOURCE_PATH})
set(INSTALL_RESOURCE_PATH ${CMAKE_INSTALL_PREFIX}/share/hemelb/resources)

function(add_to_resources)
  unset(donotexist)
  set(hasmissing FALSE)
  foreach(resource ${ARGN})
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${resource}")
      set(donotexist "${donotexist}\n - ${CMAKE_CURRENT_SOURCE_DIR}/${resource}")
      set(hasmissing TRUE)
    else()
      configure_file(${CMAKE_CURRENT_SOURCE_DIR}/${resource} ${BUILD_RESOURCE_PATH} COPYONLY)
      get_filename_component(resource_leaf ${resource} NAME)
      install(FILES ${BUILD_RESOURCE_PATH}/${resource_leaf} DESTINATION share/hemelb/resources)
    endif()
  endforeach()
  if(hasmissing)
    message(WARNING "The following resources are missing:${donotexist}")
  endif()
endfunction()

configure_file (
  "${PROJECT_SOURCE_DIR}/resources/path_parameters.h.in"
  "${PROJECT_BINARY_DIR}/resources/path_parameters.h"
  )

pass_get_defines(GLOBAL conf_global)
pass_get_defines(HEMELB conf_hemelb)
configure_file(build_info.h.in build_info.h)

# add the binary tree to the search path for include files
# so that we will find TutorialConfig.h
include_directories("${PROJECT_BINARY_DIR}")

# ----------- HemeLB ------------------
if (APPLE)
  add_definitions(-DHEMELB_CFG_ON_BSD -DHEMELB_CFG_ON_OSX)
endif()
if (HEMELB_USE_DEBUGGER)
  # Force a debug build, because the debugger makes no sense without debug symbols
  set(CMAKE_BUILD_TYPE Debug)
endif()

add_executable(${HEMELB_EXECUTABLE} main.cc)
codesign(${HEMELB_EXECUTABLE})

include_directories(${PROJECT_SOURCE_DIR})
set(package_subdirs
  configuration
  redblood
  extraction
  lb
  geometry
  debug
  util
  log
  net
  io
  reporting
  )

if (HEMELB_BUILD_COLLOIDS)
  list(APPEND package_subdirs colloids)
endif()

foreach(subdir ${package_subdirs})
  set(lib "hemelb_${subdir}")
  list(APPEND heme_libraries ${lib})
  add_subdirectory(${subdir})
endforeach()
add_subdirectory(resources)
target_link_libraries(${HEMELB_EXECUTABLE} 
  ${heme_libraries} ${heme_libraries}
  )

# Config XML checker (confcheck)
add_executable(${HEMELB_EXECUTABLE}-confcheck confcheck.cc)
target_link_libraries(${HEMELB_EXECUTABLE}-confcheck
  ${heme_libraries} ${heme_libraries}
  )

INSTALL(TARGETS ${HEMELB_EXECUTABLE} ${HEMELB_EXECUTABLE}-confcheck RUNTIME DESTINATION bin)
add_to_resources(resources/report.txt.ctp resources/report.xml.ctp)


# ----------- HemeLB Multiscale ------------------
if (HEMELB_BUILD_MULTISCALE)
  if (APPLE)
    add_definitions(-DHEMELB_CFG_ON_BSD -DHEMELB_CFG_ON_OSX)
  endif()
  if (HEMELB_USE_DEBUGGER)
    # Force a debug build, because the debugger makes no sense without debug symbols
    set(CMAKE_BUILD_TYPE DEBUG)
  endif()
  
  add_executable(multiscale_hemelb mainMultiscale.cc)
  codesign(multiscale_hemelb)

  include_directories(${PROJECT_SOURCE_DIR})

  add_subdirectory(multiscale)
  target_link_libraries(multiscale_hemelb
    PRIVATE ${heme_libraries} hemelb_multiscale
    )
  INSTALL(TARGETS multiscale_hemelb RUNTIME DESTINATION bin)
  add_to_resources(resources/report.txt.ctp resources/report.xml.ctp)
endif()

# ----------- HEMELB unittests ---------------
if(HEMELB_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
