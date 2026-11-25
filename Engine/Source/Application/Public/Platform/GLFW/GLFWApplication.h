#pragma once

class FPlatformApplication;

// Creates the GLFW backend.
//
// Called only by CreatePlatformApplication() in Private/Platform/PlatformApplication.cpp.
// This header lives in Private/ because no other module may hold a GLFW-backed application:
// any backend in the engine goes through FApplication.
FPlatformApplication* CreateGLFWApplication();
