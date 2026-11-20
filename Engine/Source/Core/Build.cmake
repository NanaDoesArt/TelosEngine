# Module Build Dependencies & Boot Stage
# Loading Phases: PreDefault, Default, PostEngineInit
set(LOADING_PHASE "PreDefault")

set(PUBLIC_DEPENDENCY_MODULES
    # Modules exposed publicly via headers
)

set(PRIVATE_DEPENDENCY_MODULES
    # Hide internal engine wrapper dependencies securely
    Engine::Core
)
set(PUBLIC_THIRD_PARTY_DEPENDENCIES
        EASTL
)


set(THIRD_PARTY_DEPENDENCIES
    # External libraries fetched in Project/Dependencies.cmake
        mimalloc-static
        spdlog
        BS_thread_pool
)
