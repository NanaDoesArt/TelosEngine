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

# RTM is PUBLIC, not private: Core.h is the module PCH and it includes Math/Vector2D.h,
# which includes <rtm/vector4f.h>. With a PRIVATE link, no consuming module would receive
# the RTM include path, and every module's PCH compile would fail with C1083.
set(PUBLIC_THIRD_PARTY_DEPENDENCIES
        EASTL
        RTM::RTM
)

set(THIRD_PARTY_DEPENDENCIES
        # External libraries fetched in Project/Dependencies.cmake
        mimalloc-static
        spdlog
        BS_thread_pool
)