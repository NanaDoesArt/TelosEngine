# Usage: cmake -P Scripts/CreateModule.cmake -- <ProjectName> <ModuleName> [PreDefault|Default|PostEngineInit]
#
# Writes only Build.cmake. <Module>.h and <Module>.cpp are produced by
# Templates/ModuleTemplate.cmake on the next configure, so the boilerplate exists in
# exactly one place.
get_filename_component(ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

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
if(ARG_COUNT LESS 2)
    message(FATAL_ERROR "Usage: cmake -P Scripts/CreateModule.cmake -- <ProjectName> <ModuleName> [PreDefault|Default|PostEngineInit]")
endif()

list(GET POS_ARGS 0 PROJ_NAME)
list(GET POS_ARGS 1 MOD_NAME)

set(PHASE "PostEngineInit")
if(ARG_COUNT GREATER_EQUAL 3)
    list(GET POS_ARGS 2 PHASE)
endif()

# The module name becomes the folder name, the target Module_<Project>_<Name> and the
# class name F<Name>Module, so it has to be a valid C++ identifier.
if(NOT PROJ_NAME MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
    message(FATAL_ERROR "Project name '${PROJ_NAME}' is not a valid C++ identifier.")
endif()
if(NOT MOD_NAME MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
    message(FATAL_ERROR
            "Module name '${MOD_NAME}' is not a valid C++ identifier. It becomes the folder name, "
            "the target Module_<Project>_${MOD_NAME} and the class F${MOD_NAME}Module.")
endif()
if(NOT PHASE MATCHES "^(PreDefault|Default|PostEngineInit)$")
    message(FATAL_ERROR "Invalid loading phase '${PHASE}'. Use PreDefault, Default or PostEngineInit.")
endif()

set(PROJ_DIR "${ROOT_DIR}/${PROJ_NAME}")
if(NOT EXISTS "${PROJ_DIR}/Target.cmake")
    message(FATAL_ERROR "Project '${PROJ_NAME}' does not exist at ${PROJ_DIR}")
endif()

set(MOD_DIR "${PROJ_DIR}/Source/${MOD_NAME}")
if(EXISTS "${MOD_DIR}/Build.cmake")
    message(FATAL_ERROR "Module '${PROJ_NAME}::${MOD_NAME}' already exists at ${MOD_DIR}")
endif()

file(MAKE_DIRECTORY "${MOD_DIR}/Public")
file(MAKE_DIRECTORY "${MOD_DIR}/Private")

file(WRITE "${MOD_DIR}/Build.cmake"
        "# Module Build Dependencies & Boot Stage\n"
        "# Loading Phases: PreDefault, Default, PostEngineInit\n"
        "set(LOADING_PHASE \"${PHASE}\")\n\n"
        "# Modules exposed in this module's Public/ headers (inherited by consumers)\n"
        "set(PUBLIC_DEPENDENCY_MODULES\n"
        ")\n\n"
        "# Modules used only inside this module's Private/ .cpp files (hidden from consumers)\n"
        "set(PRIVATE_DEPENDENCY_MODULES\n"
        "        Engine::Core\n"
        ")\n\n"
        "# Move Engine::Core to PUBLIC_DEPENDENCY_MODULES as soon as a Public/ header includes a\n"
        "# Core header (Object/Object.h, Delegates/Delegate.h, ...), or consumers cannot include it.\n\n"
        "set(PUBLIC_THIRD_PARTY_DEPENDENCIES\n"
        ")\n\n"
        "set(THIRD_PARTY_DEPENDENCIES\n"
        ")\n"
)

message(STATUS "[VEngine] Created module '${PROJ_NAME}::${MOD_NAME}' (Phase: ${PHASE}).")
message(STATUS "[VEngine] Reload CMake to generate Public/${MOD_NAME}.h and Private/${MOD_NAME}.cpp.")