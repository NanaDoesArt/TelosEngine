#pragma once

// Module-wide public header and the module's precompiled header.
//
// Every object-facing include path stays exactly what it was when this code lived in
// Core: Object/Object.h, Object/Interface.h, Object/ObjectRegistry.h and
// Templates/WeakObjectPtr.h. Consumers change a Build.cmake dependency, not their includes.
#include "RObjectAPI.h"
#include "Object/Object.h"
#include "Object/Interface.h"
#include "Object/ObjectRegistry.h"
#include "Templates/WeakObjectPtr.h"