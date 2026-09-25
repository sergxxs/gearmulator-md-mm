cmake_minimum_required(VERSION 3.15)

# build a fat binary that runs on both intel and the new Apple M1 chip
# (desktop macOS only - iOS cross builds pass CMAKE_SYSTEM_NAME=iOS plus their
# own architecture/deployment settings via the ios-* presets)
if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
	include(xcodeversion.cmake)

	set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "OS X Architectures")

	message("CMAKE_GENERATOR: " ${CMAKE_GENERATOR})
	message("XCODE_VERSION: " ${XCODE_VERSION})

	# Xcode 14+ can not build for anything < High Sierra anymore
	if(CMAKE_GENERATOR STREQUAL Xcode AND XCODE_VERSION VERSION_GREATER_EQUAL 14.0.0)
		set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum OS X deployment version")
	else()
		# 10.12 is now the minimum because we use std::shared_mutex in patch manager
		set(CMAKE_OSX_DEPLOYMENT_TARGET "10.12" CACHE STRING "Minimum OS X deployment version")
	endif()
	message("CMAKE_OSX_DEPLOYMENT_TARGET: " ${CMAKE_OSX_DEPLOYMENT_TARGET})
endif()

# Use VS 2022 toolset (v143) for Windows 7 compatibility. The VS 2026
# default toolset (v150) statically links CRT functions that import Win8+
# APIs (CopyFile2, CreateFile2), preventing DLL load on Win7.
# Override on command line with -T <toolset> if needed.
if(NOT CMAKE_GENERATOR_TOOLSET AND CMAKE_GENERATOR MATCHES "Visual Studio 18")
	set(CMAKE_GENERATOR_TOOLSET "v143")
endif()

project(gearmulator VERSION 2.2.9)

include(base.cmake)
include(CTest)

option(${PROJECT_NAME}_BUILD_JUCEPLUGIN "Build Juce plugin" on)
option(${PROJECT_NAME}_BUILD_JUCEPLUGIN_CLAP "Build CLAP version of Juce plugin" on)

# ----------------- CPack basic parameters

message("CMAKE_SYSTEM_NAME: " ${CMAKE_SYSTEM_NAME})
message("CMAKE_SYSTEM_PROCESSOR: " ${CMAKE_SYSTEM_PROCESSOR})

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
	set(CPACK_SYSTEM_NAME ${CMAKE_SYSTEM_NAME}_${CMAKE_SYSTEM_PROCESSOR})
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
	set(CPACK_SYSTEM_NAME "MacOS")
endif()

if(NOT DEFINED CPACK_SYSTEM_NAME)
	if(WIN32)
		if(CMAKE_SIZEOF_VOID_P EQUAL 8)
			set(CPACK_SYSTEM_NAME "win64")
		else()
			set(CPACK_SYSTEM_NAME "win32")
		endif()
	elseif(APPLE)
		set(CPACK_SYSTEM_NAME "MacOS")
	else()
		set(CPACK_SYSTEM_NAME "${CMAKE_SYSTEM_NAME}_${CMAKE_SYSTEM_PROCESSOR}")
	endif()
endif()
message("CPACK_SYSTEM_NAME: " ${CPACK_SYSTEM_NAME})

set(CPACK_PACKAGE_CONTACT "The Usual Suspects")
set(CPACK_PACKAGE_VENDOR "The Usual Suspects")
set(CPACK_PACKAGE_NAME "TheUsualSuspects")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "The Usual Suspects emulated audio plugin")

set(CPACK_DEBIAN_PACKAGE_MAINTAINER "The Usual Suspects")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://dsp56300.wordpress.com")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

set(CPACK_RPM_PACKAGE_AUTOREQ "yes")
set(CPACK_RPM_PACKAGE_URL ${CPACK_DEBIAN_PACKAGE_HOMEPAGE})
set(CPACK_RPM_PACKAGE_DESCRIPTION ${CPACK_PACKAGE_DESCRIPTION_SUMMARY})

# ----------------- source

