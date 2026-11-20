// The Core module is a normal module in every respect except one: it owns the allocator and the
// object registry. It must NOT register any class by hand. RObject and FObjectScope are
// bootstrap classes registered by FObjectRegistry itself (see EnsureBootstrapClasses), so this
// file stays empty of boilerplate and can never drift out of sync with the registry.
#include "Core.h"
#include "Object/Object.h"

class FCoreModule : public IModuleInterface
{
public:
    bool PrepareModule() override
    {
        VENGINE_LOG(LogCore, Info, "[Engine_Core] Preparing object registry...");

        // Touching the registry is the only setup Core needs: it registers its own bootstrap
        // classes on first use. Nothing here to keep in sync.
        FObjectRegistry& Registry = FObjectRegistry::Get();
        return Registry.IsOnGameThread();
    }

    void ShutdownModule() override
    {
        VENGINE_LOG(LogCore, Info, "[Engine_Core] Module shutting down!");
    }
};

IMPLEMENT_MODULE(FCoreModule)