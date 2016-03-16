/************************************************************************************

Filename    :   FolderBrowser.cpp
Content     :   A menu for browsing a hierarchy of folders with items represented by thumbnails.
Created     :   July 25, 2014
Authors     :   Jonathan E. Wright, Warsam Osman, Madhu Kalva

Copyright   :   Copyright 2014 Oculus VR, LLC. All Rights reserved.


*************************************************************************************/

#include "FolderBrowser.h"

#include "VThread.h"
#include <stdio.h>
#include <unistd.h>
#include "../App.h"
#include "VRMenuComponent.h"
#include "VRMenuMgr.h"
#include "GuiSys.h"
#include "DefaultComponent.h"
#include "../VrCommon.h"
#include "VMath.h"
#include "String_Utils.h"
#include "3rdParty/stb/stb_image.h"
#include "AnimComponents.h"
#include "linux/stat.h"
#include "VrCommon.h"
#include "VrLocale.h"
#include "VRMenuObject.h"
#include "ScrollBarComponent.h"
#include "SwipeHintComponent.h"
#include "VApkFile.h"
#include "VArray.h"

namespace NervGear {

char const * OvrFolderBrowser::MENU_NAME = "FolderBrowser";
const float OvrFolderBrowser::CONTROLER_COOL_DOWN = 0.2f;
const float OvrFolderBrowser::SCROLL_DIRECTION_DECIDING_DISTANCE = 10.0f;

const Vector3f FWD( 0.0f, 0.0f, -1.0f );
const Vector3f LEFT( -1.0f, 0.0f, 0.0f );
const Vector3f RIGHT( 1.0f, 0.0f, 0.0f );
const Vector3f UP( 0.0f, 1.0f, 0.0f );
const Vector3f DOWN( 0.0f, -1.0f, 0.0f );
const float SCROLL_REPEAT_TIME 					= 0.5f;
const float EYE_HEIGHT_OFFSET 					= 0.0f;
const float MAX_TOUCH_DISTANCE_FOR_TOUCH_SQ 	= 1800.0f;
const float SCROLL_HITNS_VISIBILITY_TOGGLE_TIME = 5.0f;
const float SCROLL_BAR_LENGTH					= 390;
const int 	HIDE_SCROLLBAR_UNTIL_ITEM_COUNT		= 1; // <= 0 makes scroll bar always visible

// Helper class that guarantees unique ids for VRMenuIds
class OvrUniqueId
{
public:
	explicit OvrUniqueId( int startId )
		: currentId( startId )
	{}

	int Get( const int increment )
	{
		const int toReturn = currentId;
		currentId += increment;
		return toReturn;
	}

private:
	int currentId;
};
OvrUniqueId uniqueId( 1000 );

VRMenuId_t OvrFolderBrowser::ID_CENTER_ROOT( uniqueId.Get( 1 ) );

// helps avoiding copy pasting code for updating wrap around indicator effect
void UpdateWrapAroundIndicator(const OvrScrollManager & ScrollMgr, const OvrVRMenuMgr & menuMgr, VRMenuObject * wrapIndicatorObject, const Vector3f initialPosition, const float initialAngle)
{
	static const float WRAP_INDICATOR_INITIAL_SCALE = 0.8f;

	float fadeValue;
	float rotation = 0.0f;
	float rotationDirection;
	Vector3f position = initialPosition;
	if( ScrollMgr.position() < 0.0f )
	{
		fadeValue = -ScrollMgr.position();
		rotationDirection = -1.0f;
	}
	else
	{
		fadeValue = ScrollMgr.position() - ScrollMgr.maxPosition();
		rotationDirection = 1.0f;
	}

	fadeValue = Alg::Clamp( fadeValue, 0.0f, ScrollMgr.wrapAroundScrollOffset() ) / ScrollMgr.wrapAroundScrollOffset();
	rotation += fadeValue * rotationDirection * Mathf::Pi;
	float scaleValue = 0.0f;
	if( ScrollMgr.isWrapAroundTimeInitiated() )
	{
		if( ScrollMgr.remainingTimeForWrapAround() >= 0.0f )
		{
			float timeSpentPercent = LinearRangeMapFloat( ScrollMgr.remainingTimeForWrapAround(), 0.0f, ScrollMgr.wrapAroundHoldTime(), 100, 0.0f );
			// Time Spent % :  0% ---  70% --- 90% --- 100%
			//      Scale % :  0% --- 300% --- 80% --- 100%  <= Gives popping effect
			static const float POP_EFFECT[4][2] =
			{
					{ 0		, 0.0f },
					{ 70	, 3.0f },
					{ 80	, 0.8f },
					{ 100	, 1.0f },
			};

			if( timeSpentPercent <= 70 )
			{
				scaleValue = LinearRangeMapFloat( timeSpentPercent, POP_EFFECT[0][0], POP_EFFECT[1][0], POP_EFFECT[0][1], POP_EFFECT[1][1] );
			}
			else if( timeSpentPercent <= 90 )
			{
				scaleValue = LinearRangeMapFloat( timeSpentPercent, POP_EFFECT[1][0], POP_EFFECT[2][0], POP_EFFECT[1][1], POP_EFFECT[2][1] );
			}
			else
			{
				scaleValue = LinearRangeMapFloat( timeSpentPercent, POP_EFFECT[2][0], POP_EFFECT[3][0], POP_EFFECT[2][1], POP_EFFECT[3][1] );
			}
		}
		else
		{
			scaleValue = 1.0f;
		}
	}

	float finalScale = WRAP_INDICATOR_INITIAL_SCALE + ( 1.0f - WRAP_INDICATOR_INITIAL_SCALE ) * scaleValue;
	rotation += initialAngle;
	Vector4f col = wrapIndicatorObject->textColor();
	col.w = fadeValue;
	Quatf rot( -FWD, rotation );
	rot.Normalize();

	wrapIndicatorObject->setLocalRotation( rot );
	wrapIndicatorObject->setLocalPosition( position );
	wrapIndicatorObject->setColor( col );
	wrapIndicatorObject->setLocalScale( Vector3f( finalScale, finalScale, finalScale ) );
}

//==============================
// OvrFolderBrowserRootComponent
// This component is attached to the root parent of the folder browsers and gets to consume input first
class OvrFolderBrowserRootComponent : public VRMenuComponent
{
public:
	static const int TYPE_ID = 57514;

	OvrFolderBrowserRootComponent( OvrFolderBrowser & folderBrowser )
		: VRMenuComponent( VRMenuEventFlags_t( VRMENU_EVENT_FRAME_UPDATE ) | VRMENU_EVENT_TOUCH_RELATIVE | VRMENU_EVENT_TOUCH_DOWN |
			VRMENU_EVENT_TOUCH_UP | VRMENU_EVENT_OPENING )
		, FolderBrowser( folderBrowser )
		, ScrollMgr( VERTICAL_SCROLL )
		, TotalTouchDistance( 0.0f )
		, ValidFoldersCount( 0 )
	{
		LastInteractionTimeStamp = ovr_GetTimeInSeconds() - SCROLL_HITNS_VISIBILITY_TOGGLE_TIME;
		ScrollMgr.setScrollPadding(0.5f);
		ScrollMgr.setWrapAroundEnable( false );
	}

	virtual int		typeId() const
	{
		return TYPE_ID;
	}

	int GetCurrentIndex( VRMenuObject * self, OvrVRMenuMgr & menuMgr ) const
	{
		VRMenuObject * foldersRootObject = menuMgr.toObject( FoldersRootHandle );
		OVR_ASSERT( foldersRootObject != NULL );

		// First calculating index of a folder with in valid folders(folder that has atleast one panel) array based on its position
		const int validNumFolders = GetFolderBrowserValidFolderCount();
		const float deltaHeight = FolderBrowser.panelHeight();
		const float maxHeight = deltaHeight * validNumFolders;
		const float positionRatio = foldersRootObject->localPosition().y / maxHeight;
		int idx = nearbyintf( positionRatio * validNumFolders );
		idx = Alg::Clamp( idx, 0, FolderBrowser.numFolders() - 1 );

		// Remapping index with in valid folders to index in entire Folder array
		const int numValidFolders = GetFolderBrowserValidFolderCount();
		int counter = 0;
		int remapedIdx = 0;
		for( ; remapedIdx < numValidFolders; ++remapedIdx )
		{
			OvrFolderBrowser::FolderView * folder = FolderBrowser.getFolderView( remapedIdx );
			if( folder && !folder->panels.isEmpty() )
			{
				if( counter == idx )
				{
					break;
				}
				++counter;
			}
		}

		return Alg::Clamp( remapedIdx, 0, FolderBrowser.numFolders() - 1 );
	}

	void SetActiveFolder( int folderIdx )
	{
		ScrollMgr.setPosition( folderIdx - 1 );
	}

	void SetFoldersRootHandle( menuHandle_t handle ) { FoldersRootHandle = handle; }
	void SetScrollDownHintHandle( menuHandle_t handle ) { ScrollDownHintHandle = handle; }
	void SetScrollUpHintHandle( menuHandle_t handle )  { ScrollUpHintHandle = handle; }
	void SetFoldersWrapHandle( menuHandle_t handle ) { FoldersWrapHandle = handle; }
	void SetFoldersWrapHandleTopPosition( Vector3f position ) { FoldersWrapHandleTopPosition = position; }
	void SetFoldersWrapHandleBottomPosition( Vector3f position ) { FoldersWrapHandleBottomPosition = position; }

private:
	static const float ACTIVE_DEPTH_OFFSET;

	virtual eMsgStatus      onEventImpl( App * app, VrFrame const & vrFrame, OvrVRMenuMgr & menuMgr,
										VRMenuObject * self, VRMenuEvent const & event )
	{
		switch( event.eventType )
        {
            case VRMENU_EVENT_FRAME_UPDATE:
                return Frame( app, vrFrame, menuMgr, self, event );
			case VRMENU_EVENT_TOUCH_DOWN:
				return TouchDown( app, vrFrame, menuMgr, self, event);
			case VRMENU_EVENT_TOUCH_UP:
				return TouchUp( app, vrFrame, menuMgr, self, event );
            case VRMENU_EVENT_TOUCH_RELATIVE:
                return TouchRelative( app, vrFrame, menuMgr, self, event );
			case VRMENU_EVENT_OPENING:
				return OnOpening( app, vrFrame, menuMgr, self, event );
            default:
                OVR_ASSERT( !"Event flags mismatch!" ); // the constructor is specifying a flag that's not handled
                return MSG_STATUS_ALIVE;
        }
	}

