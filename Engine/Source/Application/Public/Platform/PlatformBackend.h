#pragma once

class FPlatformApplication;

// ==============================================================================
// Internal seam between FApplication and the selected backend.
//
// Defined in Private/Platform/PlatformApplication.cpp, the single translation unit that names
// a concrete backend. FApplication only knows this function, not GLFW.
// ==============================================================================
FPlatformApplication* CreatePlatformApplication();