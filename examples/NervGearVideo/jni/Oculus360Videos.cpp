/************************************************************************************

Filename    :   Oculus360Videos.cpp
Content     :
Created     :
Authors     :

Copyright   :   Copyright 2014 Oculus VR, LLC. All Rights reserved.

This source code is licensed under the BSD-style license found in the
LICENSE file in the Oculus360Videos/ directory. An additional grant
of patent rights can be found in the PATENTS file in the same directory.

*************************************************************************************/

#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <jni.h>
#include <android/keycodes.h>

#include <Alg.h>
#include <VPath.h>
#include <VApkFile.h>

#include "TypesafeNumber.h"
#include "VArray.h"
#include "VString.h"
#include "api/VGlOperation.h"
#include "SurfaceTexture.h"

#include "GlTexture.h"
#include "BitmapFont.h"
#include "GazeCursor.h"
#include "App.h"
#include "SwipeView.h"
#include "Oculus360Videos.h"
#include "gui/GuiSys.h"
#include "ImageData.h"
#include "gui/Fader.h"
#include "3rdParty/stb/stb_image.h"
#include "3rdParty/stb/stb_image_write.h"
#include "VDir.h"
#include "VideoBrowser.h"
#include "VideoMenu.h"
#include "VrLocale.h"
#include "VStandardPath.h"

#include "VideosMetaData.h"

static bool	RetailMode = false;

static const char * videosDirectory = "Oculus/360Videos/";
static const char * videosLabel = "@string/app_name";
static const float	FadeOutTime = 0.25f;
static const float	FadeOverTime = 1.0f;

