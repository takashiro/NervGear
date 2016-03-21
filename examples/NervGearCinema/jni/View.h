#pragma once

#include "KeyState.h"
#include "VMath.h"
#include "VBasicmath.h"
#include "Input.h"

#include <VString.h>

using namespace NervGear;


namespace OculusCinema {

class View
{
protected:
						View( const char * name );

public:
	const char *		name;

	enum eViewState {
		VIEWSTATE_CLOSED,
		VIEWSTATE_OPEN,
	};

	virtual 			~View();

    virtual void 		OneTimeInit( const VString &launchIntent ) = 0;
	virtual void		OneTimeShutdown() = 0;

	virtual void 		OnOpen() = 0;
	virtual void 		OnClose() = 0;

	virtual bool 		Command( const char * msg ) = 0;
	virtual bool 		OnKeyEvent( const int keyCode, const KeyState::eKeyEventType eventType ) = 0;

	virtual Matrix4f 	DrawEyeView( const int eye, const float fovDegrees ) = 0;
	virtual Matrix4f 	Frame( const VrFrame & vrFrame ) = 0;

	bool				IsOpen() const { return CurViewState == VIEWSTATE_OPEN; }
	bool				IsClosed() const { return CurViewState == VIEWSTATE_CLOSED; }

protected:
	eViewState			CurViewState;		// current view state
	eViewState			NextViewState;		// state the view should go to on next frame
};

} // namespace OculusCinema
