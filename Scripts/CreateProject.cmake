# Usage: cmake -P Scripts/CreateProject.cmake -- <ProjectName> [EXECUTABLE|LIBRARY]
get_filename_component(ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# Find arguments passed after "--"
set(POS_ARGS "")
set(AFTER_DASH FALSE)
math(EXPR LAST_IDX "${CMAKE_ARGC} - 1")
foreach(I RANGE 0 ${LAST_IDX})
    if(AFTER_DASH)
        list(APPEND POS_ARGS "${CMAKE_ARGV${I}}")
    elseif(CMAKE_ARGV${I} STREQUAL "--")
        set(AFTER_DASH TRUE)
    endif()
endforeach()

list(LENGTH POS_ARGS ARG_COUNT)
if(ARG_COUNT LESS 1)
    message(FATAL_ERROR "Usage: cmake -P Scripts/CreateProject.cmake -- <ProjectName> [EXECUTABLE|LIBRARY]")
endif()

list(GET POS_ARGS 0 PROJ_NAME)
set(TARGET_TYPE "EXECUTABLE")
if(ARG_COUNT GREATER_EQUAL 2)
    list(GET POS_ARGS 1 TARGET_TYPE)
endif()

set(PROJ_DIR "${ROOT_DIR}/${PROJ_NAME}")
if(EXISTS "${PROJ_DIR}/Target.cmake")
    message(FATAL_ERROR "Project '${PROJ_NAME}' already exists at ${PROJ_DIR}")
endif()

# 1. Create Project Target.cmake
file(MAKE_DIRECTORY "${PROJ_DIR}/Source")
file(WRITE "${PROJ_DIR}/Target.cmake"
        "# Mandatory Project Target Descriptor\n"
        "set(TARGET_TYPE \"${TARGET_TYPE}\") # Options: EXECUTABLE or LIBRARY\n"
        "set(ENTRY_POINT_MODULE \"Engine::Launch\") # Module providing EngineMain()\n"
)

# 2. Scaffold the mandatory default module (${PROJ_NAME}::${PROJ_NAME})
set(MOD_DIR "${PROJ_DIR}/Source/${PROJ_NAME}")
file(MAKE_DIRECTORY "${MOD_DIR}/Public")
file(MAKE_DIRECTORY "${MOD_DIR}/Private")
file(WRITE "${MOD_DIR}/Build.cmake"
        "# Module Build Dependencies & Boot Stage\n"
        "# Loading Phases: PreDefault, Default, PostEngineInit\n"
        "set(LOADING_PHASE \"PostEngineInit\")\n\n"
        "set(PUBLIC_DEPENDENCY_MODULES\n"
        ")\n\n"
        "set(PRIVATE_DEPENDENCY_MODULES\n"
        "    Engine::Core\n"
        ")\n\n"
        "set(PUBLIC_THIRD_PARTY_DEPENDENCIES\n"
        ")\n\n"
        "set(THIRD_PARTY_DEPENDENCIES\n"
        ")\n"
)

message(STATUS "[VEngine] Created project '${PROJ_NAME}' (${TARGET_TYPE}) with default module '${PROJ_NAME}::${PROJ_NAME}'.")
message(STATUS "[VEngine] Reload CMake to add it to your IDE.")