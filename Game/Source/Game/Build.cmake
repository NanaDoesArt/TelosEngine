# Module Build Dependencies & Boot Stage
# Loading Phases: PreDefault, Default, PostEngineInit
set(LOADING_PHASE "PostEngineInit")

# Modules exposed in this module's Public/ headers (inherited by consumers)
set(PUBLIC_DEPENDENCY_MODULES
        Engine::Core
        Engine::Engine
)

# Modules used only inside this module's Private/ .cpp files (hidden from consumers)
set(PRIVATE_DEPENDENCY_MODULES
)

# Third-party libraries exposed in Public/ headers (avoid whenever possible)
set(PUBLIC_THIRD_PARTY_DEPENDENCIES
)

# Third-party libraries hidden inside Private/ .cpp files
set(THIRD_PARTY_DEPENDENCIES
)

# Optional: Extra headers to include in this module's precompiled header
set(PRECOMPILE_HEADERS
)