include("${CMAKE_CURRENT_LIST_DIR}/BootGraph.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/Packaging.cmake")

function(vengine_orchestrate_workspace)
    set(TEMPLATE_DIR "${CMAKE_SOURCE_DIR}/Templates")
    file(GLOB ROOT_ITEMS RELATIVE "${CMAKE_SOURCE_DIR}" CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/*")
    set(DISCOVERED_PROJECTS "")

    # 1. Discover explicitly declared projects (folders containing Target.cmake)
    foreach(ITEM ${ROOT_ITEMS})
        set(ITEM_DIR "${CMAKE_SOURCE_DIR}/${ITEM}")
        if(NOT IS_DIRECTORY "${ITEM_DIR}" OR NOT EXISTS "${ITEM_DIR}/Target.cmake")
            continue()
        endif()

        list(APPEND DISCOVERED_PROJECTS "${ITEM}")

        set(TARGET_TYPE "LIBRARY")
        set(ENTRY_POINT_MODULE "")
        include("${ITEM_DIR}/Target.cmake")
        set_property(GLOBAL PROPERTY "VENGINE_PROJECT_${ITEM}_TARGET_TYPE" "${TARGET_TYPE}")
        set_property(GLOBAL PROPERTY "VENGINE_PROJECT_${ITEM}_ENTRY_POINT" "${ENTRY_POINT_MODULE}")

        configure_file("${TEMPLATE_DIR}/ProjectTemplate.cmake" "${ITEM_DIR}/CMakeLists.txt" COPYONLY)
    endforeach()

    # 2. Configure all project subdirectories
    foreach(PROJ ${DISCOVERED_PROJECTS})
        add_subdirectory(${PROJ})
    endforeach()

    # 3. Assemble Executable & Library packaging targets
    foreach(PROJ ${DISCOVERED_PROJECTS})
        vengine_package_project("${PROJ}")
    endforeach()
endfunction()