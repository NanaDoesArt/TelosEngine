#pragma once

#include "RObjectAPI.h"

// Registers the engine's object runtime with the module manager.
//
// OwnerModule is the module that implements the runtime (Engine_Object). The manager keeps
// the registration until that module's teardown notification has run, then drops it before
// freeing the library. The runtime itself is a function-local static in this module, so it
// is destroyed by the module unloader, not by the manager.
//
// Returns false when the owner's own lifetime scope cannot be created, which fails the load.
RObject_API bool RegisterObjectRuntime(const char* OwnerModule);