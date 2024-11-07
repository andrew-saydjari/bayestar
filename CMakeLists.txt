cmake_minimum_required(VERSION 2.9)
project(bayestar)

set_property(GLOBAL PROPERTY FIND_LIBRARY_USE_LIB64_PATHS TRUE)

#
# Use pkg-config to find some libraries
#
INCLUDE(FindPkgConfig)
find_package(PkgConfig REQUIRED)

#
# Check requirements
#
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake/modules)

# Git version
IF(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/.git)
	FIND_PACKAGE(Git)
	IF(GIT_FOUND)
		EXECUTE_PROCESS(
			COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
			WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
			OUTPUT_VARIABLE "GIT_BUILD_VERSION"
			ERROR_QUIET
			OUTPUT_STRIP_TRAILING_WHITESPACE)
		MESSAGE( STATUS "Git version: ${GIT_BUILD_VERSION}" )
	ELSE(GIT_FOUND)
		SET(GIT_BUILD_VERSION 0)
	ENDIF(GIT_FOUND)
ELSE(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/.git)
	SET(GIT_BUILD_VERSION "$ENV{BAYESTARCOMMIT}")
ENDIF(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/.git)

#
# Configure a header file to pass some of the CMake
# settings to the source code
#
set(DATADIR "${CMAKE_SOURCE_DIR}/data/")
configure_file(
	"${PROJECT_SOURCE_DIR}/src/bayestar_config.h.in"
	"${PROJECT_BINARY_DIR}/src/bayestar_config.h"
	ESCAPE_QUOTES
)
include_directories("${PROJECT_BINARY_DIR}/src")

#
# Profiling
#
IF(CMAKE_COMPILER_IS_GNUCXX)
	IF(PROFILING_GEN)
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-generate=prof -lgcov ")
		MESSAGE( STATUS "Setting flags to generate profiling information." )
	ENDIF(PROFILING_GEN)
	IF(PROFILING_USE)
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-use=prof ")
		MESSAGE( STATUS "Setting flags to use profiling information." )
	ENDIF(PROFILING_USE)
ENDIF(CMAKE_COMPILER_IS_GNUCXX)

#
# Optimizations
#
IF(NOT CMAKE_COMPILER_IS_GNUCXX)
	set(CMAKE_CXX_COMPILER icx)
	# set(CMAKE_CXX_COMPILER icc)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -ipo -xHOST -fp-model precise ")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 ")
ELSE(NOT CMAKE_COMPILER_IS_GNUCXX)
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} -O3 -march=native ")
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS} -O1 -g ")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --std=c++14 ")
ENDIF(NOT CMAKE_COMPILER_IS_GNUCXX)

MESSAGE( STATUS "CMAKE_COMPILER_IS_GNUCXX: ${CMAKE_COMPILER_IS_GNUCXX}" )
MESSAGE( STATUS "CMAKE_CXX_FLAGS_RELEASE: ${CMAKE_CXX_FLAGS_RELEASE}" )

### HDF5
find_package(HDF5 COMPONENTS C CXX HL REQUIRED)
include_directories(${HDF5_INCLUDE_DIRS})
MESSAGE( STATUS "HDF5_INCLUDE_DIRS: ${HDF5_INCLUDE_DIRS}" )
MESSAGE( STATUS "HDF5_LIBRARY_DIRS: ${HDF5_LIBRARY_DIRS}" )
MESSAGE( STATUS "HDF5_LIBRARIES: ${HDF5_LIBRARIES}" )
MESSAGE( STATUS "HDF5_CXX_LIBRARIES: ${HDF5_CXX_LIBRARIES}" )

### OpenCV
find_package(OpenCV 4 REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})
link_directories(${OpenCV_LIBRARY_DIRS})
MESSAGE( STATUS "OPENCV_LIBRARY_DIRS: ${OpenCV_LIBRARY_DIRS}" )
MESSAGE( STATUS "OPENCV_INCLUDE_DIRS: ${OpenCV_INCLUDE_DIRS}" )
MESSAGE( STATUS "OPENCV_LIBRARIES: ${OpenCV_LIBRARIES}" )

### Boost
find_package(Boost REQUIRED COMPONENTS program_options)
include_directories(${Boost_INCLUDE_DIRS})
MESSAGE( STATUS "Boost_INCLUDE_DIRS: ${Boost_INCLUDE_DIRS}" )
MESSAGE( STATUS "Boost_LIBRARIES: ${Boost_LIBRARIES}" )

### OpenMP support
IF(NOT CMAKE_COMPILER_IS_GNUCXX)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -qopenmp")
ELSE(NOT CMAKE_COMPILER_IS_GNUCXX)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fopenmp")
ENDIF(NOT CMAKE_COMPILER_IS_GNUCXX)

### EIGEN3
pkg_check_modules(EIGEN3 eigen3)
#find_package(Eigen3 REQUIRED NO_MODULES)
include_directories(${EIGEN3_INCLUDE_DIRS})
MESSAGE( STATUS "EIGEN3_INCLUDE_DIRS: ${EIGEN3_INCLUDE_DIRS}" )
MESSAGE( STATUS "EIGEN3_LIBRARIES: ${EIGEN3_LIBRARIES}" )

### GSL
pkg_check_modules(GSL gsl)
#find_package(GSL REQUIRED)
MESSAGE( STATUS "GSL_INCLUDE_DIRS: ${GSL_INCLUDE_DIR}" )
MESSAGE( STATUS "GSL_LIBRARY_DIRS: ${GSL_LIBRARY_DIRS}" )
include_directories(${GSL_INCLUDE_DIR})
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DHAVE_INLINE=1 -DGSL_RANGE_CHECK=0")

### Fixed-size types
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D__STDC_LIMIT_MACROS")

#
# Executable
#
add_executable(bayestar src/main.cpp src/model.cpp src/sampler.cpp
                        src/interpolation.cpp src/stats.cpp src/chain.cpp
                        src/data.cpp src/binner.cpp src/los_sampler.cpp
                        src/h5utils.cpp src/star_exact.cpp
                        src/program_opts.cpp src/gaussian_process.cpp
                        src/healpix_tree.cpp src/neighbor_pixels.cpp
			src/bridging_sampler.cpp)

#
# Link libraries
#
target_link_libraries(bayestar rt)
target_link_libraries(bayestar ${HDF5_LIBRARIES} stdc++)
target_link_libraries(bayestar ${GSL_LIBRARIES})
target_link_libraries(bayestar ${Boost_LIBRARIES})
target_link_libraries(bayestar ${OpenCV_LIBRARIES})
#target_link_libraries(bayestar ${OpenMP_LIBRARIES})
target_link_libraries(bayestar ${EIGEN3_LIBRARIES})
IF(ADDITIONAL_LINK_DIRS)
	target_link_libraries(bayestar -L${ADDITIONAL_LINK_DIRS})
    MESSAGE( STATUS "Additional link directories: ${ADDITIONAL_LINK_DIRS}" )
ENDIF(ADDITIONAL_LINK_DIRS)
