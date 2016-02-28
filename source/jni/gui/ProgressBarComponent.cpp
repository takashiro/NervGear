/************************************************************************************

Filename    :   ProgressBarComponent.h
Content     :   A reusable component implementing a progress bar.
Created     :   Mar 30, 2015
Authors     :   Warsam Osman

Copyright   :   Copyright 2015 Oculus VR, Inc. All Rights reserved.


*************************************************************************************/

#include "ProgressBarComponent.h"
#include "App.h"
#include "VRMenuMgr.h"

namespace NervGear {

char const * OvrProgressBarComponent::TYPE_NAME = "OvrProgressBarComponent";

static const Vector3f FWD( 0.0f, 0.0f, -1.0f );
static const Vector3f RIGHT( 1.0f, 0.0f, 0.0f );
static const Vector3f DOWN( 0.0f, -1.0f, 0.0f );

static const float BASE_THUMB_WIDTH 		= 4.0f;
static const float THUMB_FROM_BASE_OFFSET 	= 0.001f;
static const float THUMB_SHRINK_FACTOR 		= 0.5f;

//==============================
// OvrProgressBarComponent::OvrProgressBarComponent
OvrProgressBarComponent::OvrProgressBarComponent( const VRMenuId_t rootId, const VRMenuId_t baseId,
	const VRMenuId_t thumbId, const VRMenuId_t animId )
		: VRMenuComponent( VRMenuEventFlags_t( VRMENU_EVENT_FRAME_UPDATE ) )
		, m_fader( 0.0f )
		, m_fadeInRate( 1.0f / 0.2f )
		, m_fadeOutRate( 1.0f / 0.5f )
		, m_frac( 0.0f )
		, m_progressBarBaseWidth( 0.0f )
		, m_progressBarBaseHeight( 0.0f )
		, m_progressBarCurrentbaseLength( 0.0f )
		, m_progressBarThumbWidth( 0.0f )
		, m_progressBarThumbHeight( 0.0f )
		, m_progressBarCurrentThumbLength( 0.0f )
		, m_progressBarRootId( rootId )
		, m_progressBarBaseId( baseId )
		, m_progressBarThumbId( thumbId )
		, m_progressBarAnimId( animId )
		, m_currentProgressBarState( PROGRESSBAR_STATE_HIDDEN )
{
}

void OvrProgressBarComponent::setProgressFrac( OvrVRMenuMgr & menuMgr, VRMenuObject * self, const float frac )
{
	//LOG( "OvrProgressBarComponent::SetProgressFrac to %f", Frac  );
	OVR_ASSERT( frac >= 0.0f && frac <= 1.0f );

	if ( m_frac == frac )
	{
		return;
	}

	m_frac = frac;
	m_progressBarCurrentThumbLength = m_progressBarThumbWidth * m_frac;

	float thumbPos = m_progressBarThumbWidth - m_progressBarCurrentThumbLength;

	thumbPos -= thumbPos * 0.5f;
	thumbPos *= VRMenuObject::DEFAULT_TEXEL_SCALE;

	VRMenuObject * thumb = menuMgr.toObject( self->childHandleForId( menuMgr, m_progressBarThumbId ) );
	if ( thumb != NULL )
	{
		thumb->setSurfaceDims( 0, Vector2f( m_progressBarCurrentThumbLength, m_progressBarThumbHeight ) );
		thumb->regenerateSurfaceGeometry( 0, false );
		thumb->setLocalPosition( ( -RIGHT * thumbPos ) - ( FWD * THUMB_FROM_BASE_OFFSET ) );
	}
}

void OvrProgressBarComponent::oneTimeInit( OvrVRMenuMgr & menuMgr, VRMenuObject * self, const Vector4f & color )
{
	// Set alpha on the base - move this to somewhere more explicit if needed
	VRMenuObject * base = menuMgr.toObject( self->childHandleForId( menuMgr, m_progressBarBaseId ) );
	if ( base != NULL )
	{
		base->setSurfaceColor( 0, color );

		// Resize the base
		base->setSurfaceDims( 0, Vector2f( m_progressBarBaseWidth, m_progressBarBaseHeight ) );
		base->regenerateSurfaceGeometry( 0, false );
	}

	// start off hidden
	setProgressbarState( self, PROGRESSBAR_STATE_HIDDEN );
}

void OvrProgressBarComponent::getProgressBarParms( VRMenu & menu, const int width, const int height,
		const VRMenuId_t parentId, const VRMenuId_t rootId, const VRMenuId_t xformId, const VRMenuId_t baseId,
		const VRMenuId_t thumbId, const VRMenuId_t animId,
		const Posef & rootLocalPose, const Posef & xformPose,
		const char * baseImage, const char * barImage, const char * animImage,
		Array< const VRMenuObjectParms* > & outParms )
{
	// Build up the Progressbar parms
	OvrProgressBarComponent * ProgressComponent = new OvrProgressBarComponent( rootId, baseId, thumbId, animId );

	ProgressComponent->setProgressBarBaseWidth( width );
	ProgressComponent->setProgressBarBaseHeight( height );
	ProgressComponent->setProgressBarThumbWidth( width );
	ProgressComponent->setProgressBarThumbHeight( height );

	// parms for the root object that holds all the Progressbar components
	{
		Array< VRMenuComponent* > comps;
		comps.pushBack( ProgressComponent );
		Array< VRMenuSurfaceParms > surfParms;
		char const * text = "ProgressBarRoot";
		Vector3f scale( 1.0f );
		Posef pose( rootLocalPose );
		Posef textPose( Quatf(), Vector3f( 0.0f ) );
		Vector3f textScale( 1.0f );
		VRMenuFontParms fontParms;
		VRMenuObjectFlags_t objectFlags( VRMENUOBJECT_DONT_HIT_ALL );
		objectFlags |= VRMENUOBJECT_DONT_RENDER_TEXT;
		VRMenuObjectInitFlags_t initFlags( VRMENUOBJECT_INIT_FORCE_POSITION  );
		VRMenuObjectParms * itemParms = new VRMenuObjectParms( VRMENU_CONTAINER, comps,
			surfParms, text, pose, scale, textPose, textScale, fontParms, rootId,
			objectFlags, initFlags );
		itemParms->ParentId = parentId;
		outParms.pushBack( itemParms );
	}

	// add parms for the object that serves as a transform 
	{
		Array< VRMenuComponent* > comps;
		Array< VRMenuSurfaceParms > surfParms;
		char const * text = "ProgressBarTransform";
		Vector3f scale( 1.0f );
		Posef pose( xformPose );
		Posef textPose( Quatf(), Vector3f( 0.0f ) );
		Vector3f textScale( 1.0f );
		VRMenuFontParms fontParms;
		VRMenuObjectFlags_t objectFlags( VRMENUOBJECT_DONT_HIT_ALL );
		objectFlags |= VRMENUOBJECT_DONT_RENDER_TEXT;
		VRMenuObjectInitFlags_t initFlags( VRMENUOBJECT_INIT_FORCE_POSITION );
		VRMenuObjectParms * itemParms = new VRMenuObjectParms( VRMENU_CONTAINER, comps,
			surfParms, text, pose, scale, textPose, textScale, fontParms, xformId,
			objectFlags, initFlags );
		itemParms->ParentId = rootId;
		outParms.pushBack( itemParms );
	}

	// add parms for the base image that underlays the whole Progressbar
	{
		const char * text = "ProgressBase";
		Array< VRMenuComponent* > comps;
		Array< VRMenuSurfaceParms > surfParms;
		VRMenuSurfaceParms baseParms( text,
				baseImage, SURFACE_TEXTURE_DIFFUSE,
				NULL, SURFACE_TEXTURE_MAX, NULL, SURFACE_TEXTURE_MAX );
		surfParms.pushBack( baseParms );
		Vector3f scale( 1.0f );
		Posef pose( Quatf(), Vector3f( 0.0f ) );
		Posef textPose( Quatf(), Vector3f( 0.0f ) );
		Vector3f textScale( 1.0f );
		VRMenuFontParms fontParms;
		VRMenuObjectFlags_t objectFlags( VRMENUOBJECT_DONT_HIT_ALL );
		objectFlags |= VRMENUOBJECT_DONT_RENDER_TEXT;
		VRMenuObjectInitFlags_t initFlags( VRMENUOBJECT_INIT_FORCE_POSITION  );
		VRMenuObjectParms * itemParms = new VRMenuObjectParms( VRMENU_BUTTON, comps,
			surfParms, text, pose, scale, textPose, textScale, fontParms, baseId,
			objectFlags, initFlags );
		itemParms->ParentId = xformId;
		outParms.pushBack( itemParms );
	}

	// add parms for the thumb image of the Progressbar
	{
		const char * text = "ProgressThumb";
		Array< VRMenuComponent* > comps;
		Array< VRMenuSurfaceParms > surfParms;
		VRMenuSurfaceParms thumbParms( text,
				barImage, SURFACE_TEXTURE_DIFFUSE,
				NULL, SURFACE_TEXTURE_MAX, NULL, SURFACE_TEXTURE_MAX  );
		//thumbParms.Border = thumbBorder;
		surfParms.pushBack( thumbParms );
		Vector3f scale( 1.0f );
		Posef pose( Quatf(), -FWD * THUMB_FROM_BASE_OFFSET );
		// Since we use left aligned anchors on the base and thumb, we offset the root once to center the Progressbar
		Posef textPose( Quatf(), Vector3f( 0.0f ) );
		Vector3f textScale( 1.0f );
		VRMenuFontParms fontParms;
		VRMenuObjectFlags_t objectFlags( VRMENUOBJECT_DONT_HIT_ALL );
		objectFlags |= VRMENUOBJECT_DONT_RENDER_TEXT;
		objectFlags |= VRMENUOBJECT_FLAG_POLYGON_OFFSET;
		VRMenuObjectInitFlags_t initFlags( VRMENUOBJECT_INIT_FORCE_POSITION  );
		VRMenuObjectParms * itemParms = new VRMenuObjectParms( VRMENU_BUTTON, comps,
			surfParms, text, pose, scale, textPose, textScale, fontParms, thumbId,
			objectFlags, initFlags );
		itemParms->ParentId = xformId;
		outParms.pushBack( itemParms );
	}

	// add parms for the progress animation
	{
		// TODO
	}
}

//==============================
// OvrProgressBarComponent::OnEvent_Impl
eMsgStatus OvrProgressBarComponent::onEventImpl( App * app, VrFrame const & vrFrame, OvrVRMenuMgr & menuMgr,
		VRMenuObject * self, VRMenuEvent const & event )
{
	switch ( event.eventType )
	{
		case VRMENU_EVENT_FRAME_UPDATE:
			return onFrameUpdate( app, vrFrame, menuMgr, self, event );
		default:
			OVR_ASSERT( false );
			return MSG_STATUS_ALIVE;
	}
    return MSG_STATUS_CONSUMED;
}

const char * progressBarStateNames [ ] =
{
	"PROGRESS_STATE_NONE",
	"PROGRESS_STATE_FADE_IN",
	"PROGRESS_STATE_VISIBLE",
	"PROGRESS_STATE_FADE_OUT",
	"PROGRESS_STATE_HIDDEN",
	"NUM_PROGRESS_STATES"
};

const char* StateString( const OvrProgressBarComponent::eProgressBarState state )
{
	OVR_ASSERT( state >= 0 && state < OvrProgressBarComponent::PROGRESSBAR_STATES_COUNT );
	return progressBarStateNames[ state ];
}

//==============================
// OvrProgressBarComponent::SetProgressState
void OvrProgressBarComponent::setProgressbarState( VRMenuObject * self, const eProgressBarState state )
{
	eProgressBarState lastState = m_currentProgressBarState;
	m_currentProgressBarState = state;
	if ( m_currentProgressBarState == lastState )
	{
		return;
	}
	
	switch ( m_currentProgressBarState )
	{
	case PROGRESSBAR_STATE_NONE:
		break;
	case PROGRESSBAR_STATE_FADE_IN:
		if ( lastState == PROGRESSBAR_STATE_HIDDEN || lastState == PROGRESSBAR_STATE_FADE_OUT )
		{
			LOG( "%s to %s", StateString( lastState ), StateString( m_currentProgressBarState ) );
			m_fader.startFadeIn();
		}
		break;
	case PROGRESSBAR_STATE_VISIBLE:
		self->setVisible( true );
		break;
	case PROGRESSBAR_STATE_FADE_OUT:
		if ( lastState == PROGRESSBAR_STATE_VISIBLE || lastState == PROGRESSBAR_STATE_FADE_IN )
		{
			LOG( "%s to %s", StateString( lastState ), StateString( m_currentProgressBarState ) );
			m_fader.startFadeOut();
		}
		break;
	case PROGRESSBAR_STATE_HIDDEN:
		self->setVisible( false );
		break;
	default:
		OVR_ASSERT( false );
		break;
	}
}

//==============================
// OvrProgressBarComponent::OnFrameUpdate
eMsgStatus OvrProgressBarComponent::onFrameUpdate( App * app, VrFrame const & vrFrame, OvrVRMenuMgr & menuMgr,
		VRMenuObject * self, VRMenuEvent const & event )
{
	OVR_ASSERT( self != NULL );
    if ( m_fader.fadeState() != Fader::FADE_NONE )
	{
        const float fadeRate = ( m_fader.fadeState() == Fader::FADE_IN ) ? m_fadeInRate : m_fadeOutRate;
		m_fader.update( fadeRate, vrFrame.DeltaSeconds );
		const float CurrentFadeLevel = m_fader.finalAlpha();
		self->setColor( Vector4f( 1.0f, 1.0f, 1.0f, CurrentFadeLevel ) );
	}
	else if ( m_fader.fadeAlpha() == 1.0f )
	{
		setProgressbarState( self, PROGRESSBAR_STATE_VISIBLE );
	}
	else if ( m_fader.fadeAlpha() == 0.0f )
	{
		setProgressbarState( self, PROGRESSBAR_STATE_HIDDEN );
	}

	return MSG_STATUS_ALIVE;
}

} // namespace NervGear