	eMsgStatus Frame( App * app, VrFrame const & vrFrame, OvrVRMenuMgr & menuMgr, VRMenuObject * self, VRMenuEvent const & event )
	{
		const int folderCount = FolderBrowser.numFolders();
		ValidFoldersCount = 0;
		// Hides invalid folders(folder that doesn't have at least one panel), and rearranges valid folders positions to avoid gaps between folders
		for ( int i = 0; i < folderCount; ++i )
		{
			const OvrFolderBrowser::FolderView * currentFolder = FolderBrowser.getFolderView( i );
			if ( currentFolder != NULL )
			{
				VRMenuObject * folderObject = menuMgr.toObject( currentFolder->handle );
				if ( folderObject != NULL )
				{
					if ( currentFolder->panels.sizeInt() > 0 )
					{
						folderObject->setVisible( true );
						folderObject->setLocalPosition( ( DOWN * FolderBrowser.panelHeight() * ValidFoldersCount ) );
						++ValidFoldersCount;
					}
					else
					{
						folderObject->setVisible( false );
					}
				}
			}
		}

		bool restrictedScrolling = ValidFoldersCount > 1;
		eScrollDirectionLockType touchDirLock = FolderBrowser.touchDirectionLock();
		eScrollDirectionLockType controllerDirLock = FolderBrowser.controllerDirectionLock();

		ScrollMgr.setMaxPosition( ValidFoldersCount - 1 );
		ScrollMgr.setRestrictedScrollingData( restrictedScrolling, touchDirLock, controllerDirLock );

		unsigned int controllerInput = 0;
		if ( ValidFoldersCount > 1 ) // Need at least one folder in order to enable vertical scrolling
		{
			controllerInput = vrFrame.Input.buttonState;
			if ( controllerInput & ( BUTTON_LSTICK_UP | BUTTON_DPAD_UP | BUTTON_LSTICK_DOWN | BUTTON_DPAD_DOWN | BUTTON_LSTICK_LEFT | BUTTON_DPAD_LEFT | BUTTON_LSTICK_RIGHT | BUTTON_DPAD_RIGHT ) )
			{
				LastInteractionTimeStamp = ovr_GetTimeInSeconds();
			}

			if ( restrictedScrolling )
			{
				if ( touchDirLock == HORIZONTAL_LOCK )
				{
					if ( ScrollMgr.isTouchIsDown() )
					{
						// Restricted scrolling enabled, but locked horizontal scrolling so ignore vertical scrolling
						ScrollMgr.touchUp();
					}
				}

				if ( controllerDirLock != VERTICAL_LOCK )
				{
					// Restricted scrolling enabled, but not locked to vertical scrolling so lose the controller input
					controllerInput = 0;
				}
			}
		}
		ScrollMgr.frame( vrFrame.DeltaSeconds, controllerInput );

		VRMenuObject * foldersRootObject = menuMgr.toObject( FoldersRootHandle );
		const Vector3f & rootPosition = foldersRootObject->localPosition();
		foldersRootObject->setLocalPosition( Vector3f( rootPosition.x, FolderBrowser.panelHeight() * ScrollMgr.position(), rootPosition.z ) );

		const float alphaSpace = FolderBrowser.panelHeight() * 2.0f;
		const float rootOffset = rootPosition.y - EYE_HEIGHT_OFFSET;

		// Fade + hide each category/folder based on distance from origin
		for ( int index = 0; index < FolderBrowser.numFolders(); ++index )
		{
			//const OvrFolderBrowser::Folder & folder = FolderBrowser.GetFolder( index );
			VRMenuObject * child = menuMgr.toObject( foldersRootObject->getChildHandleForIndex( index ) );
			OVR_ASSERT( child != NULL );

			const Vector3f & position = child->localPosition();
			Vector4f color = child->color();
			color.w = 0.0f;

			VRMenuObjectFlags_t flags = child->flags();
			const float absolutePosition = fabs( rootOffset - fabs( position.y ) );
			if ( absolutePosition < alphaSpace )
			{
				// Fade in / out
				float ratio = absolutePosition / alphaSpace;
				float ratioSq = ratio * ratio;
				float finalAlpha = 1.0f - ratioSq;
				color.w = Alg::Clamp( finalAlpha, 0.0f, 1.0f );
				flags &= ~( VRMenuObjectFlags_t( VRMENUOBJECT_DONT_RENDER ) | VRMENUOBJECT_DONT_HIT_ALL );

				// Lerp the folder towards or away from viewer
				Vector3f activePosition = position;
				const float targetZ = ACTIVE_DEPTH_OFFSET * finalAlpha;
				activePosition.z = targetZ;
				child->setLocalPosition( activePosition );
			}
			else
			{
				flags |= VRMenuObjectFlags_t( VRMENUOBJECT_DONT_RENDER ) | VRMENUOBJECT_DONT_HIT_ALL;
			}

			child->setColor( color );
			child->setFlags( flags );
		}

		if ( 0 )
		{
			// Updating folder wrap around indicator logic/effect
			VRMenuObject * wrapIndicatorObject = menuMgr.toObject( FoldersWrapHandle );
			if ( wrapIndicatorObject != NULL && ScrollMgr.isWrapAroundEnabled() )
			{
				if ( ScrollMgr.isScrolling() && ScrollMgr.isOutOfBounds() )
				{
					wrapIndicatorObject->setVisible( true );
					bool scrollingAtStart = ( ScrollMgr.position() < 0.0f );
					if ( scrollingAtStart )
					{
						UpdateWrapAroundIndicator( ScrollMgr, menuMgr, wrapIndicatorObject, FoldersWrapHandleTopPosition, -Mathf::PiOver2 );
					}
					else
					{
						FoldersWrapHandleBottomPosition.y = -FolderBrowser.panelHeight() * ValidFoldersCount;
						UpdateWrapAroundIndicator( ScrollMgr, menuMgr, wrapIndicatorObject, FoldersWrapHandleBottomPosition, Mathf::PiOver2 );
					}

					Vector3f position = wrapIndicatorObject->localPosition();
					float ratio;
					if ( scrollingAtStart )
					{
						ratio = LinearRangeMapFloat( ScrollMgr.position(), -1.0f, 0.0f, 0.0f, 1.0f );
					}
					else
					{
						ratio = LinearRangeMapFloat( ScrollMgr.position(), ValidFoldersCount - 1.0f, ValidFoldersCount, 1.0f, 0.0f );
					}
					float finalAlpha = -( ratio * ratio ) + 1.0f;

					position.z += finalAlpha;
					wrapIndicatorObject->setLocalPosition( position );
				}
				else
				{
					wrapIndicatorObject->setVisible( false );
				}
			}
		}

		// Updating Scroll Suggestions
		bool 		showScrollUpIndicator 	= false;
		bool 		showBottomIndicator 	= false;
		Vector4f 	finalCol( 1.0f, 1.0f, 1.0f, 1.0f );
		if( LastInteractionTimeStamp > 0.0f ) // is user interaction currently going on? ( during interacion LastInteractionTimeStamp value will be negative )
		{
			float timeDiff = ovr_GetTimeInSeconds() - LastInteractionTimeStamp;
			if( timeDiff > SCROLL_HITNS_VISIBILITY_TOGGLE_TIME ) // is user not interacting for a while?
			{
				// Show Indicators
				showScrollUpIndicator = ( ScrollMgr.position() >  0.8f );
				showBottomIndicator = ( ScrollMgr.position() < ( ( ValidFoldersCount - 1 ) - 0.8f ) );

				finalCol.w = Alg::Clamp( timeDiff, 5.0f, 6.0f ) - 5.0f;
			}
		}

		VRMenuObject * scrollUpHintObject = menuMgr.toObject( ScrollUpHintHandle );
		if( scrollUpHintObject != NULL )
		{
			scrollUpHintObject->setVisible( showScrollUpIndicator );
			scrollUpHintObject->setColor( finalCol );
		}

		VRMenuObject * scrollDownHintObject = menuMgr.toObject( ScrollDownHintHandle );
		if( scrollDownHintObject != NULL )
		{
			scrollDownHintObject->setVisible(showBottomIndicator);
			scrollDownHintObject->setColor( finalCol );
		}

		return  MSG_STATUS_ALIVE;
    }

	eMsgStatus TouchDown( App * app, VrFrame const & vrFrame, OvrVRMenuMgr & menuMgr, VRMenuObject * self, VRMenuEvent const & event )
	{
		if ( FolderBrowser.hasNoMedia() )
		{
			return MSG_STATUS_CONSUMED;
		}

		FolderBrowser.touchDown();
		if ( ValidFoldersCount > 1 )
		{
			ScrollMgr.touchDown();
		}

		TotalTouchDistance = 0.0f;
		StartTouchPosition.x = event.floatValue.x;
		StartTouchPosition.y = event.floatValue.y;

		LastInteractionTimeStamp = -1.0f;

		return MSG_STATUS_ALIVE;	// don't consume -- we're just listening
	}

	eMsgStatus TouchUp( App * app, VrFrame const & vrFrame, OvrVRMenuMgr & menuMgr, VRMenuObject * self, VRMenuEvent const & event )
	{
		if ( FolderBrowser.hasNoMedia() )
		{
			return MSG_STATUS_CONSUMED;
		}

		FolderBrowser.touchUp();
		if ( ValidFoldersCount > 1 )
		{
			ScrollMgr.touchUp();
		}

		bool allowPanelTouchUp = false;
		if ( TotalTouchDistance < MAX_TOUCH_DISTANCE_FOR_TOUCH_SQ )
		{
			allowPanelTouchUp = true;
		}

		FolderBrowser.setAllowPanelTouchUp( allowPanelTouchUp );
		LastInteractionTimeStamp = ovr_GetTimeInSeconds();

		return MSG_STATUS_ALIVE;
	}

	eMsgStatus TouchRelative( App * app, VrFrame const & vrFrame, OvrVRMenuMgr & menuMgr, VRMenuObject * self, VRMenuEvent const & event )
	{
		if ( FolderBrowser.hasNoMedia() )
		{
			return MSG_STATUS_CONSUMED;
		}

		FolderBrowser.touchRelative(event.floatValue);
		Vector2f currentTouchPosition( event.floatValue.x, event.floatValue.y );
		TotalTouchDistance += ( currentTouchPosition - StartTouchPosition ).LengthSq();
		if ( ValidFoldersCount > 1 )
		{
			ScrollMgr.touchRelative(event.floatValue);
		}

		return MSG_STATUS_ALIVE;
	}

	eMsgStatus OnOpening( App * app, VrFrame const & vrFrame, OvrVRMenuMgr & menuMgr, VRMenuObject * self, VRMenuEvent const & event )
	{
		return MSG_STATUS_ALIVE;
	}

	// Return valid folder count, folder that has atleast one panel is considered as valid folder.
	int GetFolderBrowserValidFolderCount() const
	{
		int validFoldersCount = 0;
		const int folderCount = FolderBrowser.numFolders();
		for( int i = 0; i < folderCount; ++i )
		{
			const OvrFolderBrowser::FolderView * currentFolder = FolderBrowser.getFolderView( i );
			if ( currentFolder && currentFolder->panels.sizeInt() > 0 )
			{
				++validFoldersCount;
			}
		}
		return validFoldersCount;
	}

	OvrFolderBrowser &	FolderBrowser;
	OvrScrollManager	ScrollMgr;
	Vector2f			StartTouchPosition;
	float				TotalTouchDistance;
	int					ValidFoldersCount;

	menuHandle_t 		FoldersRootHandle;
	menuHandle_t		ScrollDownHintHandle;
	menuHandle_t		ScrollUpHintHandle;
	menuHandle_t		FoldersWrapHandle;
	Vector3f			FoldersWrapHandleTopPosition;
	Vector3f			FoldersWrapHandleBottomPosition;
	double				LastInteractionTimeStamp;
};

const float OvrFolderBrowserRootComponent::ACTIVE_DEPTH_OFFSET = 3.4f;

//==============================================================
// OvrFolderRootComponent
// Component attached to the root object of each folder
class OvrFolderRootComponent : public VRMenuComponent
{
public:
	OvrFolderRootComponent( OvrFolderBrowser & folderBrowser, OvrFolderBrowser::FolderView * folder )
		 : VRMenuComponent( VRMenuEventFlags_t( VRMENU_EVENT_FRAME_UPDATE )  )
		, FolderBrowser( folderBrowser )
		, FolderPtr( folder )
	{
		OVR_ASSERT( FolderPtr );
	}

private:
	virtual eMsgStatus      onEventImpl( App * app, VrFrame const & vrFrame, OvrVRMenuMgr & menuMgr,
											VRMenuObject * self, VRMenuEvent const & event )
		{
			switch( event.eventType )
	        {
	            case VRMENU_EVENT_FRAME_UPDATE:
	                return Frame( app, vrFrame, menuMgr, self, event );
	            default:
	                OVR_ASSERT( !"Event flags mismatch!" ); // the constructor is specifying a flag that's not handled
	                return MSG_STATUS_ALIVE;
	        }
		}

    eMsgStatus Frame( App * app, VrFrame const & vrFrame, OvrVRMenuMgr & menuMgr, VRMenuObject * self, VRMenuEvent const & event )
    {
		VRMenuObjectFlags_t flags = self->flags();
		OVR_ASSERT( FolderPtr );
		if ( FolderBrowser.getFolderView( FolderBrowser.activeFolderIndex() ) != FolderPtr )
		{
			flags |= VRMenuObjectFlags_t( VRMENUOBJECT_DONT_HIT_ALL );
		}
		else
		{
			flags &= ~VRMenuObjectFlags_t( VRMENUOBJECT_DONT_HIT_ALL );
		}
		self->setFlags( flags );

    	return MSG_STATUS_ALIVE;
    }

