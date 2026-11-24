#pragma once
#include "Templates/SmartPointer.h"
#include "Core.h"

class Application_API FPlatformWindow
{
public:
	Application_API FPlatformWindow();

	Application_API virtual ~FPlatformWindow();
	
	Application_API virtual void ReshapeWindow( int32 X, int32 Y, int32 Width, int32 Height );
	
	Application_API virtual bool GetFullScreenInfo( int32& X, int32& Y, int32& Width, int32& Height ) const;
	
	Application_API virtual void MoveWindowTo ( int32 X, int32 Y );
	
	Application_API virtual void BringToFront( bool bForce = false );
	
	Application_API virtual void Destroy();
	
	Application_API virtual void Minimize();
	
	Application_API virtual void Maximize();
	
	Application_API virtual void Restore();
	
	Application_API virtual void Show();
	
	Application_API virtual void Hide();
	
	Application_API virtual void SetWindowMode( EWindowMode::Type InNewWindowMode );
	
	Application_API virtual EWindowMode::Type GetWindowMode() const;
	
	Application_API virtual bool IsMaximized() const;
	
	Application_API virtual bool IsMinimized() const;
	
	Application_API virtual bool IsVisible() const;

	Application_API virtual bool GetRestoredDimensions(int32& X, int32& Y, int32& Width, int32& Height);
	
	Application_API virtual void SetWindowFocus();

	Application_API virtual void SetOpacity( const float InOpacity );
	
	Application_API virtual void Enable( bool bEnable );

	Application_API virtual bool IsPointInWindow( int32 X, int32 Y ) const;
	
	Application_API virtual int32 GetWindowBorderSize() const;
	
	Application_API virtual int32 GetWindowTitleBarSize() const;

	Application_API virtual void* GetOSWindowHandle() const;

	Application_API virtual bool IsForegroundWindow() const;


	Application_API virtual bool SupportsDynamicCursorLock() const;
	
	Application_API virtual bool IsFullscreenSupported() const;

	Application_API virtual void SetText(const char* const Text);
	
	Application_API virtual const FGenericWindowDefinition& GetDefinition() const;
	
	Application_API virtual bool IsDefinitionValid() const;
	
	Application_API virtual void AdjustCachedSize( FVector2D& Size ) const;
	
	Application_API virtual float GetDPIScaleFactor() const;
	
	Application_API virtual void SetDPIScaleFactor(const float Factor);
	
	Application_API virtual bool IsManualManageDPIChanges() const;
	
	Application_API virtual void SetManualManageDPIChanges(const bool bAutoHandle);

	Application_API virtual void DrawAttention(const FWindowDrawAttentionParameters& Parameters);
	
	Application_API virtual void SetNativeWindowButtonsVisibility(bool bVisible);
	
	Application_API virtual bool IsRenderingOffScreen() const;

	Application_API virtual void SetIsInteractiveTooltip(bool bInIsInteractiveTooltip);

protected:

	TSharedPtr< FGenericWindowDefinition > Definition;
};
    
};
