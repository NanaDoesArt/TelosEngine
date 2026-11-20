function(vengine_create_staging_target PROJ MODULE_TARGETS OUT_DIR)
    set(VALID_TARGETS ${MODULE_TARGETS})
    list(REMOVE_DUPLICATES VALID_TARGETS)

    set(COPY_COMMANDS "")
    foreach(MOD_TARGET IN LISTS VALID_TARGETS)
        if(NOT TARGET "${MOD_TARGET}")
            message(FATAL_ERROR "[VEngine Packaging] Internal inconsistency: module target '${MOD_TARGET}' requested for '${PROJ}' does not exist!")
        endif()

        list(APPEND COPY_COMMANDS
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${MOD_TARGET}>" "${OUT_DIR}"
        )
    endforeach()

    if(VALID_TARGETS)
        add_custom_target(Stage_${PROJ} ALL
                COMMAND ${CMAKE_COMMAND} -E make_directory "${OUT_DIR}"
                ${COPY_COMMANDS}
                DEPENDS ${VALID_TARGETS}
                COMMENT "Staging dependency-complete runtime closure for ${PROJ}..."
                VERBATIM
        )
        set_target_properties(Stage_${PROJ} PROPERTIES FOLDER "Packaging")
        add_dependencies(${PROJ} Stage_${PROJ})
    endif()
endfunction()

# Assembles a single EXECUTABLE or LIBRARY project target (both produce dependency-complete packages)
function(vengine_package_project PROJ)
    get_property(TARGET_TYPE GLOBAL PROPERTY "VENGINE_PROJECT_${PROJ}_TARGET_TYPE")
    get_property(ENTRY_POINT_MODULE GLOBAL PROPERTY "VENGINE_PROJECT_${PROJ}_ENTRY_POINT")
    get_property(PROJ_MODULES GLOBAL PROPERTY "${PROJ}_COMPILED_MODULES")
    set(PROJ_OUT_DIR "${CMAKE_SOURCE_DIR}/Build/Binaries/$<CONFIG>/${PROJ}")

    if(TARGET_TYPE STREQUAL "EXECUTABLE")
        message(STATUS "[Engine Packaging] Assembling executable target: ${PROJ}")

        if(NOT ENTRY_POINT_MODULE)
            message(FATAL_ERROR "[${PROJ}] TARGET_TYPE is EXECUTABLE but ENTRY_POINT_MODULE is not set in ${PROJ}/Target.cmake")
        endif()

        string(REPLACE "::" "_" ENTRY_TARGET "Module_${ENTRY_POINT_MODULE}")
        if(NOT TARGET "${ENTRY_TARGET}")
            message(FATAL_ERROR "[${PROJ}] ENTRY_POINT_MODULE '${ENTRY_POINT_MODULE}' (${ENTRY_TARGET}) does not exist!")
        endif()

        set(SEED_MODULES ${PROJ_MODULES} "${ENTRY_TARGET}")
        list(REMOVE_DUPLICATES SEED_MODULES)

        vengine_generate_launcher_stub("${PROJ}" "${SEED_MODULES}" LAUNCHER_STUB)
        get_property(CLOSURE_TARGETS GLOBAL PROPERTY VENGINE_CLOSURE_TARGETS)

        add_executable(${PROJ} "${LAUNCHER_STUB}")
        target_link_libraries(${PROJ} PRIVATE ${ENTRY_POINT_MODULE})

        if(CLOSURE_TARGETS)
            add_dependencies(${PROJ} ${CLOSURE_TARGETS})
            foreach(MOD_TARGET ${CLOSURE_TARGETS})
                target_include_directories(${PROJ} PRIVATE $<TARGET_PROPERTY:${MOD_TARGET},INTERFACE_INCLUDE_DIRECTORIES>)
            endforeach()

            vengine_create_staging_target("${PROJ}" "${CLOSURE_TARGETS}" "${PROJ_OUT_DIR}")
        endif()

        set_target_properties(${PROJ} PROPERTIES
                FOLDER "Projects"
                RUNTIME_OUTPUT_DIRECTORY "${PROJ_OUT_DIR}"
                RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/Build/Binaries/Debug/${PROJ}"
                RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/Build/Binaries/Release/${PROJ}"
                RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/Build/Binaries/RelWithDebInfo/${PROJ}"
                RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_SOURCE_DIR}/Build/Binaries/MinSizeRel/${PROJ}"
                VS_DEBUGGER_WORKING_DIRECTORY "${PROJ_OUT_DIR}"
        )

    elseif(TARGET_TYPE STREQUAL "LIBRARY")
        message(STATUS "[Engine Packaging] Assembling self-contained library package target: ${PROJ}")
        add_custom_target(${PROJ})

        set_property(GLOBAL PROPERTY VENGINE_VISITED "")
        set_property(GLOBAL PROPERTY VENGINE_VISITING "")
        set_property(GLOBAL PROPERTY VENGINE_CLOSURE_TARGETS "")
        foreach(MOD_TARGET ${PROJ_MODULES})
            vengine_visit_module("${MOD_TARGET}")
        endforeach()
        get_property(CLOSURE_TARGETS GLOBAL PROPERTY VENGINE_CLOSURE_TARGETS)

        if(CLOSURE_TARGETS)
            add_dependencies(${PROJ} ${CLOSURE_TARGETS})
            vengine_create_staging_target("${PROJ}" "${CLOSURE_TARGETS}" "${PROJ_OUT_DIR}")
        endif()
    else()
        message(FATAL_ERROR "[${PROJ}] Unknown TARGET_TYPE '${TARGET_TYPE}' in ${PROJ}/Target.cmake (use EXECUTABLE or LIBRARY)")
    endif()
endfunction()