	OvrFolderBrowser &	FolderBrowser;
	OvrFolderBrowser::FolderView * FolderPtr;
};

//==============================================================
// OvrFolderSwipeComponent
// Component that holds panel sub-objects and manages swipe left/right
class OvrFolderSwipeComponent : public VRMenuComponent
{
public:
	static const int TYPE_ID = 58524;

	OvrFolderSwipeComponent( OvrFolderBrowser & folderBrowser, OvrFolderBrowser::FolderView * folder )
		: VRMenuComponent( VRMenuEventFlags_t( VRMENU_EVENT_FRAME_UPDATE ) | VRMENU_EVENT_TOUCH_RELATIVE | VRMENU_EVENT_TOUCH_DOWN | VRMENU_EVENT_TOUCH_UP )
		, FolderBrowser( folderBrowser )
		, ScrollMgr( HORIZONTAL_SCROLL )
		, FolderPtr( folder )
		, TouchDown( false )
    {
		OVR_ASSERT( FolderBrowser.circumferencePanelSlots() > 0 );
		ScrollMgr.setScrollPadding(0.5f);
		ScrollMgr.setWrapAroundEnable( false );
    }

	virtual int	typeId() const
	{
		return TYPE_ID;
	}

	bool Rotate( const OvrFolderBrowser::CategoryDirection direction )
	{
		static float const MAX_SPEED = 5.5f;
		switch ( direction )
		{
		case OvrFolderBrowser::MOVE_PANELS_LEFT:
			ScrollMgr.setVelocity( MAX_SPEED );
			return true;
		case OvrFolderBrowser::MOVE_PANELS_RIGHT:
			ScrollMgr.setVelocity( -MAX_SPEED );
			return true;
		default:
			return false;
		}
	}

	void SetRotationByRatio( const float ratio )
	{
		OVR_ASSERT( ratio >= 0.0f && ratio <= 1.0f );
		ScrollMgr.setPosition( FolderPtr->maxRotation * ratio );
		ScrollMgr.setVelocity( 0.0f );
	}

	void SetRotationByIndex( const int panelIndex )
	{
		OVR_ASSERT( panelIndex >= 0 && panelIndex < ( *FolderPtr ).panels.sizeInt() );
		ScrollMgr.setPosition( static_cast< float >( panelIndex ) );
	}

	void SetAccumulatedRotation( const float rot )
	{
		ScrollMgr.setPosition( rot );
		ScrollMgr.setVelocity( 0.0f );
	}

	float GetAccumulatedRotation() const { return ScrollMgr.position();  }

	int CurrentPanelIndex() const { return nearbyintf( ScrollMgr.position() ); }

private:
    virtual eMsgStatus Frame( App * app, VrFrame const & vrFrame, OvrVRMenuMgr & menuMgr, VRMenuObject * self, VRMenuEvent const & event )
    {
    	OVR_ASSERT( FolderPtr );
    	bool const isActiveFolder = ( FolderPtr == FolderBrowser.getFolderView( FolderBrowser.activeFolderIndex() ) );
		if ( !isActiveFolder )
		{
			TouchDown = false;
		}

		OvrFolderBrowser::FolderView & folder = *FolderPtr;
		const int numPanels = folder.panels.sizeInt();
		eScrollDirectionLockType touchDirLock = FolderBrowser.touchDirectionLock();
		eScrollDirectionLockType conrollerDirLock = FolderBrowser.controllerDirectionLock();

		ScrollMgr.setMaxPosition( static_cast<float>( numPanels - 1 ) );
		ScrollMgr.setRestrictedScrollingData( FolderBrowser.numFolders() > 1, touchDirLock, conrollerDirLock );

		unsigned int controllerInput = 0;
		if( isActiveFolder )
		{
			controllerInput = vrFrame.Input.buttonState;
			bool restrictedScrolling = FolderBrowser.numFolders() > 1;
			if( restrictedScrolling )
			{
				if ( touchDirLock == VERTICAL_LOCK )
				{
					if ( ScrollMgr.isTouchIsDown() )
					{
						// Restricted scrolling enabled, but locked to vertical scrolling so lose the touch input
						ScrollMgr.touchUp();
					}
				}

				if ( conrollerDirLock != HORIZONTAL_LOCK )
				{
					// Restricted scrolling enabled, but not locked to horizontal scrolling so lose the controller input
					controllerInput = 0;
				}
			}
		}
		else
		{
			if( ScrollMgr.isTouchIsDown() )
			{
				// While touch down this specific folder became inactive so perform touch up on this folder.
				ScrollMgr.touchUp();
			}
		}

		ScrollMgr.frame( vrFrame.DeltaSeconds, controllerInput );

    	if( numPanels <= 0 )
    	{
    		return MSG_STATUS_ALIVE;
    	}

		if ( 0 && numPanels > 1 ) // Make sure has atleast 1 panel to show wrap around indicators.
		{
			VRMenuObject * wrapIndicatorObject = menuMgr.toObject( folder.wrapIndicatorHandle );
			if ( ScrollMgr.isScrolling() && ScrollMgr.isOutOfBounds() )
			{
				static const float WRAP_INDICATOR_X_OFFSET = 0.2f;
				wrapIndicatorObject->setVisible( true );
				bool scrollingAtStart = ( ScrollMgr.position() < 0.0f );
				Vector3f position = wrapIndicatorObject->localPosition();
				if ( scrollingAtStart )
				{
					position.x = -WRAP_INDICATOR_X_OFFSET;
					UpdateWrapAroundIndicator( ScrollMgr, menuMgr, wrapIndicatorObject, position, 0.0f );
				}
				else
				{
					position.x = WRAP_INDICATOR_X_OFFSET;
					UpdateWrapAroundIndicator( ScrollMgr, menuMgr, wrapIndicatorObject, position, Mathf::Pi );
				}
			}
			else
			{
				wrapIndicatorObject->setVisible( false );
			}
		}

		const float position = ScrollMgr.position();

		// Send the position to the ScrollBar
		VRMenuObject * scrollBarObject = menuMgr.toObject( folder.scrollBarHandle );
		OVR_ASSERT( scrollBarObject != NULL );
		if ( isActiveFolder )
		{
			bool isHidden = false;
			if ( HIDE_SCROLLBAR_UNTIL_ITEM_COUNT > 0 )
			{
				isHidden = ScrollMgr.maxPosition() - (float)( HIDE_SCROLLBAR_UNTIL_ITEM_COUNT - 1 )  < Mathf::SmallestNonDenormal;
			}
			scrollBarObject->setVisible( !isHidden );
		}
		OvrScrollBarComponent * scrollBar = scrollBarObject->GetComponentByName< OvrScrollBarComponent >();
		if ( scrollBar != NULL )
		{
			scrollBar->setScrollFrac(  menuMgr, scrollBarObject, position );
		}

		// hide the scrollbar if not active
		const float velocity = ScrollMgr.velocity();
		if ( ( numPanels > 1 && TouchDown ) || fabsf( velocity ) >= 0.01f )
		{
			scrollBar->setScrollState( scrollBarObject, OvrScrollBarComponent::SCROLL_STATE_FADE_IN );
		}
		else if ( !TouchDown && ( !isActiveFolder || fabsf( velocity ) < 0.01f ) )
		{
			scrollBar->setScrollState( scrollBarObject, OvrScrollBarComponent::SCROLL_STATE_FADE_OUT );
		}

    	const float curRot = position * ( Mathf::TwoPi / FolderBrowser.circumferencePanelSlots() );
		Quatf rot( UP, curRot );
		rot.Normalize();
		self->setLocalRotation( rot );

		float alphaVal = ScrollMgr.wrapAroundAlphaChange();

		// show or hide panels based on current position
		//
		// for rendering, we want the switch to occur between panels - hence nearbyint
		const int curPanelIndex = CurrentPanelIndex();
		const int extraPanels = FolderBrowser.numSwipePanels() / 2;
		for ( int i = 0; i < numPanels; ++i )
		{
			const OvrFolderBrowser::PanelView & panel = folder.panels.at( i );
			OVR_ASSERT( panel.handle.IsValid() );
			VRMenuObject * panelObject = menuMgr.toObject( panel.handle );
			OVR_ASSERT( panelObject );

			VRMenuObjectFlags_t flags = panelObject->flags();
			if ( i >= curPanelIndex - extraPanels && i <= curPanelIndex + extraPanels )
			{
				flags &= ~( VRMenuObjectFlags_t( VRMENUOBJECT_DONT_RENDER ) | VRMENUOBJECT_DONT_HIT_ALL );
				panelObject->setFadeDirection( Vector3f( 0.0f ) );
				if ( i == curPanelIndex - extraPanels )
				{
					panelObject->setFadeDirection( -RIGHT );
				}
				else if ( i == curPanelIndex + extraPanels )
				{
					panelObject->setFadeDirection( RIGHT );
				}
			}
			else
			{
				flags |= VRMenuObjectFlags_t( VRMENUOBJECT_DONT_RENDER ) | VRMENUOBJECT_DONT_HIT_ALL;
			}
			panelObject->setFlags( flags );

			Vector4f color = panelObject->color();
			color.w = alphaVal;
			panelObject->setColor( color );
		}

		return MSG_STATUS_ALIVE;
    }

    eMsgStatus onEventImpl( App * app, VrFrame const & vrFrame, OvrVRMenuMgr & menuMgr, VRMenuObject * self, VRMenuEvent const & event )
    {
    	switch( event.eventType )
    	{
    		case VRMENU_EVENT_FRAME_UPDATE:
    			return Frame( app, vrFrame, menuMgr, self, event );
    		case VRMENU_EVENT_TOUCH_DOWN:
				return OnTouchDown_Impl( app, vrFrame, menuMgr, self, event );
    		case VRMENU_EVENT_TOUCH_UP:
				return OnTouchUp_Impl( app, vrFrame, menuMgr, self, event );
    		case VRMENU_EVENT_TOUCH_RELATIVE:
    			ScrollMgr.touchRelative( event.floatValue );
    			break;
    		default:
    			OVR_ASSERT( !"Event flags mismatch!" ); // the constructor is specifying a flag that's not handled
    			break;
    	}

    	return MSG_STATUS_ALIVE;
    }

	eMsgStatus OnTouchDown_Impl( App * app, VrFrame const & vrFrame, OvrVRMenuMgr & menuMgr, VRMenuObject * self, VRMenuEvent const & event )
	{
		ScrollMgr.touchDown();
		TouchDown = true;
		return MSG_STATUS_ALIVE;
	}

