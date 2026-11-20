get_filename_component(DIR_NAME "${CMAKE_CURRENT_SOURCE_DIR}" NAME)
project(${DIR_NAME} LANGUAGES CXX)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Dependencies.cmake")
    set(CMAKE_FOLDER "ThirdParty")
    include("${CMAKE_CURRENT_SOURCE_DIR}/Dependencies.cmake")
    unset(CMAKE_FOLDER)
endif()

set(SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Source")
file(GLOB CHILDFOLDERS RELATIVE "${SOURCE_DIR}" CONFIGURE_DEPENDS "${SOURCE_DIR}/*")

set(CURRENT_PROJECT_MODULES "")
foreach(FOLDER ${CHILDFOLDERS})
    set(MOD_DIR "${SOURCE_DIR}/${FOLDER}")
    if(NOT IS_DIRECTORY "${MOD_DIR}" OR NOT EXISTS "${MOD_DIR}/Build.cmake")
        continue()
    endif()

    list(APPEND CURRENT_PROJECT_MODULES ${FOLDER})
    configure_file("${CMAKE_SOURCE_DIR}/Templates/ModuleTemplate.cmake" "${MOD_DIR}/CMakeLists.txt" COPYONLY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${MOD_DIR}/CMakeLists.txt")
endforeach()

foreach(MODULE ${CURRENT_PROJECT_MODULES})
    set(PARENT_PROJECT_NAME ${PROJECT_NAME})
    add_subdirectory("Source/${MODULE}")
endforeach()

set(UNIQUE_PROJECT_NAME "Project_${PROJECT_NAME}")
add_custom_target(${UNIQUE_PROJECT_NAME})

set(PREFIXED_MODULES "")
foreach(MODULE ${CURRENT_PROJECT_MODULES})
    list(APPEND PREFIXED_MODULES "Module_${PARENT_PROJECT_NAME}_${MODULE}")
endforeach()

set_property(GLOBAL PROPERTY "${PROJECT_NAME}_COMPILED_MODULES" "${PREFIXED_MODULES}")
set_property(GLOBAL APPEND PROPERTY "ALL_COMPILED_MODULES" "${PREFIXED_MODULES}")

if(PREFIXED_MODULES)
    add_dependencies(${UNIQUE_PROJECT_NAME} ${PREFIXED_MODULES})
endif()

set_target_properties(${UNIQUE_PROJECT_NAME} PROPERTIES FOLDER "Projects")