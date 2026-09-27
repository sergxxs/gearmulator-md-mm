# Script-mode (cmake -P) variant of the iOS overlay patch application used at
# build time. The overlays live in submodule working trees, so a later
# `git submodule update` can silently undo them after configuration; running
# this before the patched libraries compile makes `cmake --build` on its own
# always sufficient.
#
# Required arguments:
#   -DGIT_EXECUTABLE=<git>  -DPATCH_FILE=<patch>  -DWORK_DIR=<dir>
#
# Behavior (identical to gearmulator_apply_ios_patch in the top-level
# CMakeLists.txt): apply when cleanly applicable, succeed silently when already
# applied, fail loudly on any other state - never build silently wrong code.

foreach(_required GIT_EXECUTABLE PATCH_FILE WORK_DIR)
	if(NOT DEFINED ${_required})
		message(FATAL_ERROR "applyIosPatch.cmake: missing -D${_required}=")
	endif()
endforeach()

execute_process(
	COMMAND "${GIT_EXECUTABLE}" apply --check "${PATCH_FILE}"
	WORKING_DIRECTORY "${WORK_DIR}"
	RESULT_VARIABLE _applicable
	ERROR_QUIET OUTPUT_QUIET)

if(_applicable EQUAL 0)
	execute_process(
		COMMAND "${GIT_EXECUTABLE}" apply "${PATCH_FILE}"
		WORKING_DIRECTORY "${WORK_DIR}"
		RESULT_VARIABLE _result)
	if(NOT _result EQUAL 0)
		message(FATAL_ERROR "Failed to apply iOS patch ${PATCH_FILE} in ${WORK_DIR}")
	endif()
	message(STATUS "iOS patch applied: ${PATCH_FILE}")
	return()
endif()

execute_process(
	COMMAND "${GIT_EXECUTABLE}" apply --check --reverse "${PATCH_FILE}"
	WORKING_DIRECTORY "${WORK_DIR}"
	RESULT_VARIABLE _applied
	ERROR_QUIET OUTPUT_QUIET)

if(NOT _applied EQUAL 0)
	message(FATAL_ERROR "iOS patch ${PATCH_FILE} neither applies nor is applied. "
		"The working tree in ${WORK_DIR} has local modifications; discard them with\n"
		"  git -C \"${WORK_DIR}\" checkout -- .\n"
		"and re-run the build so the patch can be applied cleanly.")
endif()