	eMsgStatus OnTouchUp_Impl( App * app, VrFrame const & vrFrame, OvrVRMenuMgr & menuMgr, VRMenuObject * self, VRMenuEvent const & event )
	{
		ScrollMgr.touchUp();
		TouchDown = false;
		return MSG_STATUS_ALIVE;
	}

private:
	OvrFolderBrowser&			FolderBrowser;
	OvrScrollManager			ScrollMgr;
	OvrFolderBrowser::FolderView *	FolderPtr;			// Correlates the folder browser component to the folder it belongs to
	bool						TouchDown;
};

//==============================
// OvrFolderBrowser
unsigned char * OvrFolderBrowser::ThumbPanelBG = NULL;

OvrFolderBrowser::OvrFolderBrowser(
		App * app,
		OvrMetaData & metaData,
		float panelWidth,
		float panelHeight,
		float radius_,
		unsigned numSwipePanels,
		unsigned thumbWidth,
		unsigned thumbHeight )
	: VRMenu( MENU_NAME )
	, m_app( app )
	, m_mediaCount( 0 )
	, m_thumbnailThreadId( -1 )
	, m_panelWidth( 0.0f )
	, m_panelHeight( 0.0f )
	, m_thumbWidth( thumbWidth )
	, m_thumbHeight( thumbHeight )
	, m_panelTextSpacingScale( 0.5f )
	, m_folderTitleSpacingScale( 0.5f )
	, m_scrollBarSpacingScale( 0.5f )
	, m_scrollBarRadiusScale( 0.97f )
	, m_numSwipePanels( numSwipePanels )
	, m_noMedia( false )
	, m_allowPanelTouchUp( false )
	, m_textureCommands( 10000 )
	, m_backgroundCommands( 10000 )
	, m_controllerDirectionLock( NO_LOCK )
	, LastControllerInputTimeStamp( 0.0f )
	, m_isTouchDownPosistionTracked( false )
	, m_touchDirectionLocked( NO_LOCK )
{
	m_defaultPanelTextureIds[ 0 ] = 0;
	m_defaultPanelTextureIds[ 1 ] = 0;

	//  Load up thumbnail alpha from panel.tga
	if ( ThumbPanelBG == NULL )
	{
		void * 	buffer;
        uint		bufferLength;

		const char * panel = NULL;

		if ( m_thumbWidth == m_thumbHeight )
		{
			panel = "res/raw/panel_square.tga";
		}
		else
		{
			panel = "res/raw/panel.tga";
		}

        const VApkFile &apk = VApkFile::CurrentApkFile();
        apk.read(panel, buffer, bufferLength);

		int panelW = 0;
		int panelH = 0;
		ThumbPanelBG = stbi_load_from_memory( ( stbi_uc const * )buffer, bufferLength, &panelW, &panelH, NULL, 4 );

		OVR_ASSERT( ThumbPanelBG != 0 && panelW == m_thumbWidth && panelH == m_thumbHeight );
	}

	// load up the default panel textures once
	{
		const char * panelSrc[ 2 ] = {};

		if ( m_thumbWidth == m_thumbHeight )
		{
			panelSrc[ 0 ] = "res/raw/panel_square.tga";
			panelSrc[ 1 ] = "res/raw/panel_hi_square.tga";
		}
		else
		{
			panelSrc[ 0 ]  = "res/raw/panel.tga";
			panelSrc[ 1 ]  = "res/raw/panel_hi.tga";
		}

		for ( int t = 0; t < 2; ++t )
		{
			int width = 0;
			int height = 0;
			m_defaultPanelTextureIds[ t ] = LoadTextureFromApplicationPackage( panelSrc[ t ], TextureFlags_t( TEXTUREFLAG_NO_DEFAULT ), width, height );
			OVR_ASSERT( m_defaultPanelTextureIds[ t ] && ( width == m_thumbWidth ) && ( height == m_thumbHeight ) );
		}
	}

	// spawn the thumbnail loading thread with the command list
	pthread_attr_t loadingThreadAttr;
	pthread_attr_init( &loadingThreadAttr );
	sched_param sparam;
    sparam.sched_priority = VThread::GetOSPriority(VThread::BelowNormalPriority);
	pthread_attr_setschedparam( &loadingThreadAttr, &sparam );

	const int createErr = pthread_create( &m_thumbnailThreadId, &loadingThreadAttr, &ThumbnailThread, this );
	if ( createErr != 0 )
	{
		LOG( "pthread_create returned %i", createErr );
	}

	m_panelWidth = panelWidth * VRMenuObject::DEFAULT_TEXEL_SCALE;
	m_panelHeight = panelHeight * VRMenuObject::DEFAULT_TEXEL_SCALE;
	m_radius = radius_;
	const float circumference = Mathf::TwoPi * m_radius;

	m_circumferencePanelSlots = ( int )( floor( circumference / m_panelWidth ) );
	m_visiblePanelsArcAngle = (( float )( m_numSwipePanels + 1 ) / m_circumferencePanelSlots ) * Mathf::TwoPi;

	Array< VRMenuComponent* > comps;
	comps.append( new OvrFolderBrowserRootComponent( *this ) );
	init( app->GetVRMenuMgr(), app->GetDefaultFont(), 0.0f, VRMenuFlags_t(), comps );
}

OvrFolderBrowser::~OvrFolderBrowser()
{
	LOG( "OvrFolderBrowser::~OvrFolderBrowser" );
	m_backgroundCommands.PostString( "shutDown" );

	if ( ThumbPanelBG != NULL )
	{
		free( ThumbPanelBG );
		ThumbPanelBG = NULL;
	}

	for ( int t = 0; t < 2; ++t )
	{
		glDeleteTextures( 1, &m_defaultPanelTextureIds[ t ] );
		m_defaultPanelTextureIds[ t ] = 0;
	}

	int numFolders = m_folders.sizeInt();
	for ( int i = 0; i < numFolders; ++i )
	{
		FolderView * folder = m_folders.at( i );
		if ( folder )
		{
			delete folder;
		}
	}

	LOG( "OvrFolderBrowser::~OvrFolderBrowser COMPLETE" );
}

void OvrFolderBrowser::frameImpl( App * app, VrFrame const & vrFrame, OvrVRMenuMgr & menuMgr, BitmapFont const & font,
	BitmapFontSurface & fontSurface, gazeCursorUserId_t const gazeUserId )
{
	// Check for thumbnail loads
	while ( 1 )
	{
		const char * cmd = m_textureCommands.nextMessage();
		if ( !cmd )
		{
			break;
		}

		//LOG( "TextureCommands: %s", cmd );
		loadThumbnailToTexture( cmd );
		free( ( void * )cmd );
	}

	// --
	// Logic for restricted scrolling
	unsigned int controllerInput = vrFrame.Input.buttonState;
	bool rightPressed 	= controllerInput & ( BUTTON_LSTICK_RIGHT | BUTTON_DPAD_RIGHT );
	bool leftPressed 	= controllerInput & ( BUTTON_LSTICK_LEFT | BUTTON_DPAD_LEFT );
	bool downPressed 	= controllerInput & ( BUTTON_LSTICK_DOWN | BUTTON_DPAD_DOWN );
	bool upPressed 		= controllerInput & ( BUTTON_LSTICK_UP | BUTTON_DPAD_UP );

	if( !( rightPressed || leftPressed || downPressed || upPressed ) )
	{
		if( m_controllerDirectionLock != NO_LOCK )
		{
			if( ovr_GetTimeInSeconds() - LastControllerInputTimeStamp >= CONTROLER_COOL_DOWN )
			{
				// Didn't receive any input for last 'CONTROLER_COOL_DOWN' seconds, so release direction lock
				m_controllerDirectionLock = NO_LOCK;
			}
		}
	}
	else
	{
		if( rightPressed || leftPressed )
		{
			if( m_controllerDirectionLock == VERTICAL_LOCK )
			{
				rightPressed = false;
				leftPressed = false;
			}
			else
			{
				if( m_controllerDirectionLock != HORIZONTAL_LOCK )
				{
					m_controllerDirectionLock = HORIZONTAL_LOCK;
				}
				LastControllerInputTimeStamp = ovr_GetTimeInSeconds();
			}
		}

		if( downPressed || upPressed )
		{
			if( m_controllerDirectionLock == HORIZONTAL_LOCK )
			{
				downPressed = false;
				upPressed = false;
			}
			else
			{
				if( m_controllerDirectionLock != VERTICAL_LOCK )
				{
					m_controllerDirectionLock = VERTICAL_LOCK;
				}
				LastControllerInputTimeStamp = ovr_GetTimeInSeconds();
			}
		}
	}
}

void OvrFolderBrowser::openImpl( App * app, OvrGazeCursor & gazeCursor )
{
	if ( m_noMedia )
	{
		return;
	}

	// Rebuild favorites if not empty
	onBrowserOpen();
}

void OvrFolderBrowser::oneTimeInit()
{
	const VStandardPath & storagePaths = m_app->GetStoragePaths();
    storagePaths.GetPathIfValidPermission(VStandardPath::PrimaryExternalStorage, VStandardPath::CacheFolder, "", W_OK, m_appCachePath );
	OVR_ASSERT( !m_appCachePath.isEmpty() );

    storagePaths.PushBackSearchPathIfValid(VStandardPath::SecondaryExternalStorage, VStandardPath::RootFolder, "RetailMedia/", m_thumbSearchPaths );
    storagePaths.PushBackSearchPathIfValid(VStandardPath::SecondaryExternalStorage, VStandardPath::RootFolder, "", m_thumbSearchPaths );
    storagePaths.PushBackSearchPathIfValid(VStandardPath::PrimaryExternalStorage, VStandardPath::RootFolder, "RetailMedia/", m_thumbSearchPaths );
    storagePaths.PushBackSearchPathIfValid(VStandardPath::PrimaryExternalStorage, VStandardPath::RootFolder, "", m_thumbSearchPaths );
	OVR_ASSERT( !m_thumbSearchPaths.isEmpty() );

	// move the root up to eye height
	OvrVRMenuMgr & menuManager = m_app->GetVRMenuMgr();
	BitmapFont & font = m_app->GetDefaultFont();
	VRMenuObject * root = menuManager.toObject( rootHandle() );
	OVR_ASSERT( root );
	if ( root != NULL )
	{
		Vector3f pos = root->localPosition();
		pos.y += EYE_HEIGHT_OFFSET;
		root->setLocalPosition( pos );
	}

	Array< VRMenuComponent* > comps;
	Array< VRMenuObjectParms const * > parms;

	// Folders root folder
	m_foldersRootId = VRMenuId_t( uniqueId.Get( 1 ) );
	VRMenuObjectParms foldersRootParms(
		VRMENU_CONTAINER,
		comps,
		VRMenuSurfaceParms(),
		"Folder Browser Folders",
		Posef(),
		Vector3f( 1.0f ),
		VRMenuFontParms(),
		m_foldersRootId,
		VRMenuObjectFlags_t(),
		VRMenuObjectInitFlags_t()
		);
	parms.append( &foldersRootParms );
	addItems( menuManager, font, parms, rootHandle(), false );
	parms.clear();
	comps.clear();

	// Build scroll up/down hints attached to root
	VRMenuId_t scrollSuggestionRootId( uniqueId.Get( 1 ) );

	VRMenuObjectParms scrollSuggestionParms(
		VRMENU_CONTAINER,
		comps,
		VRMenuSurfaceParms(),
		"scroll hints",
		Posef(),
		Vector3f( 1.0f ),
		VRMenuFontParms(),
		scrollSuggestionRootId,
		VRMenuObjectFlags_t(),
		VRMenuObjectInitFlags_t()
		);
	parms.append( &scrollSuggestionParms );
	addItems( menuManager, font, parms, rootHandle(), false );
	parms.clear();

	m_scrollSuggestionRootHandle = root->childHandleForId( menuManager, scrollSuggestionRootId );
	OVR_ASSERT( m_scrollSuggestionRootHandle.IsValid() );

	VRMenuId_t suggestionDownId( uniqueId.Get( 1 ) );
	VRMenuId_t suggestionUpId( uniqueId.Get( 1 ) );

	const Posef swipeDownPose( Quatf(), FWD * ( 0.33f * m_radius ) + DOWN * m_panelHeight * 0.5f );
	menuHandle_t scrollDownHintHandle = OvrSwipeHintComponent::CreateSwipeSuggestionIndicator( m_app, this, m_scrollSuggestionRootHandle, suggestionDownId.Get(),
		"res/raw/swipe_suggestion_arrow_down.png", swipeDownPose, DOWN );

	const Posef swipeUpPose( Quatf(), FWD * ( 0.33f * m_radius ) + UP * m_panelHeight * 0.5f );
	menuHandle_t scrollUpHintHandle = OvrSwipeHintComponent::CreateSwipeSuggestionIndicator( m_app, this, m_scrollSuggestionRootHandle, suggestionUpId.Get(),
		"res/raw/swipe_suggestion_arrow_up.png", swipeUpPose, UP );

	OvrFolderBrowserRootComponent * rootComp = root->GetComponentById<OvrFolderBrowserRootComponent>();
	OVR_ASSERT( rootComp );

	menuHandle_t foldersRootHandle = root->childHandleForId( menuManager, m_foldersRootId );
	OVR_ASSERT( foldersRootHandle.IsValid() );
	rootComp->SetFoldersRootHandle( foldersRootHandle );

	OVR_ASSERT( scrollUpHintHandle.IsValid() );
	rootComp->SetScrollDownHintHandle( scrollDownHintHandle );

	OVR_ASSERT( scrollDownHintHandle.IsValid() );
	rootComp->SetScrollUpHintHandle( scrollUpHintHandle );
}

void OvrFolderBrowser::buildDirtyMenu( OvrMetaData & metaData )
{
	OvrVRMenuMgr & menuManager = m_app->GetVRMenuMgr();
	BitmapFont & font = m_app->GetDefaultFont();
	VRMenuObject * root = menuManager.toObject( rootHandle() );
	OVR_ASSERT( root );

	Array< VRMenuComponent* > comps;
	Array< const VRMenuObjectParms * > parms;

	Array< OvrMetaData::Category > categories = metaData.categories();
	const int numCategories = categories.sizeInt();

	// load folders and position
	for ( int catIndex = 0; catIndex < numCategories; ++catIndex )
	{
		// Load a category to build swipe folder
		OvrMetaData::Category & currentCategory = metaData.getCategory( catIndex );
		if ( currentCategory.dirty ) // Only build if dirty
		{
			LOG( "Loading folder %i named %s", catIndex, currentCategory.categoryTag.toCString() );
			FolderView * folder = getFolderView( currentCategory.categoryTag );

			// if building for the first time
			if ( folder == NULL )
			{
				// Create internal folder struct
				VString localizedCategoryName;

				// Get localized tag (folder title)
                localizedCategoryName = getCategoryTitle( VrLocale::MakeStringId(currentCategory.categoryTag).toCString(), currentCategory.categoryTag.toCString() );

				// the localization is now done app-side
//				VrLocale::GetString( AppPtr->GetVrJni(), AppPtr->GetJavaObject(),
//					VrLocale::MakeStringIdFromANSI( currentCategory.CategoryTag ), currentCategory.CategoryTag, localizedCategoryName );

				folder = new FolderView( localizedCategoryName, currentCategory.categoryTag );
				m_folders.append( folder );

				buildFolder( currentCategory, folder, metaData, m_foldersRootId, catIndex );

				updateFolderTitle( folder );
				folder->maxRotation = calcFolderMaxRotation( folder );
			}
			else // already have this folder - rebuild it with the new metadata
			{
				Array< const OvrMetaDatum * > folderMetaData;
                if ( metaData.getMetaData(currentCategory, folderMetaData ) )
				{
					rebuildFolder( metaData, catIndex, folderMetaData );
				}
				else
				{
					LOG( "Failed to get any metaData for folder %i named %s", catIndex, currentCategory.categoryTag.toCString() );
				}
			}

			currentCategory.dirty = false;

			// Set up initial positions - 0 in center, the rest ascending in order below it
			m_mediaCount += folder->panels.sizeInt();
			VRMenuObject * folderObject = menuManager.toObject( folder->handle );
			OVR_ASSERT( folderObject != NULL );
			folderObject->setLocalPosition( ( DOWN * m_panelHeight * catIndex ) + folderObject->localPosition() );
		}
	}

	const VRMenuFontParms fontParms( HORIZONTAL_CENTER, VERTICAL_CENTER, false, false, true, 0.525f, 0.45f, 0.5f );

	// Process any thumb creation commands
	char cmd[ 1024 ];
	OVR_sprintf( cmd, 1024, "processCreates %p", &m_thumbCreateAndLoadCommands );
	m_backgroundCommands.PostString( cmd );

	// Show no media menu if no media found
	if ( m_mediaCount == 0 )
	{
		VString title;
		VString imageFile;
		VString message;
		onMediaNotFound( m_app, title, imageFile, message );

		// Create a folder if we didn't create at least one to display no media info
		if ( m_folders.sizeInt() < 1 )
		{
			const VString noMediaTag( "No Media" );
			const_cast< OvrMetaData & >( metaData ).addCategory( noMediaTag );
			OvrMetaData::Category & noMediaCategory = metaData.getCategory( 0 );
			FolderView * noMediaView = new FolderView( noMediaTag, noMediaTag );
			buildFolder( noMediaCategory, noMediaView, metaData, m_foldersRootId, 0 );
			m_folders.append( noMediaView );
		}

		// Set title
		const FolderView * folder = getFolderView( 0 );
		OVR_ASSERT ( folder != NULL );
		VRMenuObject * folderTitleObject = menuManager.toObject( folder->titleHandle );
		OVR_ASSERT( folderTitleObject != NULL );
        folderTitleObject->setText(title);
		VRMenuObjectFlags_t flags = folderTitleObject->flags();
		flags &= ~VRMenuObjectFlags_t( VRMENUOBJECT_DONT_RENDER );
		folderTitleObject->setFlags( flags );

		// Add no media panel
		const Vector3f dir( FWD );
		const Posef panelPose( Quatf(), dir * m_radius );
		const Vector3f panelScale( 1.0f );
		const Posef textPose( Quatf(), Vector3f( 0.0f, -0.3f, 0.0f ) );

		VRMenuSurfaceParms panelSurfParms( "panelSurface",
            imageFile.toCString(), SURFACE_TEXTURE_DIFFUSE,
			NULL, SURFACE_TEXTURE_MAX,
			NULL, SURFACE_TEXTURE_MAX );

		VRMenuObjectParms * p = new VRMenuObjectParms( VRMENU_STATIC, Array< VRMenuComponent* >(),
            panelSurfParms, message.toCString(), panelPose, panelScale, textPose, Vector3f( 1.3f ), fontParms, VRMenuId_t(),
			VRMenuObjectFlags_t( VRMENUOBJECT_DONT_HIT_ALL ), VRMenuObjectInitFlags_t( VRMENUOBJECT_INIT_FORCE_POSITION ) );

		parms.append( p );
		addItems( menuManager, font, parms, folder->swipeHandle, true ); // PARENT: folder.TitleRootHandle
		parms.clear();

		m_noMedia = true;

		// Hide scroll hints while no media
		VRMenuObject * scrollHintRootObject = menuManager.toObject( m_scrollSuggestionRootHandle );
		OVR_ASSERT( scrollHintRootObject  );
		scrollHintRootObject->setVisible( false );

		return;
	}

	if ( 0 ) // DISABLED UP/DOWN wrap indicators
	{
		// Show  wrap around indicator if we have more than one non empty folder
		const FolderView * topFolder = getFolderView( 0 );
		if( topFolder && ( ( m_folders.sizeInt() > 3 ) ||
			( m_folders.sizeInt() == 4 && !topFolder->panels.isEmpty() ) ) )
		{
			const char * indicatorLeftIcon = "res/raw/wrap_left.png";
			VRMenuSurfaceParms indicatorSurfaceParms( "leftSurface",
					indicatorLeftIcon, SURFACE_TEXTURE_DIFFUSE,
					NULL, SURFACE_TEXTURE_MAX, NULL, SURFACE_TEXTURE_MAX );
			// Wrap around indicator - used for indicating all folders/category wrap around.
			VRMenuId_t indicatorId( uniqueId.Get( 1 ) );
			Posef indicatorPos( Quatf(), FWD * ( m_radius + 0.1f ) + UP * m_panelHeight * 0.0f );
			VRMenuObjectParms indicatorParms(
					VRMENU_STATIC,
					Array< VRMenuComponent* >(),
					indicatorSurfaceParms,
					"",
					indicatorPos,
					Vector3f( 3.0f ),
					fontParms,
					indicatorId,
					VRMenuObjectFlags_t( VRMENUOBJECT_DONT_HIT_ALL ),
					VRMenuObjectInitFlags_t( VRMENUOBJECT_INIT_FORCE_POSITION ) );

			parms.append( &indicatorParms );
			addItems(menuManager, font, parms, rootHandle(), true);
			menuHandle_t foldersWrapHandle = root->childHandleForId(menuManager, indicatorId);
			VRMenuObject * wrapIndicatorObject = menuManager.toObject( foldersWrapHandle );
			OVR_UNUSED( wrapIndicatorObject );
			OVR_ASSERT( wrapIndicatorObject != NULL );

			OvrFolderBrowserRootComponent * rootComp = root->GetComponentById<OvrFolderBrowserRootComponent>();
			OVR_ASSERT( rootComp );
			rootComp->SetFoldersWrapHandle( foldersWrapHandle );
			rootComp->SetFoldersWrapHandleTopPosition( FWD * ( 0.52f * m_radius) + UP * m_panelHeight * 1.0f );
			rootComp->SetFoldersWrapHandleBottomPosition( FWD * ( 0.52f * m_radius) + DOWN * m_panelHeight * numCategories );

			wrapIndicatorObject->setVisible( false );
			parms.clear();
		}
	}
}

void OvrFolderBrowser::buildFolder( OvrMetaData::Category & category, FolderView * const folder, const OvrMetaData & metaData, VRMenuId_t foldersRootId, int folderIndex )
{
	OVR_ASSERT( folder );

	OvrVRMenuMgr & menuManager = m_app->GetVRMenuMgr();
	BitmapFont & font = m_app->GetDefaultFont();

	Array< const VRMenuObjectParms * > parms;
	const VRMenuFontParms fontParms( HORIZONTAL_CENTER, VERTICAL_CENTER, false, false, true, 0.525f, 0.45f, 1.0f );

	const int numPanels = category.datumIndicies.sizeInt();

	VRMenuObject * root = menuManager.toObject( rootHandle() );
	menuHandle_t foldersRootHandle = root ->childHandleForId( menuManager, foldersRootId );

	// Create OvrFolderRootComponent for folder root
	const VRMenuId_t folderId( uniqueId.Get( 1 ) );
	LOG( "Building Folder %s id: %d with %d panels", category.categoryTag.toCString(), folderId.Get(), numPanels );
	Array< VRMenuComponent* > comps;
	comps.append( new OvrFolderRootComponent( *this, folder ) );
	VRMenuObjectParms folderParms(
		VRMENU_CONTAINER,
		comps,
		VRMenuSurfaceParms(),
		( folder->localizedName + " root" ).toCString(),
		Posef(),
		Vector3f( 1.0f ),
		fontParms,
		folderId,
		VRMenuObjectFlags_t(),
		VRMenuObjectInitFlags_t( VRMENUOBJECT_INIT_FORCE_POSITION )
		);
	parms.append( &folderParms );
	addItems( menuManager, font, parms, foldersRootHandle, false ); // PARENT: Root
	parms.clear();

	// grab the folder handle and make sure it was created
	folder->handle = handleForId( menuManager, folderId );
	VRMenuObject * folderObject = menuManager.toObject( folder->handle );
	OVR_UNUSED( folderObject );

	// Add horizontal scrollbar to folder
	Posef scrollBarPose( Quatf(), FWD * m_radius * m_scrollBarRadiusScale );

	// Create unique ids for the scrollbar objects
	const VRMenuId_t scrollRootId( uniqueId.Get( 1 ) );
	const VRMenuId_t scrollXFormId( uniqueId.Get( 1 ) );
	const VRMenuId_t scrollBaseId( uniqueId.Get( 1 ) );
	const VRMenuId_t scrollThumbId( uniqueId.Get( 1 ) );

	// Set the border of the thumb image for 9-slicing
	const Vector4f scrollThumbBorder( 0.0f, 0.0f, 0.0f, 0.0f );
	const Vector3f xFormPos = DOWN * m_thumbHeight * VRMenuObject::DEFAULT_TEXEL_SCALE * m_scrollBarSpacingScale;

	// Build the scrollbar
	OvrScrollBarComponent::getScrollBarParms( *this, SCROLL_BAR_LENGTH, folderId, scrollRootId, scrollXFormId, scrollBaseId, scrollThumbId,
		scrollBarPose, Posef( Quatf(), xFormPos ), 0, numPanels, false, scrollThumbBorder, parms );
	addItems( menuManager, font, parms, folder->handle, false ); // PARENT: folder->Handle
	parms.clear();

	// Cache off the handle and verify successful creation
	folder->scrollBarHandle = folderObject->childHandleForId( menuManager, scrollRootId );
	VRMenuObject * scrollBarObject = menuManager.toObject( folder->scrollBarHandle );
	OVR_ASSERT( scrollBarObject != NULL );
	OvrScrollBarComponent * scrollBar = scrollBarObject->GetComponentByName< OvrScrollBarComponent >();
	if ( scrollBar != NULL )
	{
		scrollBar->updateScrollBar( menuManager, scrollBarObject, numPanels );
		scrollBar->setScrollFrac( menuManager, scrollBarObject, 0.0f );
		scrollBar->setBaseColor( menuManager, scrollBarObject, Vector4f( 1.0f, 1.0f, 1.0f, 1.0f ) );

		// Hide the scrollbar
		VRMenuObjectFlags_t flags = scrollBarObject->flags();
		scrollBarObject->setFlags( flags |= VRMenuObjectFlags_t( VRMENUOBJECT_DONT_RENDER ) );
	}

	// Add OvrFolderSwipeComponent to folder - manages panel swiping
	VRMenuId_t swipeFolderId( uniqueId.Get( 1 ) );
	Array< VRMenuComponent* > swipeComps;
	swipeComps.append( new OvrFolderSwipeComponent( *this, folder ) );
	VRMenuObjectParms swipeParms(
		VRMENU_CONTAINER,
		swipeComps,
		VRMenuSurfaceParms(),
		( folder->localizedName + " swipe" ).toCString(),
		Posef(),
		Vector3f( 1.0f ),
		fontParms,
		swipeFolderId,
		VRMenuObjectFlags_t( VRMENUOBJECT_NO_GAZE_HILIGHT ),
		VRMenuObjectInitFlags_t( VRMENUOBJECT_INIT_FORCE_POSITION )
		);
	parms.append( &swipeParms );
	addItems( menuManager, font, parms, folder->handle, false ); // PARENT: folder->Handle
	parms.clear();

	// grab the SwipeHandle handle and make sure it was created
	folder->swipeHandle = folderObject->childHandleForId( menuManager, swipeFolderId );
	VRMenuObject * swipeObject = menuManager.toObject( folder->swipeHandle );
	OVR_ASSERT( swipeObject != NULL );

	// build a collision primitive that encompasses all of the panels for a raw (including the empty space between them)
	// so that we can always send our swipe messages to the correct row based on gaze.
	VArray< Vector3f > vertices( m_circumferencePanelSlots * 2 );
	VArray< TriangleIndex > indices( m_circumferencePanelSlots * 6 );
	int curIndex = 0;
	int curVertex = 0;
	for ( int i = 0; i < m_circumferencePanelSlots; ++i )
	{
		float theta = ( i * Mathf::TwoPi ) / m_circumferencePanelSlots;
		float x = cos( theta ) * m_radius * 1.05f;
		float z = sin( theta ) * m_radius * 1.05f;
		Vector3f topVert( x, m_panelHeight * 0.5f, z );
		Vector3f bottomVert( x, m_panelHeight * -0.5f, z );

		vertices[curVertex + 0] = topVert;
		vertices[curVertex + 1] = bottomVert;
		if ( i > 0 )	// only set up indices if we already have one column's vertices
		{
			// first tri
			indices[curIndex + 0] = curVertex + 1;
			indices[curIndex + 1] = curVertex + 0;
			indices[curIndex + 2] = curVertex - 1;
			// second tri
			indices[curIndex + 3] = curVertex + 0;
			indices[curIndex + 4] = curVertex - 2;
			indices[curIndex + 5] = curVertex - 1;
			curIndex += 6;
		}

		curVertex += 2;
	}
	// connect the last vertices to the first vertices for the last sector
	indices[curIndex + 0] = 1;
	indices[curIndex + 1] = 0;
	indices[curIndex + 2] = curVertex - 1;
	indices[curIndex + 3] = 0;
	indices[curIndex + 4] = curVertex - 2;
	indices[curIndex + 5] = curVertex - 1;

	// create and set the collision primitive on the swipe object
	OvrTriCollisionPrimitive * cp = new OvrTriCollisionPrimitive( vertices, indices, ContentFlags_t( CONTENT_SOLID ) );
	swipeObject->setCollisionPrimitive( cp );

	if ( !category.datumIndicies.isEmpty() )
	{
		// Grab panel parms
		loadFolderPanels( metaData, category, folderIndex, *folder, parms );

		// Add panels to swipehandle
		addItems( menuManager, font, parms, folder->swipeHandle, false );
		DeletePointerArray( parms );
		parms.clear();

		// Assign handles to panels
		for ( int i = 0; i < folder->panels.sizeInt(); ++i )
		{
			PanelView& panel = folder->panels.at( i );
			panel.handle = swipeObject->getChildHandleForIndex( i );
		}
	}

	// Folder title
	VRMenuId_t folderTitleRootId( uniqueId.Get( 1 ) );
	VRMenuObjectParms titleRootParms(
		VRMENU_CONTAINER,
		Array< VRMenuComponent* >(),
		VRMenuSurfaceParms(),
		( folder->localizedName + " title root" ).toCString(),
		Posef(),
		Vector3f( 1.0f ),
		fontParms,
		folderTitleRootId,
		VRMenuObjectFlags_t(),
		VRMenuObjectInitFlags_t( VRMENUOBJECT_INIT_FORCE_POSITION )
		);
	parms.append( &titleRootParms );
	addItems( menuManager, font, parms, folder->handle, false ); // PARENT: folder->Handle
	parms.clear();

	// grab the title root handle and make sure it was created
	folder->titleRootHandle = folderObject->childHandleForId( menuManager, folderTitleRootId );
	VRMenuObject * folderTitleRootObject = menuManager.toObject( folder->titleRootHandle );
	OVR_UNUSED( folderTitleRootObject );
	OVR_ASSERT( folderTitleRootObject != NULL );

	VRMenuId_t folderTitleId( uniqueId.Get( 1 ) );
	Posef titlePose( Quatf(), FWD * m_radius + UP * m_panelHeight * m_folderTitleSpacingScale );
	VRMenuObjectParms titleParms(
		VRMENU_STATIC,
		Array< VRMenuComponent* >(),
		VRMenuSurfaceParms(),
		"no title",
		titlePose,
		Vector3f( 1.25f ),
		fontParms,
		folderTitleId,
		VRMenuObjectFlags_t( VRMENUOBJECT_DONT_HIT_TEXT ),
		VRMenuObjectInitFlags_t( VRMENUOBJECT_INIT_FORCE_POSITION ) );
	parms.append( &titleParms );
	addItems( menuManager, font, parms, folder->titleRootHandle, true ); // PARENT: folder->TitleRootHandle
	parms.clear();

	// grab folder title handle and make sure it was created
	folder->titleHandle = folderTitleRootObject->childHandleForId( menuManager, folderTitleId );
	VRMenuObject * folderTitleObject = menuManager.toObject( folder->titleHandle );
	OVR_UNUSED( folderTitleObject );
	OVR_ASSERT( folderTitleObject != NULL );

	// Wrap around indicator
	VRMenuId_t indicatorId( uniqueId.Get( 1 ) );
	Posef indicatorPos( Quatf(), FWD * ( m_radius + 0.1f ) + UP * m_panelHeight * 0.0f );

	const char * leftIcon = "res/raw/wrap_left.png";
	VRMenuSurfaceParms indicatorSurfaceParms( "leftSurface",
			leftIcon, SURFACE_TEXTURE_DIFFUSE,
			NULL, SURFACE_TEXTURE_MAX, NULL, SURFACE_TEXTURE_MAX );

	VRMenuObjectParms indicatorParms(
			VRMENU_STATIC,
			Array< VRMenuComponent* >(),
			indicatorSurfaceParms,
			"wrap indicator",
			indicatorPos,
			Vector3f( 3.0f ),
			fontParms,
			indicatorId,
			VRMenuObjectFlags_t( VRMENUOBJECT_DONT_HIT_ALL ) | VRMENUOBJECT_DONT_RENDER_TEXT,
			VRMenuObjectInitFlags_t( VRMENUOBJECT_INIT_FORCE_POSITION ) );

	parms.append( &indicatorParms );

	addItems( menuManager, font, parms, folder->titleRootHandle, true ); // PARENT: folder->TitleRootHandle
	folder->wrapIndicatorHandle = folderTitleRootObject->childHandleForId( menuManager, indicatorId );
	VRMenuObject * wrapIndicatorObject = menuManager.toObject( folder->wrapIndicatorHandle );
	OVR_UNUSED( wrapIndicatorObject );
	OVR_ASSERT( wrapIndicatorObject != NULL );

	parms.clear();

	setWrapIndicatorVisible( *folder, false );
}

void OvrFolderBrowser::rebuildFolder( OvrMetaData & metaData, const int folderIndex, const Array< const OvrMetaDatum * > & data )
{
	if ( folderIndex >= 0 && m_folders.sizeInt() > folderIndex )
	{
		OvrVRMenuMgr & menuManager = m_app->GetVRMenuMgr();
		BitmapFont & font = m_app->GetDefaultFont();

		FolderView * folder = getFolderView( folderIndex );
		if ( folder == NULL )
		{
			LOG( "OvrFolderBrowser::RebuildFolder failed to Folder for folderIndex %d", folderIndex );
			return;
		}

		VRMenuObject * swipeObject = menuManager.toObject( folder->swipeHandle );
		OVR_ASSERT( swipeObject );

		swipeObject->freeChildren( menuManager );
		folder->panels.clear();

		const int numPanels = data.sizeInt();
		Array< const VRMenuObjectParms * > outParms;
		Array< int > newDatumIndicies;
		for ( int panelIndex = 0; panelIndex < numPanels; ++panelIndex )
		{
			const OvrMetaDatum * panelData = data.at( panelIndex );
			if ( panelData )
			{
				addPanelToFolder( data.at( panelIndex ), folderIndex, *folder, outParms );
				addItems( menuManager, font, outParms, folder->swipeHandle, false );
				DeletePointerArray( outParms );
				outParms.clear();

				// Assign handle to panel
				PanelView& panel = folder->panels.at( panelIndex );
				panel.handle = swipeObject->getChildHandleForIndex( panelIndex );

				newDatumIndicies.append( panelData->id );
			}
		}

		metaData.setCategoryDatumIndicies( folderIndex, newDatumIndicies );

		OvrFolderSwipeComponent * swipeComp = swipeObject->GetComponentById< OvrFolderSwipeComponent >();
		OVR_ASSERT( swipeComp );
		updateFolderTitle( folder );

		// Recalculate accumulated rotation in the swipe component based on ratio of where user left off before adding/removing favorites
		const float currentMaxRotation = folder->maxRotation > 0.0f ? folder->maxRotation : 1.0f;
		const float positionRatio = Alg::Clamp( swipeComp->GetAccumulatedRotation() / currentMaxRotation, 0.0f, 1.0f );
		folder->maxRotation = calcFolderMaxRotation( folder );
		swipeComp->SetAccumulatedRotation( folder->maxRotation * positionRatio );

		// Update the scroll bar on new element count
		VRMenuObject * scrollBarObject = menuManager.toObject( folder->scrollBarHandle );
		if ( scrollBarObject != NULL )
		{
			OvrScrollBarComponent * scrollBar = scrollBarObject->GetComponentByName< OvrScrollBarComponent >();
			if ( scrollBar != NULL )
			{
				scrollBar->updateScrollBar( menuManager, scrollBarObject, numPanels );
			}
		}
	}
}

void OvrFolderBrowser::updateFolderTitle( const FolderView * folder )
{
	if ( folder != NULL )
	{
		const int numPanels = folder->panels.sizeInt();

		VString folderTitle = folder->localizedName;
		VRMenuObject * folderTitleObject = m_app->GetVRMenuMgr().toObject( folder->titleHandle );
		OVR_ASSERT( folderTitleObject != NULL );
        folderTitleObject->setText(folderTitle);

		VRMenuObjectFlags_t flags = folderTitleObject->flags();
		if ( numPanels > 0 )
		{
			flags &= ~VRMenuObjectFlags_t( VRMENUOBJECT_DONT_RENDER );
			flags &= ~VRMenuObjectFlags_t( VRMENUOBJECT_DONT_HIT_ALL );
		}
		else
		{
			flags |= VRMenuObjectFlags_t( VRMENUOBJECT_DONT_RENDER );
			flags |= VRMenuObjectFlags_t( VRMENUOBJECT_DONT_HIT_ALL );
		}

		folderTitleObject->setFlags( flags );
	}
}

void * OvrFolderBrowser::ThumbnailThread( void * v )
{
	OvrFolderBrowser * folderBrowser = ( OvrFolderBrowser * )v;

	int result = pthread_setname_np( pthread_self(), "FolderBrowser" );
	if ( result != 0 )
	{
		WARN( "FolderBrowser: pthread_setname_np failed %s", strerror( result ) );
	}

	sched_param sparam;
    sparam.sched_priority = VThread::GetOSPriority(VThread::BelowNormalPriority);

	int setSchedparamResult = pthread_setschedparam( pthread_self(), SCHED_NORMAL, &sparam );
	if ( setSchedparamResult != 0 )
	{
		WARN( "FolderBrowser: pthread_setschedparam failed %s", strerror( setSchedparamResult ) );
	}

	for ( ;; )
	{
		folderBrowser->m_backgroundCommands.SleepUntilMessage();
		const char * msg = folderBrowser->m_backgroundCommands.nextMessage();
		//LOG( "BackgroundCommands: %s", msg );

		if ( MatchesHead( "shutDown", msg ) )
		{
			LOG( "OvrFolderBrowser::ThumbnailThread shutting down" );
			folderBrowser->m_backgroundCommands.ClearMessages();
			break;
		}
		else if ( MatchesHead( "load ", msg ) )
		{
			int folderId = -1;
			int panelId = -1;

			sscanf( msg, "load %i %i", &folderId, &panelId );
			OVR_ASSERT( folderId >= 0 && panelId >= 0 );

			const char * fileName = strstr( msg, ":" ) + 1;

			const VString fullPath( fileName );

			int		width;
			int		height;
			unsigned char * data = folderBrowser->loadThumbnail( fileName, width, height );
			if ( data != NULL )
			{
				if ( folderBrowser->applyThumbAntialiasing( data, width, height ) )
				{
					folderBrowser->m_textureCommands.PostPrintf( "thumb %i %i %p %i %i",
						folderId, panelId, data, width, height );
				}
			}
			else
			{
				WARN( "Thumbnail load fail for: %s", fileName );
			}
		}
		else if ( MatchesHead( "httpThumb", msg ) )
		{
			int folderId = -1;
			int panelId = -1;
			char panoUrl[ 1024 ] = {};
			char cacheDestination[ 1024 ] = {};

			sscanf( msg, "httpThumb %s %s %d %d", panoUrl, cacheDestination, &folderId, &panelId );
			OVR_ASSERT( folderId >= 0 && panelId >= 0 );

			int		width;
			int		height;
			unsigned char * data = folderBrowser->retrieveRemoteThumbnail(
					panoUrl,
					cacheDestination,
					folderId,
					panelId,
					width,
					height );

			if ( data != NULL )
			{
				if ( folderBrowser->applyThumbAntialiasing( data, width, height ) )
				{
					folderBrowser->m_textureCommands.PostPrintf( "thumb %i %i %p %i %i",
						folderId, panelId, data, width, height );
				}
			}
			else
			{
				WARN( "Thumbnail download fail for: %s", panoUrl );
			}
		}
		else if ( MatchesHead( "processCreates ", msg ) )
		{
			Array< OvrCreateThumbCmd > * ThumbCreateAndLoadCommands;

			sscanf( msg, "processCreates %p", &ThumbCreateAndLoadCommands );

			for ( int i = 0; i < ThumbCreateAndLoadCommands->sizeInt(); ++i )
			{
				const OvrCreateThumbCmd & cmd = ThumbCreateAndLoadCommands->at( i );
				int	width = 0;
				int height = 0;
                unsigned char * data = folderBrowser->createAndCacheThumbnail( cmd.sourceImagePath.toCString(), cmd.thumbDestination.toCString(), width, height );

				if ( data != NULL )
				{
					// load to panel
					const unsigned ThumbWidth = folderBrowser->thumbWidth();
					const unsigned ThumbHeight = folderBrowser->thumbHeight();

					const int numBytes = width * height * 4;
					const int thumbPanelBytes = ThumbWidth * ThumbHeight * 4;
					if ( numBytes != thumbPanelBytes )
					{
						WARN( "Thumbnail image '%s' is the wrong size! Regenerate thumbnails!", cmd.thumbDestination.toCString() );
						free( data );
					}
					else
					{
						// Apply alpha from vrlib/res/raw to alpha channel for anti-aliasing
						for ( int i = 3; i < thumbPanelBytes; i += 4 )
						{
							data[ i ] = ThumbPanelBG[ i ];
						}

						int folderId;
						int panelId;

                        sscanf( cmd.loadCmd.toCString(), "load %i %i", &folderId, &panelId );

						folderBrowser->m_textureCommands.PostPrintf( "thumb %i %i %p %i %i",
							folderId, panelId, data, width, height );
					}
				}
			}
			ThumbCreateAndLoadCommands->clear();
		}
		else
		{
			LOG( "OvrFolderBrowser::ThumbnailThread received unhandled message: %s", msg );
			OVR_ASSERT( false );
		}

		free( ( void * )msg );
	}
	LOG( "OvrFolderBrowser::ThumbnailThread returned" );
	return NULL;
}

void OvrFolderBrowser::loadThumbnailToTexture( const char * thumbnailCommand )
{
	int folderId;
	int panelId;
	unsigned char * data;
	int width;
	int height;

	sscanf( thumbnailCommand, "thumb %i %i %p %i %i", &folderId, &panelId, &data, &width, &height );

	FolderView * folder = getFolderView( folderId );
	OVR_ASSERT( folder );

	Array<PanelView> * panels = &folder->panels;
	OVR_ASSERT( panels );

	PanelView * panel = NULL;

	// find panel using panelId
	const int numPanels = panels->sizeInt();
	for ( int index = 0; index < numPanels; ++index )
	{
		PanelView& currentPanel = panels->at( index );
		if ( currentPanel.id == panelId )
		{
			panel = &currentPanel;
			break;
		}
	}

	if ( panel == NULL ) // Panel not found as it was moved. Delete data and bail
	{
		free( data );
		return;
	}

	const int max = Alg::Max( width, height );

	// Grab the Panel from VRMenu
	VRMenuObject * panelObject = NULL;
	panelObject = m_app->GetVRMenuMgr().toObject( panel->handle );
	OVR_ASSERT( panelObject );

	panel->size[ 0 ] *= ( float )width / max;
	panel->size[ 1 ] *= ( float )height / max;

	GLuint texId = LoadRGBATextureFromMemory(
		data, width, height, true /* srgb */ ).texture;

	OVR_ASSERT( texId );

	panelObject->setSurfaceTextureTakeOwnership( 0, 0, SURFACE_TEXTURE_DIFFUSE,
		texId, panel->size[ 0 ], panel->size[ 1 ] );

	BuildTextureMipmaps( texId );
	MakeTextureTrilinear( texId );
	MakeTextureClamped( texId );

	free( data );
}

void OvrFolderBrowser::loadFolderPanels( const OvrMetaData & metaData, const OvrMetaData::Category & category, const int folderIndex, FolderView & folder,
		Array< VRMenuObjectParms const * >& outParms )
{
	// Build panels
	Array< const OvrMetaDatum * > categoryPanos;
    metaData.getMetaData( category, categoryPanos );
	const int numPanos = categoryPanos.sizeInt();
	LOG( "Building %d panels for %s", numPanos, category.categoryTag.toCString() );
	for ( int panoIndex = 0; panoIndex < numPanos; panoIndex++ )
	{
		addPanelToFolder( const_cast< OvrMetaDatum * const >( categoryPanos.at( panoIndex ) ), folderIndex, folder, outParms );
	}
}

//==============================================================
// OvrPanel_OnUp
// Forwards a touch up from Panel to Menu
// Holds pointer to the datum panel represents
class OvrPanel_OnUp : public VRMenuComponent_OnTouchUp
{
public:
	static const int TYPE_ID = 1107;