namespace NervGear
{

extern "C" {

static jclass	GlobalActivityClass;
void Java_com_vrseen_nervgear_video_MainActivity_nativeSetAppInterface( JNIEnv *jni, jclass clazz, jobject activity,
		jstring fromPackageName, jstring commandString, jstring uriString )
{
	// This is called by the java UI thread.

	GlobalActivityClass = (jclass)jni->NewGlobalRef( clazz );

	LOG( "nativeSetAppInterface");
    (new Oculus360Videos(jni, clazz, activity))->onCreate(fromPackageName, commandString, uriString );
}

void Java_com_vrseen_nervgear_video_MainActivity_nativeFrameAvailable(JNIEnv *, jclass)
{
    Oculus360Videos * panoVids = ( Oculus360Videos * ) vApp->appInterface();
	panoVids->SetFrameAvailable( true );
}

jobject Java_com_vrseen_nervgear_video_MainActivity_nativePrepareNewVideo(JNIEnv *, jclass)
{

	// set up a message queue to get the return message
	// TODO: make a class that encapsulates this work
	VEventLoop	result( 1 );
    vApp->eventLoop().post("newVideo", &result);

	result.wait();
    VEvent event = result.next();
	jobject	texobj = nullptr;
    if (event.name == "surfaceTexture") {
        texobj = static_cast<jobject>(event.data.toPointer());
    }

	return texobj;
}

void Java_com_vrseen_nervgear_video_MainActivity_nativeSetVideoSize(JNIEnv *, jclass, int width, int height)
{
	LOG( "nativeSetVideoSizes: width=%i height=%i", width, height );
    VVariantArray args;
    args << width << height;
    vApp->eventLoop().post("video", std::move(args));
}

void Java_com_vrseen_nervgear_video_MainActivity_nativeVideoCompletion(JNIEnv *, jclass)
{
	LOG( "nativeVideoCompletion" );
    vApp->eventLoop().post( "completion" );
}

void Java_com_vrseen_nervgear_video_MainActivity_nativeVideoStartError(JNIEnv *, jclass)
{
	LOG( "nativeVideoStartError" );
    vApp->eventLoop().post( "startError" );
}

} // extern "C"



Oculus360Videos::Oculus360Videos(JNIEnv *jni, jclass activityClass, jobject activityObject)
    : VMainActivity(jni, activityClass, activityObject)
    , MainActivityClass( GlobalActivityClass )
	, BackgroundScene( NULL )
	, VideoWasPlayingWhenPaused( false )
	, BackgroundTexId( 0 )
	, MetaData( NULL )
	, Browser( NULL )
	, VideoMenu( NULL )
	, ActiveVideo( NULL )
	, MenuState( MENU_NONE )
	, Fader( 1.0f )
	, FadeOutRate( 1.0f / 0.5f )
	, VideoMenuVisibleTime( 5.0f )
	, CurrentFadeRate( FadeOutRate )
	, CurrentFadeLevel( 1.0f )
	, VideoMenuTimeLeft( 0.0f )
	, UseSrgb( false )
	, MovieTexture( NULL )
	, CurrentVideoWidth( 0 )
	, CurrentVideoHeight( 480 )
	, BackgroundWidth( 0 )
	, BackgroundHeight( 0 )
	, FrameAvailable( false )
{
}

Oculus360Videos::~Oculus360Videos()
{
}

//============================================================================================
void Oculus360Videos::init(const VString &fromPackage, const VString &launchIntentJSON, const VString &launchIntentURI)
{
	// This is called by the VR thread, not the java UI thread.
	LOG( "--------------- Oculus360Videos OneTimeInit ---------------" );

//	RetailMode = FileExists( "/sdcard/RetailMedia" );
	VDir vdir;
	RetailMode = vdir.exists( "/sdcard/RetailMedia" );

	vApp->vrParms().colorFormat = COLOR_8888;
	vApp->vrParms().depthFormat = DEPTH_16;
	vApp->vrParms().multisamples = 2;

	PanoramaProgram.initShader(
		"uniform highp mat4 Mvpm;\n"
		"uniform highp mat4 Texm;\n"
		"attribute vec4 Position;\n"
		"attribute vec2 TexCoord;\n"
		"varying  highp vec2 oTexCoord;\n"
		"void main()\n"
		"{\n"
		"   gl_Position = Mvpm * Position;\n"
		"   oTexCoord = vec2( Texm * vec4( TexCoord, 0, 1 ) );\n"
		"}\n"
		,
		"#extension GL_OES_EGL_image_external : require\n"
		"uniform samplerExternalOES Texture0;\n"
		"uniform lowp vec4 UniformColor;\n"
		"uniform lowp vec4 ColorBias;\n"
		"varying highp vec2 oTexCoord;\n"
		"void main()\n"
		"{\n"
		"	gl_FragColor = ColorBias + UniformColor * texture2D( Texture0, oTexCoord );\n"
		"}\n"
		);

	FadedPanoramaProgram.initShader(
		"uniform highp mat4 Mvpm;\n"
		"uniform highp mat4 Texm;\n"
		"attribute vec4 Position;\n"
		"attribute vec2 TexCoord;\n"
		"varying  highp vec2 oTexCoord;\n"
		"void main()\n"
		"{\n"
		"   gl_Position = Mvpm * Position;\n"
		"   oTexCoord = vec2( Texm * vec4( TexCoord, 0, 1 ) );\n"
		"}\n"
		,
		"#extension GL_OES_EGL_image_external : require\n"
		"uniform samplerExternalOES Texture0;\n"
		"uniform sampler2D Texture1;\n"
		"uniform lowp vec4 UniformColor;\n"
		"varying highp vec2 oTexCoord;\n"
		"void main()\n"
		"{\n"
		"	lowp vec4 staticColor = texture2D( Texture1, oTexCoord );\n"
		"	lowp vec4 movieColor = texture2D( Texture0, oTexCoord );\n"
		"	gl_FragColor = UniformColor * mix( movieColor, staticColor, staticColor.w );\n"
		"}\n"
		);

	SingleColorTextureProgram.initShader(
		"uniform highp mat4 Mvpm;\n"
		"attribute highp vec4 Position;\n"
		"attribute highp vec2 TexCoord;\n"
		"varying highp vec2 oTexCoord;\n"
		"void main()\n"
		"{\n"
		"   gl_Position = Mvpm * Position;\n"
		"   oTexCoord = TexCoord;\n"
		"}\n"
		,
		"uniform sampler2D Texture0;\n"
		"uniform lowp vec4 UniformColor;\n"
		"varying highp vec2 oTexCoord;\n"
		"void main()\n"
		"{\n"
		"   gl_FragColor = UniformColor * texture2D( Texture0, oTexCoord );\n"
		"}\n"
		);

	const char *launchPano = NULL;
	if ( ( NULL != launchPano ) && launchPano[ 0 ] )
	{
#if 0
		unsigned char * texture;
		texture = stbi_load( launchPano, &BackgroundWidth, &BackgroundHeight, NULL, 4 );
		if ( texture )
		{
			for ( int i = 0; i < BackgroundWidth*BackgroundHeight; i++ )
			{
				texture[ i * 4 + 3 ] = 255;
			}

			// cut a hole in the middle
			for ( int i = 0; i < BackgroundWidth / 2; i++ )
			for ( int j = 0; j < BackgroundHeight / 4; j++ )
			{
				texture[ ( ( BackgroundHeight / 8 + j )*BackgroundWidth + BackgroundWidth / 4 + 256 + i ) * 4 + 3 ] = 0;
				texture[ ( ( 5 * BackgroundHeight / 8 + j )*BackgroundWidth + BackgroundWidth / 4 + 256 + i ) * 4 + 3 ] = 0;
			}

			BackgroundTexId = LoadRGBATextureFromMemory( texture, BackgroundWidth, BackgroundHeight, false );
		}
#else
		BackgroundTexId = LoadTextureFromBuffer( launchPano, MemBufferFile( launchPano ),
			TextureFlags_t( TEXTUREFLAG_NO_DEFAULT ) | TEXTUREFLAG_USE_SRGB, BackgroundWidth, BackgroundHeight );
#endif
	}

	// always fall back to valid background
	if ( BackgroundTexId == 0 )
	{
		BackgroundTexId = LoadTextureFromApplicationPackage( "assets/background.jpg",
			TextureFlags_t( TEXTUREFLAG_USE_SRGB ), BackgroundWidth, BackgroundHeight );
	}

	LOG( "Creating Globe" );
	Globe = VGlGeometryFactory::CreateGlobe();

	// Stay exactly at the origin, so the panorama globe is equidistant
	// Don't clear the head model neck length, or swipe view panels feel wrong.
	VrViewParms viewParms = vApp->vrViewParms();
	viewParms.EyeHeight = 0.0f;
	vApp->setVrViewParms( viewParms );

	// Optimize for 16 bit depth in a modest theater size
	Scene.Znear = 0.1f;
	Scene.Zfar = 200.0f;
	MaterialParms materialParms;
	materialParms.UseSrgbTextureFormats = ( vApp->vrParms().colorFormat == COLOR_8888_sRGB );


    const VApkFile &apk = VApkFile::CurrentApkFile();
    void *buffer = nullptr;
    uint length = 0;
    apk.read("assets/stars.ovrscene", buffer, length);
    BackgroundScene = LoadModelFileFromMemory("assets/stars.ovrscene", buffer, length, Scene.GetDefaultGLPrograms(), materialParms);

	Scene.SetWorldModel( *BackgroundScene );

	// Load up meta data from videos directory
	MetaData = new OvrVideosMetaData();
	if ( MetaData == NULL )
	{
		FAIL( "Oculus360Photos::OneTimeInit failed to create MetaData" );
	}

    const VStandardPath &storagePaths = vApp->storagePaths();
    storagePaths.PushBackSearchPathIfValid( VStandardPath::SecondaryExternalStorage, VStandardPath::RootFolder, "RetailMedia/", SearchPaths );
    storagePaths.PushBackSearchPathIfValid( VStandardPath::SecondaryExternalStorage, VStandardPath::RootFolder, "", SearchPaths );
    storagePaths.PushBackSearchPathIfValid( VStandardPath::PrimaryExternalStorage, VStandardPath::RootFolder, "RetailMedia/", SearchPaths );
    storagePaths.PushBackSearchPathIfValid( VStandardPath::PrimaryExternalStorage, VStandardPath::RootFolder, "", SearchPaths );

	OvrMetaDataFileExtensions fileExtensions;
	fileExtensions.goodExtensions.append( ".mp4" );
	fileExtensions.goodExtensions.append( ".m4v" );
	fileExtensions.goodExtensions.append( ".3gp" );
	fileExtensions.goodExtensions.append( ".3g2" );
	fileExtensions.goodExtensions.append( ".ts" );
	fileExtensions.goodExtensions.append( ".webm" );
	fileExtensions.goodExtensions.append( ".mkv" );
	fileExtensions.goodExtensions.append( ".wmv" );
	fileExtensions.goodExtensions.append( ".asf" );
	fileExtensions.goodExtensions.append( ".avi" );
	fileExtensions.goodExtensions.append( ".flv" );

	MetaData->initFromDirectory( videosDirectory, SearchPaths, fileExtensions );

	VString localizedAppName;
	VrLocale::GetString( vApp->vrJni(), vApp->javaObject(), videosLabel, videosLabel, localizedAppName );
    MetaData->renameCategory(VPath(videosDirectory).baseName(), localizedAppName);

	// Start building the VideoMenu
	VideoMenu = ( OvrVideoMenu * )vApp->guiSys().getMenu( OvrVideoMenu::MENU_NAME );
	if ( VideoMenu == NULL )
	{
		VideoMenu = OvrVideoMenu::Create(
			vApp, this, vApp->vrMenuMgr(), vApp->defaultFont(), *MetaData, 1.0f, 2.0f );
		OVR_ASSERT( VideoMenu );

		vApp->guiSys().addMenu( VideoMenu );
	}

	VideoMenu->setFlags( VRMenuFlags_t( VRMENU_FLAG_PLACE_ON_HORIZON ) | VRMENU_FLAG_SHORT_PRESS_HANDLED_BY_APP );

	// Start building the FolderView
	Browser = ( VideoBrowser * )vApp->guiSys().getMenu( OvrFolderBrowser::MENU_NAME );
	if ( Browser == NULL )
	{
		Browser = VideoBrowser::Create(
			vApp,
			*MetaData,
			256, 20.0f,
			256, 200.0f,
			7,
			5.4f );
		OVR_ASSERT( Browser );

		vApp->guiSys().addMenu( Browser );
	}

	Browser->setFlags( VRMenuFlags_t( VRMENU_FLAG_PLACE_ON_HORIZON ) | VRMENU_FLAG_BACK_KEY_EXITS_APP );
	Browser->setFolderTitleSpacingScale( 0.37f );
	Browser->setPanelTextSpacingScale( 0.34f );
	Browser->setScrollBarSpacingScale( 0.9f );
	Browser->setScrollBarRadiusScale( 1.0f );

	Browser->oneTimeInit();
	Browser->buildDirtyMenu( *MetaData );

	SetMenuState( MENU_BROWSER );
}

void Oculus360Videos::shutdown()
{
	// This is called by the VR thread, not the java UI thread.
	LOG( "--------------- Oculus360Videos OneTimeShutdown ---------------" );

	if ( BackgroundScene != NULL )
	{
		delete BackgroundScene;
		BackgroundScene = NULL;
	}

	if ( MetaData != NULL )
	{
		delete MetaData;
		MetaData = NULL;
	}

	Globe.Free();

	FreeTexture( BackgroundTexId );

	if ( MovieTexture != NULL )
	{
		delete MovieTexture;
		MovieTexture = NULL;
	}

	PanoramaProgram.destroy();
	 FadedPanoramaProgram.destroy();
	 SingleColorTextureProgram.destroy();
}

void Oculus360Videos::configureVrMode( ovrModeParms & modeParms )
{
	// We need very little CPU for pano browsing, but a fair amount of GPU.
	// The CPU clock should ramp up above the minimum when necessary.
	LOG( "ConfigureClocks: Oculus360Videos only needs minimal clocks" );
	modeParms.CpuLevel = 1;
	modeParms.GpuLevel = 0;

	// When the app is throttled, go to the platform UI and display a
	// dismissable warning. On return to the app, force 30hz timewarp.
	modeParms.AllowPowerSave = true;

	// All geometry is blended, so save power with no MSAA
	vApp->vrParms().multisamples = 1;
}

bool Oculus360Videos::onKeyEvent( const int keyCode, const KeyState::eKeyEventType eventType )
{
	if ( ( ( keyCode == AKEYCODE_BACK ) && ( eventType == KeyState::KEY_EVENT_SHORT_PRESS ) ) ||
		( ( keyCode == KEYCODE_B ) && ( eventType == KeyState::KEY_EVENT_UP ) ) )
	{
		if ( MenuState == MENU_VIDEO_LOADING )
		{
			return true;
		}

		if ( ActiveVideo )
		{
			SetMenuState( MENU_BROWSER );
			return true;	// consume the key
		}
		// if no video is playing (either paused or stopped), let VrLib handle the back key
	}
	else if ( keyCode == AKEYCODE_P && eventType == KeyState::KEY_EVENT_DOWN )
	{
		PauseVideo( true );
	}

	return false;
}

void Oculus360Videos::command(const VEvent &event )
{
	// Always include the space in MatchesHead to prevent problems
	// with commands with matching prefixes.

    if (event.name == "newVideo") {
		delete MovieTexture;
		MovieTexture = new SurfaceTexture( vApp->vrJni() );
		LOG( "RC_NEW_VIDEO texId %i", MovieTexture->textureId );

        VEventLoop *receiver = static_cast<VEventLoop *>(event.data.toPointer());
        receiver->post("surfaceTexture", MovieTexture->javaObject);

		// don't draw the screen until we have the new size
		CurrentVideoWidth = 0;
		return;

    } else if (event.name == "completion") {// video complete, return to menu
		SetMenuState( MENU_BROWSER );
		return;

    } else if (event.name == "video") {
        CurrentVideoWidth = event.data.at(0).toInt();
        CurrentVideoHeight = event.data.at(1).toInt();

		if ( MenuState != MENU_VIDEO_PLAYING ) // If video is already being played dont change the state to video ready
		{
			SetMenuState( MENU_VIDEO_READY );
		}

		return;
    } else if (event.name == "resume") {
		OnResume();
		return;	// allow VrLib to handle it, too
    } else if (event.name == "pause") {
		OnPause();
		return;	// allow VrLib to handle it, too
    } else if (event.name == "startError") {
		// FIXME: this needs to do some parameter magic to fix xliff tags
		VString message;
		VrLocale::GetString( vApp->vrJni(), vApp->javaObject(), "@string/playback_failed", "@string/playback_failed", message );
        VString fileName = VPath(ActiveVideo->url).fileName();
        message = VrLocale::GetXliffFormattedString( message, fileName.toCString() );
		BitmapFont & font = vApp->defaultFont();
		font.WordWrapText( message, 1.0f );
        vApp->showInfoText( 4.5f, message.toCString() );
		SetMenuState( MENU_BROWSER );
		return;
	}

}

VR4Matrixf	Oculus360Videos::TexmForVideo( const int eye )
{
	if ( strstr( VideoName.toCString(), "_TB.mp4" ) )
	{	// top / bottom stereo panorama
		return eye ?
            VR4Matrixf( 1, 0, 0, 0,
			0, 0.5, 0, 0.5,
			0, 0, 1, 0,
			0, 0, 0, 1 )
			:
            VR4Matrixf( 1, 0, 0, 0,
			0, 0.5, 0, 0,
			0, 0, 1, 0,
			0, 0, 0, 1 );
	}
	if ( strstr( VideoName.toCString(), "_BT.mp4" ) )
	{	// top / bottom stereo panorama
		return ( !eye ) ?
            VR4Matrixf( 1, 0, 0, 0,
			0, 0.5, 0, 0.5,
			0, 0, 1, 0,
			0, 0, 0, 1 )
			:
            VR4Matrixf( 1, 0, 0, 0,
			0, 0.5, 0, 0,
			0, 0, 1, 0,
			0, 0, 0, 1 );
	}
	if ( strstr( VideoName.toCString(), "_LR.mp4" ) )
	{	// left / right stereo panorama
		return eye ?
            VR4Matrixf( 0.5, 0, 0, 0,
			0, 1, 0, 0,
			0, 0, 1, 0,
			0, 0, 0, 1 )
			:
            VR4Matrixf( 0.5, 0, 0, 0.5,
			0, 1, 0, 0,
			0, 0, 1, 0,
			0, 0, 0, 1 );
	}
	if ( strstr( VideoName.toCString(), "_RL.mp4" ) )
	{	// left / right stereo panorama
		return ( !eye ) ?
            VR4Matrixf( 0.5, 0, 0, 0,
			0, 1, 0, 0,
			0, 0, 1, 0,
			0, 0, 0, 1 )
			:
            VR4Matrixf( 0.5, 0, 0, 0.5,
			0, 1, 0, 0,
			0, 0, 1, 0,
			0, 0, 0, 1 );
	}

	// default to top / bottom stereo
	if ( CurrentVideoWidth == CurrentVideoHeight )
	{	// top / bottom stereo panorama
		return eye ?
            VR4Matrixf( 1, 0, 0, 0,
			0, 0.5, 0, 0.5,
			0, 0, 1, 0,
			0, 0, 0, 1 )
			:
            VR4Matrixf( 1, 0, 0, 0,
			0, 0.5, 0, 0,
			0, 0, 1, 0,
			0, 0, 0, 1 );

		// We may want to support swapping top/bottom
	}
    return VR4Matrixf::Identity();
}

VR4Matrixf	Oculus360Videos::TexmForBackground( const int eye )
{
	if ( BackgroundWidth == BackgroundHeight )
	{	// top / bottom stereo panorama
		return eye ?
            VR4Matrixf(
			1, 0, 0, 0,
			0, 0.5, 0, 0.5,
			0, 0, 1, 0,
			0, 0, 0, 1 )
			:
            VR4Matrixf(
			1, 0, 0, 0,
			0, 0.5, 0, 0,
			0, 0, 1, 0,
			0, 0, 0, 1 );

		// We may want to support swapping top/bottom
	}
    return VR4Matrixf::Identity();
}

VR4Matrixf Oculus360Videos::drawEyeView( const int eye, const float fovDegrees )
{
    VR4Matrixf mvp = Scene.MvpForEye( eye, fovDegrees );

	if ( MenuState != MENU_VIDEO_PLAYING )
	{
		// Draw the ovr scene
		const float fadeColor = CurrentFadeLevel;
		ModelDef & def = *const_cast< ModelDef * >( &Scene.WorldModel.Definition->Def );
		for ( int i = 0; i < def.surfaces.length(); i++ )
		{
			SurfaceDef & sd = def.surfaces[ i ];
			glUseProgram( SingleColorTextureProgram.program );

            glUniformMatrix4fv( SingleColorTextureProgram.uniformModelViewProMatrix, 1, GL_FALSE, mvp.Transposed().M[ 0 ] );

			glActiveTexture( GL_TEXTURE0 );
			glBindTexture( GL_TEXTURE_2D, sd.materialDef.textures[ 0 ] );

			glUniform4f( SingleColorTextureProgram.uniformColor, fadeColor, fadeColor, fadeColor, 1.0f );

			sd.geo.Draw();

			glBindTexture( GL_TEXTURE_2D, 0 ); // don't leave it bound
		}
	}
	else if ( ( MenuState == MENU_VIDEO_PLAYING ) && ( MovieTexture != NULL ) )
	{
		// draw animated movie panorama
		glActiveTexture( GL_TEXTURE0 );
		glBindTexture( GL_TEXTURE_EXTERNAL_OES, MovieTexture->textureId );

		glActiveTexture( GL_TEXTURE1 );
		glBindTexture( GL_TEXTURE_2D, BackgroundTexId );

		glDisable( GL_DEPTH_TEST );
		glDisable( GL_CULL_FACE );

		VGlShader & prog = ( BackgroundWidth == BackgroundHeight ) ? FadedPanoramaProgram : PanoramaProgram;

		glUseProgram( prog.program );
		glUniform4f( prog.uniformColor, 1.0f, 1.0f, 1.0f, 1.0f );

		// Videos have center as initial focal point - need to rotate 90 degrees to start there
        const VR4Matrixf view = Scene.ViewMatrixForEye( 0 ) * VR4Matrixf::RotationY( M_PI / 2 );
        const VR4Matrixf proj = Scene.ProjectionMatrixForEye( 0, fovDegrees );

		const int toggleStereo = VideoMenu->isOpenOrOpening() ? 0 : eye;

        glUniformMatrix4fv( prog.uniformTexMatrix, 1, GL_FALSE, TexmForVideo( toggleStereo ).Transposed().M[ 0 ] );
        glUniformMatrix4fv( prog.uniformModelViewProMatrix, 1, GL_FALSE, ( proj * view ).Transposed().M[ 0 ] );
		Globe.Draw();

		glBindTexture( GL_TEXTURE_EXTERNAL_OES, 0 );	// don't leave it bound
	}

	return mvp;
}

float Fade( double now, double start, double length )
{
	return NervGear::Alg::Clamp( ( ( now - start ) / length ), 0.0, 1.0 );
}

bool Oculus360Videos::IsVideoPlaying() const
{
	jmethodID methodId = vApp->vrJni()->GetMethodID( MainActivityClass, "isPlaying", "()Z" );
	if ( !methodId )
	{
		LOG( "Couldn't find isPlaying methodID" );
		return false;
	}

	bool isPlaying = vApp->vrJni()->CallBooleanMethod( vApp->javaObject(), methodId );
	return isPlaying;
}

void Oculus360Videos::PauseVideo( bool const force )
{
	LOG( "PauseVideo()" );

	jmethodID methodId = vApp->vrJni()->GetMethodID( MainActivityClass,
		"pauseMovie", "()V" );
	if ( !methodId )
	{
		LOG( "Couldn't find pauseMovie methodID" );
		return;
	}

	vApp->vrJni()->CallVoidMethod( vApp->javaObject(), methodId );
}

void Oculus360Videos::StopVideo()
{
	LOG( "StopVideo()" );

	jmethodID methodId = vApp->vrJni()->GetMethodID( MainActivityClass,
		"stopMovie", "()V" );
	if ( !methodId )
	{
		LOG( "Couldn't find stopMovie methodID" );
		return;
	}

	vApp->vrJni()->CallVoidMethod( vApp->javaObject(), methodId );

	delete MovieTexture;
	MovieTexture = NULL;
}

void Oculus360Videos::ResumeVideo()
{
	LOG( "ResumeVideo()" );

	vApp->guiSys().closeMenu( vApp, Browser, false );

	jmethodID methodId = vApp->vrJni()->GetMethodID( MainActivityClass,
		"resumeMovie", "()V" );
	if ( !methodId )
	{
		LOG( "Couldn't find resumeMovie methodID" );
		return;
	}

	vApp->vrJni()->CallVoidMethod( vApp->javaObject(), methodId );
}

void Oculus360Videos::StartVideo( const double nowTime )
{
	if ( ActiveVideo )
	{
		SetMenuState( MENU_VIDEO_LOADING );
		VideoName = ActiveVideo->url;
		LOG( "StartVideo( %s )", ActiveVideo->url.toCString() );
		vApp->playSound( "sv_select" );

		jmethodID startMovieMethodId = vApp->vrJni()->GetMethodID( MainActivityClass,
			"startMovieFromNative", "(Ljava/lang/String;)V" );

		if ( !startMovieMethodId )
		{
			LOG( "Couldn't find startMovie methodID" );
			return;
		}

		LOG( "moviePath = '%s'", ActiveVideo->url.toCString() );
        jstring jstr = vApp->vrJni()->NewStringUTF( ActiveVideo->url.toCString() );
		vApp->vrJni()->CallVoidMethod( vApp->javaObject(), startMovieMethodId, jstr );
		vApp->vrJni()->DeleteLocalRef( jstr );

		LOG( "StartVideo done" );
	}
}

void Oculus360Videos::SeekTo( const int seekPos )
{
	if ( ActiveVideo )
	{
		jmethodID seekToMethodId = vApp->vrJni()->GetMethodID( MainActivityClass,
			"seekToFromNative", "(I)V" );

		if ( !seekToMethodId )
		{
			LOG( "Couldn't find seekToMethodId methodID" );
			return;
		}

		vApp->vrJni()->CallVoidMethod( vApp->javaObject(), seekToMethodId, seekPos );

		LOG( "SeekTo %i done", seekPos );
	}
}

void Oculus360Videos::SetMenuState( const OvrMenuState state )
{
	OvrMenuState lastState = MenuState;
	MenuState = state;
	LOG( "%s to %s", MenuStateString( lastState ), MenuStateString( MenuState ) );
	switch ( MenuState )
	{
	case MENU_NONE:
		break;
	case MENU_BROWSER:
		Fader.forceFinish();
		Fader.reset();
		vApp->guiSys().closeMenu( vApp, VideoMenu, false );
		vApp->guiSys().openMenu( vApp, vApp->gazeCursor(), OvrFolderBrowser::MENU_NAME );
		if ( ActiveVideo )
		{
			StopVideo();
			ActiveVideo = NULL;
		}
		break;
	case MENU_VIDEO_LOADING:
		if ( MovieTexture != NULL )
		{
			delete MovieTexture;
			MovieTexture = NULL;
		}
		vApp->guiSys().closeMenu( vApp, Browser, false );
		vApp->guiSys().closeMenu( vApp, VideoMenu, false );
		Fader.startFadeOut();
		break;
	case MENU_VIDEO_READY:
		break;
	case MENU_VIDEO_PLAYING:
		Fader.reset();
		VideoMenuTimeLeft = VideoMenuVisibleTime;
		break;
	default:
		LOG( "Oculus360Videos::SetMenuState unknown state: %d", static_cast< int >( state ) );
		OVR_ASSERT( false );
		break;
	}
}

const char * menuStateNames [ ] =
{
	"MENU_NONE",
	"MENU_BROWSER",
	"MENU_VIDEO_LOADING",
	"MENU_VIDEO_READY",
	"MENU_VIDEO_PLAYING",
	"NUM_MENU_STATES"
};

const char* Oculus360Videos::MenuStateString( const OvrMenuState state )
{
	OVR_ASSERT( state >= 0 && state < NUM_MENU_STATES );
	return menuStateNames[ state ];
}

void Oculus360Videos::OnVideoActivated( const OvrMetaDatum * videoData )
{
	ActiveVideo = videoData;
	StartVideo( ovr_GetTimeInSeconds() );
}

VR4Matrixf Oculus360Videos::onNewFrame( const VrFrame vrFrame )
{
	// Disallow player foot movement, but we still want the head model
	// movement for the swipe view.
	VrFrame vrFrameWithoutMove = vrFrame;
	vrFrameWithoutMove.Input.sticks[ 0 ][ 0 ] = 0.0f;
	vrFrameWithoutMove.Input.sticks[ 0 ][ 1 ] = 0.0f;
	Scene.Frame( vApp->vrViewParms(), vrFrameWithoutMove, vApp->swapParms().ExternalVelocity );

	// Check for new video frames
	// latch the latest movie frame to the texture.
	if ( MovieTexture && CurrentVideoWidth ) {
		glActiveTexture( GL_TEXTURE0 );
		MovieTexture->Update();
		glBindTexture( GL_TEXTURE_EXTERNAL_OES, 0 );
		FrameAvailable = false;
	}

	if ( MenuState != MENU_BROWSER && MenuState != MENU_VIDEO_LOADING )
	{
		if ( vrFrame.Input.buttonReleased & ( BUTTON_TOUCH | BUTTON_A ) )
		{
			vApp->playSound( "sv_release_active" );
			if ( IsVideoPlaying() )
			{
				vApp->guiSys().openMenu( vApp, vApp->gazeCursor(), OvrVideoMenu::MENU_NAME );
				VideoMenu->repositionMenu( vApp );
				PauseVideo( false );
			}
			else
			{
				vApp->guiSys().closeMenu( vApp, VideoMenu, false );
				ResumeVideo();
			}
		}
	}

	// State transitions
	if ( Fader.fadeState() != Fader::FADE_NONE )
	{
		Fader.update( CurrentFadeRate, vrFrame.DeltaSeconds );
	}
	else if ( ( MenuState == MENU_VIDEO_READY ) &&
		( Fader.fadeAlpha() == 0.0f ) &&
		( MovieTexture != NULL ) )
	{
		SetMenuState( MENU_VIDEO_PLAYING );
		vApp->recenterYaw( true );
	}
	CurrentFadeLevel = Fader.finalAlpha();

	// We could disable the srgb convert on the FBO. but this is easier
	vApp->vrParms().colorFormat = UseSrgb ? COLOR_8888_sRGB : COLOR_8888;

	// Draw both eyes
	vApp->drawEyeViewsPostDistorted( Scene.CenterViewMatrix() );

	return Scene.CenterViewMatrix();
}

void Oculus360Videos::OnResume()
{
	LOG( "Oculus360Videos::OnResume" );
	if ( VideoWasPlayingWhenPaused )
	{
		vApp->guiSys().openMenu( vApp, vApp->gazeCursor(), OvrVideoMenu::MENU_NAME );
		VideoMenu->repositionMenu( vApp );
		PauseVideo( false );
	}
}

void Oculus360Videos::OnPause()
{
	LOG( "Oculus360Videos::OnPause" );
	VideoWasPlayingWhenPaused = IsVideoPlaying();
	if ( VideoWasPlayingWhenPaused )
	{
		PauseVideo( false );
	}
}

}
