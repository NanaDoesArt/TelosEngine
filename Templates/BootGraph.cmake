function(vengine_phase_rank PHASE OUT_RANK)
    if(PHASE STREQUAL "PreDefault")
        set(${OUT_RANK} 0 PARENT_SCOPE)
    elseif(PHASE STREQUAL "Default")
        set(${OUT_RANK} 1 PARENT_SCOPE)
    elseif(PHASE STREQUAL "PostEngineInit")
        set(${OUT_RANK} 2 PARENT_SCOPE)
    else()
        message(FATAL_ERROR "[VEngine] Invalid LOADING_PHASE '${PHASE}'. Must be PreDefault, Default, or PostEngineInit.")
    endif()
endfunction()

# Recursive DFS with cycle detection, phase-order validation, and transitive closure collection
function(vengine_visit_module TARGET_MOD)
    if(NOT TARGET "${TARGET_MOD}")
        return()
    endif()

    get_property(VISITING GLOBAL PROPERTY VENGINE_VISITING)
    if("${TARGET_MOD}" IN_LIST VISITING)
        message(FATAL_ERROR "[VEngine] Circular module dependency detected involving '${TARGET_MOD}'!")
    endif()

    get_property(VISITED GLOBAL PROPERTY VENGINE_VISITED)
    if("${TARGET_MOD}" IN_LIST VISITED)
        return()
    endif()

    set_property(GLOBAL APPEND PROPERTY VENGINE_VISITING "${TARGET_MOD}")

    get_property(MOD_PHASE GLOBAL PROPERTY "${TARGET_MOD}_PHASE")
    vengine_phase_rank("${MOD_PHASE}" MOD_RANK)

    get_property(MOD_DEPS GLOBAL PROPERTY "${TARGET_MOD}_DEPS")
    foreach(DEP ${MOD_DEPS})
        if(NOT DEP)
            continue()
        endif()
        string(REPLACE "::" "_" DEP_TARGET "Module_${DEP}")
        if(TARGET "${DEP_TARGET}")
            get_property(DEP_PHASE GLOBAL PROPERTY "${DEP_TARGET}_PHASE")
            vengine_phase_rank("${DEP_PHASE}" DEP_RANK)
            if(DEP_RANK GREATER MOD_RANK)
                message(FATAL_ERROR "[VEngine] Boot phase inversion: '${TARGET_MOD}' (${MOD_PHASE}) depends on '${DEP_TARGET}' (${DEP_PHASE}), which loads later!")
            endif()
            vengine_visit_module("${DEP_TARGET}")
        endif()
    endforeach()

    list(REMOVE_ITEM VISITING "${TARGET_MOD}")
    set_property(GLOBAL PROPERTY VENGINE_VISITING "${VISITING}")
    set_property(GLOBAL APPEND PROPERTY VENGINE_VISITED "${TARGET_MOD}")
    set_property(GLOBAL APPEND PROPERTY VENGINE_CLOSURE_TARGETS "${TARGET_MOD}")
endfunction()

# Generates the cross-platform C++ boot manifest & main() entry point
function(vengine_generate_launcher_stub PROJ SEED_MODULES OUT_STUB_PATH)
    set_property(GLOBAL PROPERTY VENGINE_VISITED "")
    set_property(GLOBAL PROPERTY VENGINE_VISITING "")
    set_property(GLOBAL PROPERTY VENGINE_CLOSURE_TARGETS "")

    foreach(MOD_TARGET ${SEED_MODULES})
        vengine_visit_module("${MOD_TARGET}")
    endforeach()

    get_property(CLOSURE_TARGETS GLOBAL PROPERTY VENGINE_CLOSURE_TARGETS)
    set(SORTED_BOOT_MODULES "")

    foreach(PHASE PreDefault Default PostEngineInit)
        foreach(MOD_TARGET ${CLOSURE_TARGETS})
            get_property(MOD_PHASE GLOBAL PROPERTY "${MOD_TARGET}_PHASE")
            if(MOD_PHASE STREQUAL "${PHASE}")
                get_property(QUAL_NAME GLOBAL PROPERTY "${MOD_TARGET}_QUALIFIED_NAME")
                list(APPEND SORTED_BOOT_MODULES "\"${QUAL_NAME}\"")
            endif()
        endforeach()
    endforeach()

    string(REPLACE ";" ", " BOOT_ARRAY_STR "${SORTED_BOOT_MODULES}")
    set(STUB_FILE "${CMAKE_SOURCE_DIR}/Intermediate/Executable/${PROJ}.gen.cpp")

    file(CONFIGURE OUTPUT "${STUB_FILE}" @ONLY NEWLINE_STYLE UNIX CONTENT [=[
// Auto-generated dependency-sorted boot manifest & entry stub
#if defined(_WIN32)
  #define VENGINE_EXPORT __declspec(dllexport)
  #define VENGINE_IMPORT __declspec(dllimport)
#else
  #define VENGINE_EXPORT __attribute__((visibility("default")))
  #define VENGINE_IMPORT
#endif

extern "C" VENGINE_EXPORT const char* GetTargetProjectName() { return "@PROJ@"; }
extern "C" VENGINE_EXPORT const char* const* GetBootModules(int* OutCount)
{
    static const char* Modules[] = { @BOOT_ARRAY_STR@ };
    *OutCount = sizeof(Modules) / sizeof(Modules[0]);
    return Modules;
}
extern "C" VENGINE_IMPORT int EngineMain(int argc, char* argv[]);

int main(int argc, char* argv[])
{
    return EngineMain(argc, argv);
}
]=])
    set(${OUT_STUB_PATH} "${STUB_FILE}" PARENT_SCOPE)
endfunction()