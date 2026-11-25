# Module Build Dependencies & Boot Stage
# Loading Phases: PreDefault, Default, PostEngineInit)
set(LOADING_PHASE "PreDefault")

# Core is in the Public/ headers, so it is a public dependency: FWindowDefinition carries an
# FString, and every caller of FApplication::CreateWindow() needs Core's include paths too.
set(PUBLIC_DEPENDENCY_MODULES
        Engine::Core
)

set(PRIVATE_DEPENDENCY_MODULES
)

# GLFW is private: no Public/ header includes <GLFW/glfw3.h>. Only the backend in Private/
# does, so consumers of this module never see a GLFW type or include path.
set(PUBLIC_THIRD_PARTY_DEPENDENCIES
)

# OpenGL::GL is private as well. GLFW creates the OpenGL context but does not link OpenGL
# itself, and the placeholder frame in the backend issues glViewport/glClearColor/glClear.
# When a renderer module takes over drawing, this dependency moves there with the calls.
set(THIRD_PARTY_DEPENDENCIES
        glfw
)