#include "Platform/PlatformBackend.h"
#include "Platform/GLFW/GLFWApplication.h"

// ==============================================================================
// Backend selection.
//
// This is the only translation unit in the engine that names a concrete platform
// implementation. To move to a native backend, compile its files instead and return it here;
// FApplication, FWindow, the loop and every caller stay unchanged.
// ==============================================================================
FPlatformApplication* CreatePlatformApplication()
{
    return CreateGLFWApplication();
}