#pragma once

#include <VMainActivity.h>
#include <VPath.h>

#include "Fader.h"
#include "ModelView.h"

NV_NAMESPACE_BEGIN

enum Action
{
	ACT_NONE,
	ACT_LAUNCHER,
	ACT_STILLS,
	ACT_VIDEOS,
};

class PanoVideo : public NervGear::VMainActivity
{
public:

	enum OvrMenuState
	{
		MENU_NONE,
		MENU_BROWSER,
		MENU_VIDEO_LOADING,
		MENU_VIDEO_READY,
		MENU_VIDEO_PLAYING,
		NUM_MENU_STATES
	};

    PanoVideo(JNIEnv *jni, jclass activityClass, jobject activityObject);
    ~PanoVideo();

	virtual void		init(const VString &fromPackage, const VString &launchIntentJSON, const VString &launchIntentURI );
	virtual void		shutdown();
	virtual void		configureVrMode(VKernel* kernel);
    virtual VR4Matrixf 	drawEyeView( const int eye, const float fovDegrees );
    virtual VR4Matrixf 	onNewFrame( VFrame vrFrame );
    void command(const VEvent &event) override;
	virtual bool 		onKeyEvent( const int keyCode, const KeyState::eKeyEventType eventType );

	void 				StopVideo();
	void 				PauseVideo( bool const force );
	void 				ResumeVideo();
	bool				IsVideoPlaying() const;

    void 				StartVideo(const VString &url);
	void				SeekTo( const int seekPos );

	void 				CloseSwipeView( const VFrame &vrFrame );
	void 				OpenSwipeView( const VFrame &vrFrame, bool centerList );
    VR4Matrixf			TexmForVideo( const int eye );
    VR4Matrixf			TexmForBackground( const int eye );

	void				SetMenuState( const OvrMenuState state );
	OvrMenuState		GetCurrentState() const				{ return  MenuState; }

	void				SetFrameAvailable( bool const a ) { FrameAvailable = a; }

    const VPath &videoUrl() { return m_videoUrl; }
	float				GetFadeLevel()		{ return CurrentFadeLevel; }

private:
	// shared vars
	jclass				MainActivityClass;	// need to look up from main thread
	VGlGeometry			Globe;
    OvrSceneView		Scene;
	bool				VideoWasPlayingWhenPaused;	// state of video when main activity was paused

	// panorama vars
	GLuint				BackgroundTexId;
	VGlShader			PanoramaProgram;
	VGlShader			FadedPanoramaProgram;
	VGlShader			SingleColorTextureProgram;

    VPath m_videoUrl;
	OvrMenuState		MenuState;
	SineFader			Fader;
	const float			FadeOutRate;
	const float			VideoMenuVisibleTime;
	float				CurrentFadeRate;
	float				CurrentFadeLevel;
	float				VideoMenuTimeLeft;

	bool				UseSrgb;

    // video vars
	SurfaceTexture	* 	MovieTexture;

	// Set when MediaPlayer knows what the stream size is.
	// current is the aspect size, texture may be twice as wide or high for 3D content.
	int					CurrentVideoWidth;	// set to 0 when a new movie is started, don't render until non-0
	int					CurrentVideoHeight;

	int					BackgroundWidth;
	int					BackgroundHeight;

	bool				FrameAvailable;

private:
	void				OnResume();
	void				OnPause();
};

NV_NAMESPACE_END