# The dsp56300 submodule (and its nested asmjit submodule) pin upstream commits
# that use APIs the iOS SDK marks unavailable (mach_vm_* in CowMemory) or miss
# an include on iOS (sys_icache_invalidate in asmjit's virtmem.cpp). Until the
# fixes are upstreamed, apply local overlay patches at configure time.
# Idempotent: skipped when already applied; desktop/macOS/Linux builds never
# run this and the patches keep macOS behavior byte-for-byte identical.
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
	find_package(Git REQUIRED)
	function(gearmulator_apply_ios_patch _patch _dir)
		execute_process(
			COMMAND "${GIT_EXECUTABLE}" apply --check "${_patch}"
			WORKING_DIRECTORY "${_dir}"
			RESULT_VARIABLE _applicable
			ERROR_QUIET OUTPUT_QUIET)
		if(_applicable EQUAL 0)
			execute_process(
				COMMAND "${GIT_EXECUTABLE}" apply "${_patch}"
				WORKING_DIRECTORY "${_dir}"
				RESULT_VARIABLE _result)
			if(NOT _result EQUAL 0)
				message(FATAL_ERROR "Failed to apply iOS patch ${_patch} in ${_dir}")
			endif()
			message(STATUS "iOS patch applied: ${_patch}")
		else()
			execute_process(
				COMMAND "${GIT_EXECUTABLE}" apply --check --reverse "${_patch}"
				WORKING_DIRECTORY "${_dir}"
				RESULT_VARIABLE _applied
				ERROR_QUIET OUTPUT_QUIET)
			if(_applied EQUAL 0)
				message(STATUS "iOS patch already applied: ${_patch}")
			else()
				message(FATAL_ERROR "iOS patch ${_patch} neither applies nor is applied; check the state of ${_dir}")
			endif()
		endif()
	endfunction()
	gearmulator_apply_ios_patch(
		"${CMAKE_CURRENT_SOURCE_DIR}/patches/dsp56300-ios-cowmemory.patch"
		"${CMAKE_CURRENT_SOURCE_DIR}/source/dsp56300")
	gearmulator_apply_ios_patch(
		"${CMAKE_CURRENT_SOURCE_DIR}/patches/asmjit-ios-icache.patch"
		"${CMAKE_CURRENT_SOURCE_DIR}/source/dsp56300/source/asmjit")
endif()

add_subdirectory(source)
add_subdirectory(doc)

# ----------------- CPack parameters based on source

get_cmake_property(CPACK_COMPONENTS_ALL COMPONENTS)
list(REMOVE_ITEM CPACK_COMPONENTS_ALL "Unspecified")

# set better archive names
foreach(comp IN LISTS CPACK_COMPONENTS_ALL)
	string(TOUPPER "${comp}" COMP_UPPER)

	set(archiveName "${CPACK_PACKAGE_NAME}-${comp}-${CMAKE_PROJECT_VERSION}-${CPACK_SYSTEM_NAME}")

	set(varName "CPACK_ARCHIVE_${COMP_UPPER}_FILE_NAME")
	set(${varName} ${archiveName})

	set(varName "CPACK_DEBIAN_${COMP_UPPER}_FILE_NAME")
	set(${varName} "${archiveName}.deb")

	set(varName "CPACK_RPM_${COMP_UPPER}_FILE_NAME")
	set(${varName} "${archiveName}.rpm")
endforeach()

# add list of all targets that we want to upload
get_property(CPACK_TUS_TARGETS GLOBAL PROPERTY "TUS_TARGETS")

# add target properties that we need for CPack / upload
foreach(target IN LISTS CPACK_TUS_TARGETS)
	get_target_property(comp "${target}" TUS_CMAKE_COMPONENT)
	if(comp)
		set(CPACK_TUS_${target}_CMAKE_COMPONENT ${comp})
	endif()
	get_target_property(prodName "${target}" TUS_PRODUCT_NAME)
	if(prodName)
		set(CPACK_TUS_${target}_PRODUCT_NAME ${prodName})
	endif()
endforeach()

set(CPACK_TUS_SOURCE_DIR ${CMAKE_SOURCE_DIR})

set(CPACK_COMPONENTS_GROUPING IGNORE)

set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_RPM_COMPONENT_INSTALL ON)

if(UNIX AND NOT APPLE)
	set(CPACK_PACKAGING_INSTALL_PREFIX /usr/local)
endif()

include(CPack)
