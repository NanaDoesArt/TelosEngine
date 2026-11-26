# Module Build Dependencies & Boot Stage
# Loading Phases: PreDefault, Default, PostEngineInit
set(LOADING_PHASE "PreDefault")

# Modules exposed in this module's Public/ headers (inherited by consumers)
set(PUBLIC_DEPENDENCY_MODULES
)

# Modules used only inside this module's Private/ .cpp files (hidden from consumers)
set(PRIVATE_DEPENDENCY_MODULES
        Engine::Core
)

# Move Engine::Core to PUBLIC_DEPENDENCY_MODULES as soon as a Public/ header includes a
# Core header (Object/Object.h, Delegates/Delegate.h, ...), or consumers cannot include it.

set(PUBLIC_THIRD_PARTY_DEPENDENCIES
)

set(THIRD_PARTY_DEPENDENCIES
)