	OvrPanel_OnUp( VRMenu * menu, const OvrMetaDatum * panoData ) :
		VRMenuComponent_OnTouchUp(),
		Menu( menu ),
		Data( panoData )
	{
		OVR_ASSERT( Data );
	}

	void					SetData( const OvrMetaDatum * panoData )	{ Data = panoData; }

	virtual int				typeId() const					{ return TYPE_ID; }
	const OvrMetaDatum *	GetData() const						{ return Data; }

private:
	virtual eMsgStatus  onEventImpl( App * app, VrFrame const & vrFrame, OvrVRMenuMgr & menuMgr,
		VRMenuObject * self, VRMenuEvent const & event )
	{
		OVR_ASSERT( event.eventType == VRMENU_EVENT_TOUCH_UP );
		if ( OvrFolderBrowser * folderBrowser = static_cast< OvrFolderBrowser * >( Menu ) )
		{
			folderBrowser->onPanelUp( Data );
		}

		return MSG_STATUS_CONSUMED;
	}

private:
	VRMenu *				Menu;	// menu that holds the button
	const OvrMetaDatum *	Data;	// Payload for this button
};

void OvrFolderBrowser::addPanelToFolder( const OvrMetaDatum * panoData, const int folderIndex, FolderView & folder, Array< VRMenuObjectParms const * >& outParms )
{
	OVR_ASSERT( panoData );

	PanelView panel;
	const int panelIndex = folder.panels.sizeInt();
	panel.id = panelIndex;
	panel.size.x = m_panelWidth;
	panel.size.y = m_panelHeight;

	VString panelTitle = getPanelTitle( *panoData );
	// This is now done at the application left so that an app can localize any way it wishes
	//VrLocale::GetString(  AppPtr->GetVrJni(), AppPtr->GetJavaObject(), panoTitle, panoTitle, panelTitle );

	// Load a panel
	Array< VRMenuComponent* > panelComps;
	VRMenuId_t id( uniqueId.Get( 1 ) );

	//int  folderIndexShifted = folderIndex << 24;
	VRMenuId_t buttonId( uniqueId.Get( 1 ) );

	panelComps.append( new OvrPanel_OnUp( this, panoData ) );
	panelComps.append( new OvrDefaultComponent( Vector3f( 0.0f, 0.0f, 0.05f ), 1.05f, 0.25f, 0.25f, Vector4f( 0.f ) ) );

	// single-pass multitexture
	VRMenuSurfaceParms panelSurfParms( "panelSurface",
		m_defaultPanelTextureIds[ 0 ],
		m_thumbWidth,
		m_thumbHeight,
		SURFACE_TEXTURE_DIFFUSE,
		m_defaultPanelTextureIds[ 1 ],
		m_thumbWidth,
		m_thumbHeight,
		SURFACE_TEXTURE_DIFFUSE,
		0,
		0,
		0,
		SURFACE_TEXTURE_MAX );

	// Panel placement - based on index which determines position within the circumference
	const float factor = ( float )panelIndex / ( float )m_circumferencePanelSlots;
	Quatf rot( DOWN, ( Mathf::TwoPi * factor ) );
	Vector3f dir( FWD * rot );
	Posef panelPose( rot, dir * m_radius );
	Vector3f panelScale( 1.0f );

	// Title text placed below thumbnail
	const Posef textPose( Quatf(), Vector3f( 0.0f, -m_panelHeight * m_panelTextSpacingScale, 0.0f ) );

	const VRMenuFontParms fontParms( HORIZONTAL_CENTER, VERTICAL_CENTER, false, false, true, 0.525f, 0.45f, 0.5f );
	VRMenuObjectParms * p = new VRMenuObjectParms( VRMENU_BUTTON, panelComps,
        panelSurfParms, panelTitle.toCString(), panelPose, panelScale, textPose, Vector3f( 1.0f ), fontParms, id,
		VRMenuObjectFlags_t( VRMENUOBJECT_DONT_HIT_TEXT ), VRMenuObjectInitFlags_t( VRMENUOBJECT_INIT_FORCE_POSITION ) );

	outParms.append( p );
	folder.panels.append( panel );

	OVR_ASSERT( folderIndex < m_folders.sizeInt() );

	// Create or load thumbnail - request built up here to be processed ThumbnailThread
    const VString & panoUrl = this->thumbUrl( panoData );
    const VString thumbName = this->thumbName( panoUrl );
	VString finalThumb;
	char relativeThumbPath[ 1024 ];
    ToRelativePath( m_thumbSearchPaths, panoUrl.toCString(), relativeThumbPath, 1024 );

	char appCacheThumbPath[ 1024 ];
    OVR_sprintf( appCacheThumbPath, 1024, "%s%s", m_appCachePath.toCString(), this->thumbName( relativeThumbPath ).toCString() );

	// if this url doesn't exist locally
    if ( !FileExists( panoUrl ) )
	{
		// Check app cache to see if we already downloaded it
		if ( FileExists( appCacheThumbPath ) )
		{
			finalThumb = appCacheThumbPath;
		}
		else // download and cache it
		{
			m_backgroundCommands.PostPrintf( "httpThumb %s %s %d %d", panoUrl.toCString(), appCacheThumbPath, folderIndex, panel.id );
			return;
		}
	}

	if ( finalThumb.isEmpty() )
	{
		// Try search paths next to image for retail photos
        if ( !GetFullPath( m_thumbSearchPaths, thumbName.toCString(), finalThumb ) )
		{
			// Try app cache for cached user pano thumbs
			if ( FileExists( appCacheThumbPath ) )
			{
				finalThumb = appCacheThumbPath;
			}
			else
			{
				const VString altThumbPath = alternateThumbName( panoUrl );
                if ( altThumbPath.isEmpty() || !GetFullPath( m_thumbSearchPaths, altThumbPath.toCString(), finalThumb ) )
				{
                    int pathLen = (int) panoUrl.length();
					if ( pathLen > 2 && OVR_stricmp( panoUrl.toCString() + pathLen - 2, ".x" ) == 0 )
					{
						WARN( "Thumbnails cannot be generated from encrypted images." );
						return; // No thumb & can't create
					}

					// Create and write out thumbnail to app cache
					finalThumb = appCacheThumbPath;

					// Create the path if necessary
					MakePath( finalThumb, S_IRUSR | S_IWUSR );

					if ( HasPermission( finalThumb, W_OK ) )
					{
						if ( FileExists( panoData->url ) )
						{
							OvrCreateThumbCmd createCmd;
							createCmd.sourceImagePath = panoUrl;
							createCmd.thumbDestination = finalThumb;
							char loadCmd[ 1024 ];
							OVR_sprintf( loadCmd, 1024, "load %i %i:%s", folderIndex, panel.id, finalThumb.toCString() );
							createCmd.loadCmd = loadCmd;
							m_thumbCreateAndLoadCommands.append( createCmd );
						}
						return;
					}
				}
			}
		}
	}

	char cmd[ 1024 ];
	OVR_sprintf( cmd, 1024, "load %i %i:%s", folderIndex, panel.id, finalThumb.toCString() );
	//LOG( "Thumb cmd: %s", cmd );
	m_backgroundCommands.PostString( cmd );
}

bool OvrFolderBrowser::applyThumbAntialiasing( unsigned char * inOutBuffer, int width, int height ) const
{
	if ( inOutBuffer != NULL )
	{
		if ( ThumbPanelBG != NULL )
		{
			const unsigned ThumbWidth = thumbWidth();
			const unsigned ThumbHeight = thumbHeight();

			const int numBytes = width * height * 4;
			const int thumbPanelBytes = ThumbWidth * ThumbHeight * 4;
			if ( numBytes != thumbPanelBytes )
			{
				WARN( "OvrFolderBrowser::ApplyAA - Thumbnail image '%s' is the wrong size!" );
			}
			else
			{
				// Apply alpha from vrlib/res/raw to alpha channel for anti-aliasing
				for ( int i = 3; i < thumbPanelBytes; i += 4 )
				{
					inOutBuffer[ i ] = ThumbPanelBG[ i ];
				}
				return true;
			}
		}
	}
	return false;
}

const OvrFolderBrowser::FolderView * OvrFolderBrowser::getFolderView( int index ) const
{
	if ( m_folders.isEmpty() )
	{
		return NULL;
	}

	if ( index < 0 || index >= m_folders.sizeInt() )
	{
		return NULL;
	}

	return m_folders.at( index );
}

OvrFolderBrowser::FolderView * OvrFolderBrowser::getFolderView( int index )
{
	if ( m_folders.isEmpty() )
	{
		return NULL;
	}

	if ( index < 0 || index >= m_folders.sizeInt() )
	{
		return NULL;
	}

	return m_folders.at( index );
}

OvrFolderBrowser::FolderView * OvrFolderBrowser::getFolderView( const VString & categoryTag )
{
	if ( m_folders.isEmpty() )
	{
		return NULL;
	}

	for ( int i = 0; i < m_folders.sizeInt(); ++i )
	{
		FolderView * currentFolder = m_folders.at( i );
		if ( currentFolder->categoryTag == categoryTag )
		{
			return currentFolder;
		}
	}

	return NULL;
}

bool OvrFolderBrowser::rotateCategory( const CategoryDirection direction )
{
	OvrFolderSwipeComponent * swipeComp = swipeComponentForActiveFolder();
	return swipeComp->Rotate( direction );
}

void OvrFolderBrowser::setCategoryRotation( const int folderIndex, const int panelIndex )
{
	OVR_ASSERT( folderIndex >= 0 && folderIndex < m_folders.sizeInt() );
	const FolderView * folder = getFolderView( folderIndex );
	if ( folder != NULL )
	{
		VRMenuObject * swipe = m_app->GetVRMenuMgr().toObject( folder->swipeHandle );
		OVR_ASSERT( swipe );

		OvrFolderSwipeComponent * swipeComp = swipe->GetComponentById< OvrFolderSwipeComponent >();
		OVR_ASSERT( swipeComp );

		swipeComp->SetRotationByIndex( panelIndex );
	}
}

void OvrFolderBrowser::onPanelUp( const OvrMetaDatum * data )
{
	if ( m_allowPanelTouchUp )
	{
		onPanelActivated( data );
	}
}

const OvrMetaDatum * OvrFolderBrowser::nextFileInDirectory( const int step )
{
	FolderView * folder = getFolderView( activeFolderIndex() );
	if ( folder == NULL )
	{
		return NULL;
	}

	const int numPanels = folder->panels.sizeInt();

	if ( numPanels == 0 )
	{
		return NULL;
	}

	OvrFolderSwipeComponent * swipeComp = swipeComponentForActiveFolder();
	OVR_ASSERT( swipeComp );

	int nextPanelIndex = swipeComp->CurrentPanelIndex() + step;
	if ( nextPanelIndex >= numPanels )
	{
		nextPanelIndex = 0;
	}
	else if ( nextPanelIndex < 0 )
	{
		nextPanelIndex = numPanels - 1;
	}

	PanelView & panel = folder->panels.at( nextPanelIndex );
	VRMenuObject * panelObject = m_app->GetVRMenuMgr().toObject( panel.handle );
	OVR_ASSERT( panelObject );

	OvrPanel_OnUp * panelUpComp = panelObject->GetComponentById<OvrPanel_OnUp>();
	OVR_ASSERT( panelUpComp );

	const OvrMetaDatum * datum = panelUpComp->GetData();
	OVR_ASSERT( datum );

	swipeComp->SetRotationByIndex( nextPanelIndex );

	return datum;
}

float OvrFolderBrowser::calcFolderMaxRotation( const FolderView * folder ) const
{
	if ( folder == NULL )
	{
		OVR_ASSERT( false );
		return 0.0f;
	}
	int numPanels = Alg::Clamp( folder->panels.sizeInt() - 1, 0, INT_MAX );
	return static_cast< float >( numPanels );
}

void OvrFolderBrowser::setWrapIndicatorVisible( FolderView& folder, const bool visible )
{
	if ( folder.wrapIndicatorHandle.IsValid() )
	{
		VRMenuObject * wrapIndicatorObject = m_app->GetVRMenuMgr().toObject( folder.wrapIndicatorHandle );
		if ( wrapIndicatorObject )
		{
			wrapIndicatorObject->setVisible( visible );
		}
	}
}

OvrFolderSwipeComponent * OvrFolderBrowser::swipeComponentForActiveFolder()
{
	const FolderView * folder = getFolderView( activeFolderIndex() );
	if ( folder == NULL )
	{
		OVR_ASSERT( false );
		return NULL;
	}

	VRMenuObject * swipeObject = m_app->GetVRMenuMgr().toObject( folder->swipeHandle );
	OVR_ASSERT( swipeObject );

	OvrFolderSwipeComponent * swipeComp = swipeObject->GetComponentById<OvrFolderSwipeComponent>();
	OVR_ASSERT( swipeComp );

	return swipeComp;
}

bool OvrFolderBrowser::gazingAtMenu() const
{
	if ( focusedHandle().IsValid() )
	{
		const Matrix4f & view = m_app->GetLastViewMatrix();
		Vector3f viewForwardFlat( view.M[ 2 ][ 0 ], 0.0f, view.M[ 2 ][ 2 ] );
		viewForwardFlat.Normalize();

		static const float cosHalf = cos( m_visiblePanelsArcAngle );
		const float dot = viewForwardFlat.Dot( -FWD * menuPose().Orientation );

		if ( dot >= cosHalf )
		{
			return true;
		}
	}
	return false;
}

int OvrFolderBrowser::activeFolderIndex() const
{
	VRMenuObject * rootObject = m_app->GetVRMenuMgr().toObject( rootHandle() );
	OVR_ASSERT( rootObject );

	OvrFolderBrowserRootComponent * rootComp = rootObject->GetComponentById<OvrFolderBrowserRootComponent>();
	OVR_ASSERT( rootComp );

	return rootComp->GetCurrentIndex( rootObject, m_app->GetVRMenuMgr() );
}

void OvrFolderBrowser::setActiveFolder( int folderIdx )
{
	VRMenuObject * rootObject = m_app->GetVRMenuMgr().toObject( rootHandle() );
	OVR_ASSERT( rootObject );

	OvrFolderBrowserRootComponent * rootComp = rootObject->GetComponentById<OvrFolderBrowserRootComponent>();
	OVR_ASSERT( rootComp );

	rootComp->SetActiveFolder( folderIdx );
}

void OvrFolderBrowser::touchDown()
{
	m_isTouchDownPosistionTracked = false;
	m_touchDirectionLocked = NO_LOCK;
}

void OvrFolderBrowser::touchUp()
{
	m_isTouchDownPosistionTracked = false;
	m_touchDirectionLocked = NO_LOCK;
}

void OvrFolderBrowser::touchRelative( Vector3f touchPos )
{
	if ( !m_isTouchDownPosistionTracked )
	{
		m_isTouchDownPosistionTracked = true;
		m_touchDownPosistion = touchPos;
	}

	if ( m_touchDirectionLocked == NO_LOCK )
	{
		float xAbsChange = fabsf( m_touchDownPosistion.x - touchPos.x );
		float yAbsChange = fabsf( m_touchDownPosistion.y - touchPos.y );

		if ( xAbsChange >= SCROLL_DIRECTION_DECIDING_DISTANCE || yAbsChange >= SCROLL_DIRECTION_DECIDING_DISTANCE )
		{
			if ( xAbsChange >= yAbsChange )
			{
				m_touchDirectionLocked = HORIZONTAL_LOCK;
			}
			else
			{
				m_touchDirectionLocked = VERTICAL_LOCK;
			}
		}
	}
}

} // namespace NervGear
