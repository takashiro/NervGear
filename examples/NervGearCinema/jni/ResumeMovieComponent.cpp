#include "ResumeMovieComponent.h"
#include "ResumeMovieView.h"
#include "CinemaApp.h"

namespace OculusCinema {

const V4Vectf ResumeMovieComponent::FocusColor( 1.0f, 1.0f, 1.0f, 1.0f );
const V4Vectf ResumeMovieComponent::HighlightColor( 1.0f, 1.0f, 1.0f, 1.0f );
const V4Vectf ResumeMovieComponent::NormalColor( 82.0f / 255.0f, 101.0f / 255.0f, 120.0f / 255.06, 255.0f / 255.0f );

//==============================
//  ResumeMovieComponent::
ResumeMovieComponent::ResumeMovieComponent( ResumeMovieView * view, int itemNum ) :
    VRMenuComponent( VRMenuEventFlags_t( VRMENU_EVENT_TOUCH_DOWN ) | 
            VRMENU_EVENT_TOUCH_UP | 
            VRMENU_EVENT_FOCUS_GAINED | 
            VRMENU_EVENT_FOCUS_LOST |
            VRMENU_EVENT_FRAME_UPDATE ),

    Icon( NULL ),
    Sound(),
	HasFocus( false ),
    ItemNum( itemNum ),
    CallbackView( view )
{
}

//==============================
//  ResumeMovieComponent::UpdateColor
void ResumeMovieComponent::UpdateColor( VRMenuObject * self )
{
    self->setTextColor( HasFocus ? FocusColor : ( self->isHilighted() ? HighlightColor : NormalColor ) );
	if ( Icon != NULL )
	{
        Icon->setColor( HasFocus ? FocusColor : ( self->isHilighted() ? HighlightColor : NormalColor ) );
	}
    self->setColor( self->isHilighted() ? V4Vectf( 1.0f ) : V4Vectf( 0.0f ) );
}

//==============================
//  ResumeMovieComponent::OnEvent_Impl
eMsgStatus ResumeMovieComponent::onEventImpl( App * app, VrFrame const & vrFrame, OvrVRMenuMgr & menuMgr,
        VRMenuObject * self, VRMenuEvent const & event )
{
    switch( event.eventType )
    {
        case VRMENU_EVENT_FRAME_UPDATE:
            return Frame( app, vrFrame, menuMgr, self, event );
        case VRMENU_EVENT_FOCUS_GAINED:
            return FocusGained( app, vrFrame, menuMgr, self, event );
        case VRMENU_EVENT_FOCUS_LOST:
            return FocusLost( app, vrFrame, menuMgr, self, event );
        case VRMENU_EVENT_TOUCH_DOWN:
        	if ( CallbackView != NULL )
        	{
                Sound.playSound( app, "touch_down", 0.1 );
        		return MSG_STATUS_CONSUMED;
        	}
        	return MSG_STATUS_ALIVE;
        case VRMENU_EVENT_TOUCH_UP:
        	if ( !( vrFrame.Input.buttonState & BUTTON_TOUCH_WAS_SWIPE ) && ( CallbackView != NULL ) )
        	{
                Sound.playSound( app, "touch_up", 0.1 );
               	CallbackView->ResumeChoice( ItemNum );
        		return MSG_STATUS_CONSUMED;
        	}
            return MSG_STATUS_ALIVE;
        default:
            OVR_ASSERT( !"Event flags mismatch!" );
            return MSG_STATUS_ALIVE;
    }
}

//==============================
//  ResumeMovieComponent::Frame
eMsgStatus ResumeMovieComponent::Frame( App * app, VrFrame const & vrFrame, OvrVRMenuMgr & menuMgr,
        VRMenuObject * self, VRMenuEvent const & event )
{
	UpdateColor( self );

    return MSG_STATUS_ALIVE;
}

//==============================
//  ResumeMovieComponent::FocusGained
eMsgStatus ResumeMovieComponent::FocusGained( App * app, VrFrame const & vrFrame, OvrVRMenuMgr & menuMgr,
        VRMenuObject * self, VRMenuEvent const & event )
{
	//LOG( "FocusGained" );
	HasFocus = true;
    Sound.playSound( app, "gaze_on", 0.1 );

    self->setHilighted( true );
    self->setTextColor( HighlightColor );
	if ( Icon != NULL )
	{
        Icon->setColor( HighlightColor );
	}

	return MSG_STATUS_ALIVE;
}

//==============================
//  ResumeMovieComponent::FocusLost
eMsgStatus ResumeMovieComponent::FocusLost( App * app, VrFrame const & vrFrame, OvrVRMenuMgr & menuMgr,
        VRMenuObject * self, VRMenuEvent const & event )
{
	//LOG( "FocusLost" );

	HasFocus = false;
    Sound.playSound( app, "gaze_off", 0.1 );

    self->setHilighted( false );
    self->setTextColor( self->isHilighted() ? HighlightColor : NormalColor );
	if ( Icon != NULL )
	{
        Icon->setColor( self->isHilighted() ? HighlightColor : NormalColor );
	}

	return MSG_STATUS_ALIVE;
}

} // namespace OculusCinema
