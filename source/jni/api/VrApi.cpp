/************************************************************************************

Filename    :   VrApi.cpp
Content     :   Primary C level interface necessary for VR, App builds on this
Created     :   July, 2014
Authors     :   John Carmack

Copyright   :   Copyright 2014 Oculus VR, LLC. All Rights reserved.

*************************************************************************************/

#include "VrApi.h"
#include "VrApi_Android.h"

#include <unistd.h>						// gettid, usleep, etc
#include <jni.h>
#include <sstream>
#include <math.h>

#include <VLog.h>

#include "api/VGlOperation.h"
#include "android/JniUtils.h"
#include "android/VOsBuild.h"
#include "OVRVersion.h"					// for vrlib build version
#include "VString.h"			// for ReadFreq()
#include "VJson.h"			// needed for ovr_StartSystemActivity
#include "MemBuffer.h"		// needed for MemBufferT
#include "sensor/DeviceImpl.h"

#include "HmdInfo.h"
#include "HmdSensors.h"
#include "VFrameSmooth.h"
#include "VrApi_local.h"
#include "VrApi_Helpers.h"
#include "Vsync.h"
#include "SystemActivities.h"

NV_USING_NAMESPACE

// FIXME:VRAPI move to ovrMobile
static HMDState * OvrHmdState = NULL;
float OvrHmdYaw;

void ovr_InitSensors()
{
#if 0
	if ( OvrHmdState != NULL )
	{
		return;
	}

	OvrHmdState = new HMDState();
	if ( !OvrHmdState->InitDevice() )
	{
		FAIL( "failed to create HMD device" );
	}

	OvrHmdState->StartSensor( ovrHmdCap_Orientation|ovrHmdCap_YawCorrection, 0 );
	OvrHmdState->SetYaw( OvrHmdYaw );
#else

    OvrHmdState = new HMDState();
    if ( OvrHmdState != NULL )
	{
        OvrHmdState->initDevice();
    }

	if ( OvrHmdState == NULL )
	{
		FAIL( "failed to create HMD device" );
	}

	// Start the sensor running
    OvrHmdState->startSensor( ovrHmdCap_Orientation|ovrHmdCap_YawCorrection, 0 );
#endif
}

void ovr_ShutdownSensors()
{
#if 0
	if ( OvrHmdState == NULL )
	{
		return;
	}

	OvrHmdYaw = OvrHmdState->GetYaw();

	delete OvrHmdState;
	OvrHmdState = NULL;
#else

	if ( OvrHmdState != NULL )
	{
		delete OvrHmdState;
		OvrHmdState = NULL;
	}
#endif
}

bool ovr_InitializeInternal()
{
    // We must set up the system for the plugin to work
    if ( !NervGear::System::IsInitialized() )
	{
    	NervGear::System::Init( NervGear::Log::ConfigureDefaultLog( NervGear::LogMask_All ) );
	}

	ovr_InitSensors();

    return true;
}

void ovr_Shutdown()
{
	ovr_ShutdownSensors();

    // We should clean up the system to be complete
    NervGear::System::Destroy();
}

using namespace NervGear;

namespace NervGear {

SensorState::operator const ovrSensorState& () const
{
    OVR_COMPILER_ASSERT(sizeof(SensorState) == sizeof(ovrSensorState));
    return reinterpret_cast<const ovrSensorState&>(*this);
}

} // namespace NervGear

ovrSensorState ovr_GetSensorStateInternal( double absTime )
{
	if ( OvrHmdState == NULL )
	{
		ovrSensorState state;
		memset( &state, 0, sizeof( state ) );
		state.Predicted.Pose.Orientation.w = 1.0f;
		state.Recorded.Pose.Orientation.w = 1.0f;
		return state;
	}
	return OvrHmdState->predictedSensorState( absTime );
}

void ovr_RecenterYawInternal()
{
	if ( OvrHmdState == NULL )
	{
		return;
	}
	OvrHmdState->recenterYaw();
}

// Does latency test processing and returns 'true' if specified rgb color should
// be used to clear the screen.
bool ovr_ProcessLatencyTest( unsigned char rgbColorOut[3] )
{
	if ( OvrHmdState == NULL )
	{
		return false;
	}
	return OvrHmdState->processLatencyTest( rgbColorOut );
}

// Returns non-null string once with latency test result, when it is available.
// Buffer is valid until next call.
const char * ovr_GetLatencyTestResult()
{
	if ( OvrHmdState == NULL )
	{
		return "";
	}
	return OvrHmdState->latencyTestResult();
}

int ovr_GetDeviceManagerThreadTid()
{
	if ( OvrHmdState == NULL )
	{
		return 0;
	}
	return static_cast<NervGear::DeviceManagerImpl *>( OvrHmdState->deviceManager() )->threadTid();
}

/*
 * This interacts with the VrLib java class to deal with Android platform issues.
 */

// This is public for any user.
JavaVM	* VrLibJavaVM;

static pid_t	OnLoadTid;

// This needs to be looked up by a thread called directly from java,
// not a native pthread.
static jclass	VrLibClass = NULL;
static jclass	ProximityReceiverClass = NULL;
static jclass	DockReceiverClass = NULL;
static jclass	ConsoleReceiverClass = NULL;

static jmethodID getPowerLevelStateID = NULL;
static jmethodID setActivityWindowFullscreenID = NULL;
static jmethodID notifyMountHandledID = NULL;

static bool alwaysSkipPowerLevelCheck = false;
static double startPowerLevelCheckTime = -1.0;
static double powerLevelCheckLastReportTime = 0.0;

static bool hmtIsMounted = false;

// Register the HMT receivers once, and do
// not unregister in Pause(). We may miss
// important mount or dock events while
// the receiver is unregistered.
static bool registerHMTReceivers = false;

static int BuildVersionSDK = 19;		// default supported version for vrlib is KitKat 19

static int windowSurfaceWidth = 2560;	// default to Note4 resolution
static int windowSurfaceHeight = 1440;	// default to Note4 resolution

enum eHMTDockState
{
	HMT_DOCK_NONE,			// nothing to do
	HMT_DOCK_DOCKED,		// the device is inserted into the HMT
	HMT_DOCK_UNDOCKED		// the device has been removed from the HMT
};

struct HMTDockState_t
{
	HMTDockState_t() :
		DockState( HMT_DOCK_NONE )
	{
	}

	explicit HMTDockState_t( eHMTDockState const dockState ) :
		DockState( dockState )
	{
	}

	eHMTDockState	DockState;
};

enum eHMTMountState
{
	HMT_MOUNT_NONE,			// nothing to do
	HMT_MOUNT_MOUNTED,		// the HMT has been placed on the head
	HMT_MOUNT_UNMOUNTED		// the HMT has been removed from the head
};

struct HMTMountState_t
{
	HMTMountState_t() :
		MountState( HMT_MOUNT_NONE )
	{
	}

	explicit HMTMountState_t( eHMTMountState const mountState ) :
		MountState( mountState )
	{
	}

	eHMTMountState	MountState;
};

template< typename T, T _initValue_ >
class LocklessVar
{
public:
	LocklessVar() : Value( _initValue_ ) { }
	LocklessVar( T const v ) : Value( v ) { }

	T	Value;
};

class LocklessDouble
{
public:
	LocklessDouble( const double v ) : Value( v ) { };
	LocklessDouble() : Value( -1 ) { };

	double Value;
};

typedef LocklessVar< int, -1> 						volume_t;
NervGear::LocklessUpdater< volume_t >					CurrentVolume;
NervGear::LocklessUpdater< LocklessDouble >				TimeOfLastVolumeChange;
NervGear::LocklessUpdater< batteryState_t >				BatteryState;
NervGear::LocklessUpdater< bool >						HeadsetPluggedState;
NervGear::LocklessUpdater< bool >						PowerLevelStateThrottled;
NervGear::LocklessUpdater< bool >						PowerLevelStateMinimum;
NervGear::LocklessUpdater< HMTMountState_t >				HMTMountState;
NervGear::LocklessUpdater< HMTDockState_t >				HMTDockState;	// edge triggered events, not the actual state
static NervGear::LocklessUpdater< bool >					DockState;


typedef LocklessVar< int, -1> wifiSignalLevel_t;
NervGear::LocklessUpdater< wifiSignalLevel_t >			WifiSignalLevel;
typedef LocklessVar< eWifiState, WIFI_STATE_UNKNOWN > wifiState_t;
NervGear::LocklessUpdater< wifiState_t >					WifiState;

typedef LocklessVar< int, -1> cellularSignalLevel_t;
NervGear::LocklessUpdater< cellularSignalLevel_t >		CellularSignalLevel;
typedef LocklessVar< eCellularState, CELLULAR_STATE_IN_SERVICE > cellularState_t;
NervGear::LocklessUpdater< cellularState_t >				CellularState;

extern "C"
{
// The JNIEXPORT macro prevents the functions from ever being stripped out of the library.

void Java_com_vrseen_nervgear_VrLib_nativeVsync( JNIEnv *jni, jclass clazz, jlong frameTimeNanos );

JNIEXPORT jint JNI_OnLoad( JavaVM * vm, void * reserved )
{
	LOG( "JNI_OnLoad" );

	// Lookup our classnames
	ovr_OnLoad( vm );

	// Start up the Oculus device manager
	ovr_Init();

	return JNI_VERSION_1_6;
}

JNIEXPORT void Java_com_vrseen_nervgear_VrLib_nativeVolumeEvent(JNIEnv *jni, jclass clazz, jint volume)
{
    LOG( "nativeVolumeEvent(%i)", volume );

    CurrentVolume.setState( volume );
    double now = ovr_GetTimeInSeconds();

    TimeOfLastVolumeChange.setState( now );
}

JNIEXPORT void Java_com_vrseen_nervgear_VrLib_nativeBatteryEvent(JNIEnv *jni, jclass clazz, jint status, jint level, jint temperature)
{
    LOG( "nativeBatteryEvent(%i, %i, %i)", status, level, temperature );

    batteryState_t state;
    state.level = level;
    state.temperature = temperature;
    state.status = (eBatteryStatus)status;
    BatteryState.setState( state );
}

JNIEXPORT void Java_com_vrseen_nervgear_VrLib_nativeHeadsetEvent(JNIEnv *jni, jclass clazz, jint state)
{
    LOG( "nativeHeadsetEvent(%i)", state );
    HeadsetPluggedState.setState( ( state == 1 ) );
}

JNIEXPORT void Java_com_vrseen_nervgear_VrLib_nativeWifiEvent( JNIEnv * jni, jclass clazz, jint state, jint level )
{
	LOG( "nativeWifiSignalEvent( %i, %i )", state, level );
	WifiState.setState( wifiState_t( static_cast< eWifiState >( state ) ) );
	WifiSignalLevel.setState( wifiSignalLevel_t( level ) );
}

JNIEXPORT void Java_com_vrseen_nervgear_VrLib_nativeCellularStateEvent( JNIEnv * jni, jclass clazz, jint state )
{
	LOG( "nativeCellularStateEvent( %i )", state );
	CellularState.setState( cellularState_t( static_cast< eCellularState >( state ) ) );
}

JNIEXPORT void Java_com_vrseen_nervgear_VrLib_nativeCellularSignalEvent( JNIEnv * jni, jclass clazz, jint level )
{
	LOG( "nativeCellularSignalEvent( %i )", level );
	CellularSignalLevel.setState( cellularSignalLevel_t( level ) );
}

JNIEXPORT void Java_com_vrseen_nervgear_ProximityReceiver_nativeMountHandled(JNIEnv *jni, jclass clazz)
{
	LOG( "Java_com_vrseen_nervgear_VrLib_nativeMountEventHandled" );

	// If we're received this, the foreground app has already received
	// and processed the mount event.
	if ( HMTMountState.state().MountState == HMT_MOUNT_MOUNTED )
	{
		LOG( "RESETTING MOUNT" );
		HMTMountState.setState( HMTMountState_t( HMT_MOUNT_NONE ) );
	}
}

JNIEXPORT void Java_com_vrseen_nervgear_ProximityReceiver_nativeProximitySensor(JNIEnv *jni, jclass clazz, jint state)
{
	LOG( "nativeProximitySensor(%i)", state );
	if ( state == 0 )
	{
		HMTMountState.setState( HMTMountState_t( HMT_MOUNT_UNMOUNTED ) );
	}
	else
	{
		HMTMountState.setState( HMTMountState_t( HMT_MOUNT_MOUNTED ) );
	}
}

JNIEXPORT void Java_com_vrseen_nervgear_DockReceiver_nativeDockEvent(JNIEnv *jni, jclass clazz, jint state)
{
	LOG( "nativeDockEvent = %s", ( state == 0 ) ? "UNDOCKED" : "DOCKED" );

	DockState.setState( state != 0 );

	if ( state == 0 )
	{
		// On undock, we need to do the following 2 things:
		// (1) Provide a means for apps to save their state.
		// (2) Call finish() to kill app.

		// NOTE: act.finish() triggers OnPause() -> OnStop() -> OnDestroy() to
		// be called on the activity. Apps may place save data and state in
		// OnPause() (or OnApplicationPause() for Unity)

		HMTDockState.setState( HMTDockState_t( HMT_DOCK_UNDOCKED ) );
	}
	else
	{
		HMTDockState_t dockState = HMTDockState.state();
		if ( dockState.DockState == HMT_DOCK_UNDOCKED )
		{
			LOG( "CLEARING UNDOCKED!!!!" );
		}
		HMTDockState.setState( HMTDockState_t( HMT_DOCK_DOCKED ) );
	}
}

} // extern "C"

const char *ovr_GetVersionString()
{
    return NV_VERSION_STRING;
}

double ovr_GetTimeInSeconds()
{
	return NervGear::VTimer::Seconds();
}

// This must be called by a function called directly from a java thread,
// preferably at JNI_OnLoad().  It will fail if called from a pthread created
// in native code, or from a NativeActivity due to the class-lookup issue:
//
// http://developer.android.com/training/articles/perf-jni.html#faq_FindClass
//
// This should not start any threads or consume any significant amount of
// resources, so hybrid apps aren't penalizing their normal mode of operation
// by supporting VR.
void ovr_OnLoad( JavaVM * JavaVm_ )
{
	LOG( "ovr_OnLoad()" );

	if ( JavaVm_ == NULL )
	{
		FAIL( "JavaVm == NULL" );
	}
	if ( VrLibJavaVM != NULL )
	{
		// Should we silently return instead?
		FAIL( "ovr_OnLoad() called twice" );
	}

	VrLibJavaVM = JavaVm_;
	OnLoadTid = gettid();

	JNIEnv * jni;
	bool privateEnv = false;
	if ( JNI_OK != VrLibJavaVM->GetEnv( reinterpret_cast<void**>(&jni), JNI_VERSION_1_6 ) )
	{
		LOG( "Creating temporary JNIEnv" );
		// We will detach after we are done
		privateEnv = true;
		const jint rtn = VrLibJavaVM->AttachCurrentThread( &jni, 0 );
		if ( rtn != JNI_OK )
		{
			FAIL( "AttachCurrentThread returned %i", rtn );
		}
	}
	else
	{
		LOG( "Using caller's JNIEnv" );
	}

	VrLibClass = JniUtils::GetGlobalClassReference( jni, "com/vrseen/nervgear/VrLib" );
	ProximityReceiverClass = JniUtils::GetGlobalClassReference( jni, "com/vrseen/nervgear/ProximityReceiver" );
	DockReceiverClass = JniUtils::GetGlobalClassReference( jni, "com/vrseen/nervgear/DockReceiver" );
	ConsoleReceiverClass = JniUtils::GetGlobalClassReference( jni, "com/vrseen/nervgear/ConsoleReceiver" );

	// Get the BuildVersion SDK
	jclass versionClass = jni->FindClass( "android/os/Build$VERSION" );
	if ( versionClass != 0 )
	{
		jfieldID sdkIntFieldID = jni->GetStaticFieldID( versionClass, "SDK_INT", "I" );
		if ( sdkIntFieldID != 0 )
		{
			BuildVersionSDK = jni->GetStaticIntField( versionClass, sdkIntFieldID );
			LOG( "BuildVersionSDK %d", BuildVersionSDK );
		}
		jni->DeleteLocalRef( versionClass );
	}

	// Explicitly register our functions.
	// Without this, the automatic name lookup fails in some projects
	// where we are loaded as a secondary .so.
	struct
	{
		jclass			Clazz;
		JNINativeMethod	Jnim;
	} gMethods[] =
	{
		{ DockReceiverClass, 		{ "nativeDockEvent", "(I)V",(void*)Java_com_vrseen_nervgear_DockReceiver_nativeDockEvent } },
		{ ProximityReceiverClass, 	{ "nativeProximitySensor", "(I)V",(void*)Java_com_vrseen_nervgear_ProximityReceiver_nativeProximitySensor } },
		{ ProximityReceiverClass, 	{ "nativeMountHandled", "()V",(void*)Java_com_vrseen_nervgear_ProximityReceiver_nativeMountHandled } },
		{ VrLibClass, 				{ "nativeVolumeEvent", "(I)V",(void*)Java_com_vrseen_nervgear_VrLib_nativeVolumeEvent } },
		{ VrLibClass, 				{ "nativeBatteryEvent", "(III)V",(void*)Java_com_vrseen_nervgear_VrLib_nativeBatteryEvent } },
		{ VrLibClass, 				{ "nativeHeadsetEvent", "(I)V",(void*)Java_com_vrseen_nervgear_VrLib_nativeHeadsetEvent } },
		{ VrLibClass, 				{ "nativeWifiEvent", "(II)V",(void*)Java_com_vrseen_nervgear_VrLib_nativeWifiEvent } },
		{ VrLibClass, 				{ "nativeCellularStateEvent", "(I)V",(void*)Java_com_vrseen_nervgear_VrLib_nativeCellularStateEvent } },
		{ VrLibClass, 				{ "nativeCellularSignalEvent", "(I)V",(void*)Java_com_vrseen_nervgear_VrLib_nativeCellularSignalEvent } },
		{ VrLibClass, 				{ "nativeVsync", "(J)V",(void*)Java_com_vrseen_nervgear_VrLib_nativeVsync } },
	};
	const int count = sizeof( gMethods ) / sizeof( gMethods[0] );

	// Register one at a time, so we can issue a good error message.
	for ( int i = 0; i < count; i++ )
	{
		if ( JNI_OK != jni->RegisterNatives( gMethods[i].Clazz, &gMethods[i].Jnim, 1 ) )
		{
			FAIL( "RegisterNatives failed on %s", gMethods[i].Jnim.name, 1 );
		}
	}

	// Detach if the caller wasn't already attached
	if ( privateEnv )
	{
		LOG( "Freeing temporary JNIEnv" );
		VrLibJavaVM->DetachCurrentThread();
	}
}

// A dedicated VR app will usually call this immediately after ovr_OnLoad(),
// but a hybrid app may want to defer calling it until the first headset
// plugin event to avoid starting the device manager.
void ovr_Init()
{
	LOG( "ovr_Init" );

	// initialize Oculus code
	ovr_InitializeInternal();

	JNIEnv * jni;
	const jint rtn = VrLibJavaVM->AttachCurrentThread( &jni, 0 );
	if ( rtn != JNI_OK )
	{
		FAIL( "AttachCurrentThread returned %i", rtn );
	}

	// After ovr_Initialize(), because it uses String
    VOsBuild::Init(jni);

	NervGear::SystemActivities_InitEventQueues();
}

// This sends an explicit intent to the package/classname with the command and URI in the intent.
void ovr_SendIntent( ovrMobile * ovr, const char * actionName, const char * toPackageName,
		const char * toClassName, const char * command, const char * uri, eExitType exitType )
{
	LOG( "ovr_SendIntent( '%s' '%s/%s' '%s' '%s' )", actionName, toPackageName, toClassName,
			( command != NULL ) ? command : "<NULL>",
			( uri != NULL ) ? uri : "<NULL>" );

	// Eliminate any frames of lost head tracking, push black images to the screen
	const ovrTimeWarpParms warpSwapBlackParms = InitTimeWarpParms( WARP_INIT_BLACK );
	ovr_WarpSwap( ovr, &warpSwapBlackParms );

	// We need to leave before sending the intent, or the leave VR mode from the platform
	// activity can end up executing after we've already entered VR mode for the main activity.
	// This was showing up as the vsync callback getting turned off after it had already
	// been turned on.
	ovr_LeaveVrMode( ovr );

	JavaString actionString( ovr->Jni, actionName );
	JavaString packageString( ovr->Jni, toPackageName );
	JavaString className( ovr->Jni, toClassName );
	JavaString commandString( ovr->Jni, command == NULL ? PUI_GLOBAL_MENU : command );
	JavaString uriString( ovr->Jni, uri == NULL ? "" : uri );

	jmethodID sendIntentFromNativeId = JniUtils::GetStaticMethodID( ovr->Jni, VrLibClass,
			"sendIntentFromNative", "(Landroid/app/Activity;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V" );
	if ( sendIntentFromNativeId != NULL )
	{
		ovr->Jni->CallStaticVoidMethod( VrLibClass, sendIntentFromNativeId, ovr->Parms.ActivityObject,
				actionString.toJString(), packageString.toJString(), className.toJString(),
				commandString.toJString(), uriString.toJString() );
	}

	if ( exitType != EXIT_TYPE_NONE )
	{
		ovr_ExitActivity( ovr, exitType );
	}
}

void CreateSystemActivitiesCommand(const char * toPackageName, const char * command, const char * uri, NervGear::VString & out )
{
	// serialize the command to a JSON object with version inf
    VJsonObject obj;
    obj.insert("Command", command);
    obj.insert("OVRVersion", ovr_GetVersionString());
    obj.insert("PlatformUIVersion", PLATFORM_UI_VERSION);
    obj.insert("ToPackage", toPackageName);

    std::stringstream s;
    s << VJson(std::move(obj));

    char text[10240];
    s.getline(text, 10240);
    out = text;
}

// This will query Android for the launch intent of the specified package, then append the
// command and URI data to the intent.
void ovr_SendLaunchIntent( ovrMobile * ovr, const char * toPackageName, const char * command,
		const char * uri, eExitType exitType )
{
	LOG( "ovr_SendLaunchIntent( '%s' '%s' '%s' )", toPackageName,
			( command != NULL ) ? command : "<NULL>",
			( uri != NULL ) ? uri : "<NULL>" );

	// Eliminate any frames of lost head tracking, push black images to the screen
	const ovrTimeWarpParms warpSwapBlackParms = InitTimeWarpParms( WARP_INIT_BLACK );
	ovr_WarpSwap( ovr, &warpSwapBlackParms );

	// We need to leave before sending the intent, or the leave VR mode from the platform
	// activity can end up executing after we've already entered VR mode for the main activity.
	// This was showing up as the vsync callback getting turned off after it had already
	// been turned on.
	ovr_LeaveVrMode( ovr );

	JavaString packageString( ovr->Jni, toPackageName );
	JavaString commandString( ovr->Jni, command == NULL ? PUI_GLOBAL_MENU : command );
	JavaString uriString( ovr->Jni, uri == NULL ? "" : uri );

	jmethodID sendLaunchIntentId = JniUtils::GetStaticMethodID( ovr->Jni, VrLibClass,
			"sendLaunchIntent", "(Landroid/app/Activity;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V" );
	if ( sendLaunchIntentId != NULL )
	{
		ovr->Jni->CallStaticVoidMethod( VrLibClass, sendLaunchIntentId, ovr->Parms.ActivityObject,
				packageString.toJString(), commandString.toJString(), uriString.toJString() );
	}

	if ( exitType != EXIT_TYPE_NONE )
	{
		ovr_ExitActivity( ovr, exitType );
	}
}

bool ovr_StartSystemActivity_JSON( ovrMobile * ovr, const char * jsonText )
{
	return true;
}

// creates a command for sending to System Activities with optional embedded extra JSON text.
bool ovr_CreateSystemActivityIntent( ovrMobile * ovr, const char * command, const char * extraJsonText,
		char * outBuffer, unsigned long long const outBufferSize, unsigned long long & outRequiredBufferSize )
{
	outRequiredBufferSize = 0;
	if ( outBuffer == NULL || outBufferSize < 1 )
	{
		return false;
	}
	outBuffer[0] = '\0';

    VJsonObject jsonObj;

    jsonObj.insert("Command", command );
    jsonObj.insert("OVRVersion", ovr_GetVersionString() );
    jsonObj.insert( "PlatformUIVersion", PLATFORM_UI_VERSION );

    std::stringstream s;
    s << VJson(std::move(jsonObj));
    std::string str;
    s.str(str);

    outRequiredBufferSize = str.length() + 1;
    if ( extraJsonText != NULL && extraJsonText[0] != '\0' )
        outRequiredBufferSize += strlen(extraJsonText);

    if ( outBufferSize < outRequiredBufferSize )
        return false;

    // combine the JSON objects
    strcpy(outBuffer, str.c_str());
    if ( extraJsonText != NULL && extraJsonText[0] != '\0' ) {
        int offset = str.length() - 1;
        strcpy(outBuffer + offset, extraJsonText);
    }

	return true;
}

// creates a JSON object with command and version info in it and combines that with a pre-serialized json object
bool ovr_StartSystemActivity( ovrMobile * ovr, const char * command, const char * extraJsonText )
{
	unsigned long long const INTENT_COMMAND_SIZE = 1024;
	NervGear::MemBufferT< char > intentBuffer( INTENT_COMMAND_SIZE );
	unsigned long long requiredSize = 0;
	if ( !ovr_CreateSystemActivityIntent( ovr, command, extraJsonText, intentBuffer, INTENT_COMMAND_SIZE, requiredSize ) )
	{
		OVR_ASSERT( requiredSize > INTENT_COMMAND_SIZE );	// if this isn't true, command creation failed for some other reason
		// reallocate a buffer of the required size
		intentBuffer.realloc( requiredSize );
		bool ok = ovr_CreateSystemActivityIntent( ovr, command, extraJsonText, intentBuffer, INTENT_COMMAND_SIZE, requiredSize );
		if ( !ok )
		{
			OVR_ASSERT( ok );
			return false;
		}
	}

	return ovr_StartSystemActivity_JSON( ovr, intentBuffer );
}

void ovr_ExitActivity( ovrMobile * ovr, eExitType exitType )
{
	if ( exitType == EXIT_TYPE_FINISH )
	{
		LOG( "ovr_ExitActivity( EXIT_TYPE_FINISH ) - act.finish()" );

		ovr_LeaveVrMode( ovr );

		OVR_ASSERT( ovr != NULL );

	//	const char * name = "finish";
		const char * name = "finishOnUiThread";
		const jmethodID mid = JniUtils::GetStaticMethodID( ovr->Jni, VrLibClass,
				name, "(Landroid/app/Activity;)V" );

		if ( ovr->Jni->ExceptionOccurred() )
		{
			ovr->Jni->ExceptionClear();
			LOG("Cleared JNI exception");
		}
		LOG( "Calling activity.finishOnUiThread()" );
		ovr->Jni->CallStaticVoidMethod( VrLibClass, mid, *static_cast< jobject* >( &ovr->Parms.ActivityObject ) );
		LOG( "Returned from activity.finishOnUiThread()" );
	}
	else if ( exitType == EXIT_TYPE_FINISH_AFFINITY )
	{
		LOG( "ovr_ExitActivity( EXIT_TYPE_FINISH_AFFINITY ) - act.finishAffinity()" );

		OVR_ASSERT( ovr != NULL );

		ovr_LeaveVrMode( ovr );

		const char * name = "finishAffinityOnUiThread";
		const jmethodID mid = JniUtils::GetStaticMethodID( ovr->Jni, VrLibClass,
				name, "(Landroid/app/Activity;)V" );

		if ( ovr->Jni->ExceptionOccurred() )
		{
			ovr->Jni->ExceptionClear();
			LOG("Cleared JNI exception");
		}
		LOG( "Calling activity.finishAffinityOnUiThread()" );
		ovr->Jni->CallStaticVoidMethod( VrLibClass, mid, *static_cast< jobject* >( &ovr->Parms.ActivityObject ) );
		LOG( "Returned from activity.finishAffinityOnUiThread()" );
	}
	else if ( exitType == EXIT_TYPE_EXIT )
	{
		LOG( "ovr_ExitActivity( EXIT_TYPE_EXIT ) - exit(0)" );

		// This should only ever be called from the Java thread.
		// ovr_LeaveVrMode() should have been called already from the VrThread.
		OVR_ASSERT( ovr == NULL || ovr->Destroyed );

		if ( OnLoadTid != gettid() )
		{
			FAIL( "ovr_ExitActivity( EXIT_TYPE_EXIT ): Called with tid %d instead of %d", gettid(), OnLoadTid );
		}

		NervGear::SystemActivities_ShutdownEventQueues();
		ovr_Shutdown();
		exit( 0 );
	}
}

// Always returns a 0 terminated, possibly empty, string.
// Strips any trailing \n.
static const char * ReadSmallFile( const char * path )
{
	static char buffer[1024];
	buffer[0] = 0;

	FILE * f = fopen( path, "r" );
	if ( !f )
	{
		return buffer;
	}
	const int r = fread( buffer, 1, sizeof( buffer ) - 1, f );
	fclose( f );

	// Strip trailing \n that some sys files have.
	for ( int n = r; n > 0 && buffer[n] == '\n'; n-- )
	{
		buffer[n] = 0;
	}
	return buffer;
}

static NervGear::VString StripLinefeed( const NervGear::VString s )
{
	NervGear::VString copy;
    for ( int i = 0; i < (int) s.length() && s.at( i ) != '\n'; i++ )
	{
        copy += s.at( i );
	}
	return copy;
}

static int ReadFreq( const char * pathFormat, ... )
{
	char fullPath[1024] = {};

	va_list argptr;
	va_start( argptr, pathFormat );
	vsnprintf( fullPath, sizeof( fullPath ) - 1, pathFormat, argptr );
	va_end( argptr );

	NervGear::VString clock = ReadSmallFile( fullPath );
	clock = StripLinefeed( clock );
	if ( clock == "" )
	{
		return -1;
	}
	return atoi( clock.toCString() );
}

// System Power Level State
enum ePowerLevelState
{
	POWERLEVEL_NORMAL = 0,		// Device operating at full level
	POWERLEVEL_POWERSAVE = 1,	// Device operating at throttled level
	POWERLEVEL_MINIMUM = 2,		// Device operating at minimum level
	MAX_POWERLEVEL_STATES
};
enum ePowerLevelAction
{
	POWERLEVEL_ACTION_NONE,
	POWERLEVEL_ACTION_WAITING_FOR_UNMOUNT,
	POWERLEVEL_ACTION_WAITING_FOR_UNDOCK,
	POWERLEVEL_ACTION_WAITING_FOR_RESET,
	MAX_POWERLEVEL_ACTIONS
};
static ePowerLevelAction powerLevelAction = POWERLEVEL_ACTION_NONE;

bool ovr_GetPowerLevelStateThrottled()
{
	return PowerLevelStateThrottled.state();
}

bool ovr_GetPowerLevelStateMinimum()
{
	return PowerLevelStateMinimum.state();
}

void ovr_CheckPowerLevelState( ovrMobile * ovr )
{
	const double timeNow = floor( ovr_GetTimeInSeconds() );
	bool checkThisFrame = false;
	if ( timeNow > powerLevelCheckLastReportTime )
	{
		checkThisFrame = true;
		powerLevelCheckLastReportTime = timeNow;
	}
	if ( !checkThisFrame )
	{
		return;
	}

	ePowerLevelState powerLevelState = POWERLEVEL_NORMAL;
	if ( getPowerLevelStateID != NULL )
	{
		powerLevelState = (ePowerLevelState)ovr->Jni->CallStaticIntMethod( VrLibClass, getPowerLevelStateID, ovr->Parms.ActivityObject );
	}
#if 0
	ovr_UpdateLocalPreferences();
	const char * powerLevelStr = ovr_GetLocalPreferenceValueForKey( "dev_powerLevelState", "-1" );	// "0", "1", "2"
	const int powerLevel = atoi( powerLevelStr );
	if ( powerLevel >= 0 )
	{
		powerLevelState = (ePowerLevelState)powerLevel;
	}
	LOG( "powerlevelstate %d powerlevelaction %d", powerLevelState, powerLevelAction );
#endif

	// Skip handling of power level changes (ie, on unmount)
	const bool isThrottled = PowerLevelStateThrottled.state();

	// TODO: Test if we can leave the sys files open and do continuous read or do we always
	// need to read then close?

	// NOTE: Only testing one core for thermal throttling.
	// we won't be able to read the cpu clock unless it has been chmod'd to 0666, but try anyway.
    VGlOperation glOperation;
    int cpuCore = 0;
    if ( ( glOperation.EglGetGpuType() & NervGear::GpuType::GPU_TYPE_MALI ) != 0 )
	{
		// Use the first BIG core if it is online, otherwise use the first LITTLE core.
		const char * online = ReadSmallFile( "/sys/devices/system/cpu/cpu4/online" );
		cpuCore = ( online[0] != '\0' && atoi( online ) != 0 ) ? 4 : 0;
	}

    const int64_t cpuUnit = ( ( glOperation.EglGetGpuType() & NervGear::GpuType::GPU_TYPE_MALI ) != 0 ) ? 1000LL : 1000LL;
	const int64_t cpuFreq = ReadFreq( "/sys/devices/system/cpu/cpu%i/cpufreq/scaling_cur_freq", cpuCore );
    const int64_t gpuUnit = ( ( glOperation.EglGetGpuType() & NervGear::GpuType::GPU_TYPE_MALI ) != 0 ) ? 1000000LL : 1000LL;
    const int64_t gpuFreq = ReadFreq( ( ( glOperation.EglGetGpuType() & NervGear::GpuType::GPU_TYPE_MALI ) != 0 ) ?
									"/sys/devices/14ac0000.mali/clock" :
									"/sys/class/kgsl/kgsl-3d0/gpuclk" );
	// unused macros are so the spam can be #if 0'd out for debugging without getting unused variable compiler warnings
	OVR_UNUSED( cpuUnit );
	OVR_UNUSED( cpuFreq );
	OVR_UNUSED( gpuUnit );
	OVR_UNUSED( gpuFreq );
#if 1
	LOG( "CPU%d Clock %lld MHz, GPU Clock %lld MHz, Power Level State %d: %s, Temp %fC",
			cpuCore, cpuFreq * cpuUnit / 1000000, gpuFreq * gpuUnit / 1000000,
			powerLevelState, ( isThrottled ) ? "throttled" : "normal", ovr_GetBatteryState().temperature / 10.0f );
#endif
	/*
	------------------------------------------------------------
	When the device first transitions from NORMAL to POWERSAVE,
	we display a warning that performance will be degraded and
	force 30Hz TimeWarp operation.

	We stay in this state even if the power level state goes back
	to NORMAL after cooling down, because it would be expected to
	rapidly go back up if 60Hz operation were resumed.

	60Hz operation will only be re-enabled after the headset has
	been taken off the user's head.

	If the power level goes to MINIMUM, the platform UI will come
	up with a warning and not allow the game to continue. The user
	will have to take the headset off and wait to get it reset,
	but no state in the app should be lost.
	-------------------------------------------------------------
	*/
	if ( powerLevelState == POWERLEVEL_NORMAL )
	{
		if ( powerLevelAction == POWERLEVEL_ACTION_WAITING_FOR_RESET )
		{
			LOG( "RESET FROM POWERSAVE MODE" );
			PowerLevelStateThrottled.setState( false );
			powerLevelAction = POWERLEVEL_ACTION_NONE;
		}
	}
	else if ( powerLevelState == POWERLEVEL_POWERSAVE )
	{
		// CPU and GPU have been de-clocked to power-save levels.
		if ( !isThrottled )
		{
			if ( ovr->Parms.AllowPowerSave )
			{
				// If app allows power save, adjust application performance
				// to account for lower clock frequencies and warn about
				// performance degradation.
				LOG( "THERMAL THROTTLING LEVEL 1 - POWER SAVE MODE FORCING 30FPS" );
				PowerLevelStateThrottled.setState( true );
				powerLevelAction = POWERLEVEL_ACTION_WAITING_FOR_UNMOUNT;
				ovr_StartSystemActivity( ovr, PUI_THROTTLED1, NULL );
			}
			else
			{
				if ( powerLevelAction != POWERLEVEL_ACTION_WAITING_FOR_UNDOCK )
				{
					LOG( "THERMAL THROTTLING LEVEL 2 - CANNOT CONTINUE" );
					PowerLevelStateThrottled.setState( true );
					PowerLevelStateMinimum.setState( true );
					powerLevelAction = POWERLEVEL_ACTION_WAITING_FOR_UNDOCK;
					ovr_StartSystemActivity( ovr, PUI_THROTTLED2, NULL );
				}
			}
		}
	}
	else if ( powerLevelState == POWERLEVEL_MINIMUM )
	{
		if ( powerLevelAction != POWERLEVEL_ACTION_WAITING_FOR_UNDOCK )
		{
			// Warn that the game cannot continue in minimum mode.
			LOG( "THERMAL THROTTLING LEVEL 2 - CANNOT CONTINUE" );
			PowerLevelStateThrottled.setState( true );
			PowerLevelStateMinimum.setState( true );
			powerLevelAction = POWERLEVEL_ACTION_WAITING_FOR_UNDOCK;
			ovr_StartSystemActivity( ovr, PUI_THROTTLED2, NULL );
		}
	}
}

/*
Fixed Clock Level API

return2EnableFreqLev returns the [min,max] clock levels available for the
CPU and GPU, ie [0,3]. However, not all clock combinations are valid for
all devices. For instance, the highest GPU level may not be available for
use with the highest CPU levels. If an invalid matrix combination is
provided, the system will not acknowlege the request and clock settings
will go into dynamic mode.

Fixed clock level on Qualcomm Snapdragon 805 based Note 4
GPU MHz: 0 =    240, 1 =   300, 2 =   389, 3 =   500
CPU MHz: 0 =    884, 1 =  1191, 2 =  1498, 3 =  1728

Fixed clock level on ARM Exynos 5433 based Note 4
GPU MHz: 0 =    266, 1 =   350, 2 =   500, 3 =   550
CPU MHz: 0 = L 1000, 1 = B 700, 2 = B 700, 3 = B 800

Fixed clock level on ARM Exynos 7420 based Zero (as of 2015-02-10)
GPU MHz: 0 =    266, 1 =   350, 2 =   420, 3 =   544  4 =    600
			 1200/2		  1500/2
CPU MHz: 0 = L  600, 1 = L 750, 2 = B 800, 3 = B 800, 4 = B 1000, 5 = B 1200

VRSVC as of 2015-02-10 provides support for the cpu to remain in dynamic
mode with a fixed GPU Level. NOTE: The intended usage for CPU dynamic mode
is for apps like 360 photos which have a big burst of high performance needs
for decompression, then long periods of low load. However, in testing, this
appears to cause tearing due to TimeWarp not finishing on time. To enable
CPU dynamic mode for testing purposes only, set cpuLevel = -1.
*/

enum {
	LEVEL_GPU_MIN = 0,
	LEVEL_GPU_MAX,
	LEVEL_CPU_MIN,
	LEVEL_CPU_MAX
};

enum {
	CLOCK_CPU_FREQ = 0,
	CLOCK_GPU_FREQ,
	CLOCK_POWERSAVE_CPU_FREQ,
	CLOCK_POWERSAVE_GPU_FREQ
};

static const int INVALID_CLOCK_LEVEL = -1;
static const int DYNAMIC_MODE_LEVEL = -1;

#if 0
static bool WriteFreq( const int freq, const char * pathFormat, ... )
{
	char fullPath[1024] = {};

	va_list argptr;
	va_start( argptr, pathFormat );
	vsnprintf( fullPath, sizeof( fullPath ) - 1, pathFormat, argptr );
	va_end( argptr );

	char string[1024] = {};
	sprintf( string, "%d", freq );

	FILE * f = fopen( fullPath, "w" );
	if ( !f )
	{
		LOG( "failed to open %s", fullPath );
		return false;
	}
	fwrite( string, 1, strlen( string ), f );
	fclose( f );

	LOG( "wrote %s to %s", string, fullPath );
	return true;
}
#endif

/*
 * Set the Fixed CPU/GPU Clock Levels
 */
static void SetVrSystemPerformance( JNIEnv * VrJni, jclass vrActivityClass, jobject activityObject,
		const int cpuLevel, const int gpuLevel )
{
}
/*{
	// Clear any previous exceptions.
	// NOTE: This can be removed once security exception handling is moved to
	// Java IF.
	if ( VrJni->ExceptionOccurred() )
	{
		VrJni->ExceptionClear();
		LOG( "SetVrSystemPerformance: Enter: JNI Exception occurred" );
	}

	LOG( "SetVrSystemPerformance( %i, %i )", cpuLevel, gpuLevel );

	// Get the available clock levels for the device.
	const jmethodID getAvailableClockLevelsId = ovr_GetStaticMethodID( VrJni, vrActivityClass,
		"getAvailableFreqLevels", "(Landroid/app/Activity;)[I" );
	jintArray jintLevels = (jintArray)VrJni->CallStaticObjectMethod( vrActivityClass,
		getAvailableClockLevelsId, activityObject );

	// Move security exception detection to the java IF.
	// Catch Permission denied
	if ( VrJni->ExceptionOccurred() )
	{
		VrJni->ExceptionClear();
		LOG( "SetVrSystemPerformance: JNI Exception occurred, returning" );
		return;
	}

	OVR_ASSERT( VrJni->GetArrayLength( jintLevels )== 4 );		// {GPU MIN, GPU MAX, CPU MIN, CPU MAX}

	jint * levels = VrJni->GetIntArrayElements( jintLevels, NULL );
	if ( levels != NULL )
	{
		// Verify levels are within appropriate range for the device
		if ( cpuLevel >= 0 )
		{
			OVR_ASSERT( cpuLevel >= levels[LEVEL_CPU_MIN] && cpuLevel <= levels[LEVEL_CPU_MAX] );
			LOG( "CPU levels [%d, %d]", levels[LEVEL_CPU_MIN], levels[LEVEL_CPU_MAX] );
		}
		if ( gpuLevel >= 0 )
		{
			OVR_ASSERT( gpuLevel >= levels[LEVEL_GPU_MIN] && gpuLevel <= levels[LEVEL_GPU_MAX] );
			LOG( "GPU levels [%d, %d]", levels[LEVEL_GPU_MIN], levels[LEVEL_GPU_MAX] );
		}

		VrJni->ReleaseIntArrayElements( jintLevels, levels, 0 );
	}
	VrJni->DeleteLocalRef( jintLevels );

	// Set the fixed cpu and gpu clock levels
	const jmethodID setSystemPerformanceId = ovr_GetStaticMethodID( VrJni, vrActivityClass,
			"setSystemPerformanceStatic", "(Landroid/app/Activity;II)[I" );

	jintArray jintClocks = (jintArray)VrJni->CallStaticObjectMethod( vrActivityClass, setSystemPerformanceId,
			activityObject, cpuLevel, gpuLevel );

	OVR_ASSERT( VrJni->GetArrayLength( jintClocks ) == 4 );		//  {CPU CLOCK, GPU CLOCK, POWERSAVE CPU CLOCK, POWERSAVE GPU CLOCK}

	jint * clocks = VrJni->GetIntArrayElements( jintClocks, NULL );
	if ( clocks != NULL )
	{
		const int64_t cpuUnit = ( ( NervGear::EglGetGpuType() & NervGear::GPU_TYPE_MALI ) != 0 ) ? 1000LL : 1000LL;
		const int64_t gpuUnit = ( ( NervGear::EglGetGpuType() & NervGear::GPU_TYPE_MALI ) != 0 ) ? 1000LL : 1LL;
		const int64_t cpuFreq = clocks[CLOCK_CPU_FREQ];
		const int64_t gpuFreq = clocks[CLOCK_GPU_FREQ];
		//const int64_t powerSaveCpuMhz = clocks[CLOCK_POWERSAVE_CPU_FREQ];
		//const int64_t powerSaveGpuMhz = clocks[CLOCK_POWERSAVE_GPU_FREQ];

		LOG( "CPU Clock = %lld MHz", cpuFreq * cpuUnit / 1000000 );
		LOG( "GPU Clock = %lld MHz", gpuFreq * gpuUnit / 1000000 );

		// NOTE: If provided an invalid matrix combination, the system will not
		// acknowledge the request and clock settings will go into dynamic mode.
		// Warn when we've encountered this failure condition.
		if ( ( ( cpuLevel != DYNAMIC_MODE_LEVEL ) && ( cpuFreq == INVALID_CLOCK_LEVEL ) ) || ( gpuFreq == INVALID_CLOCK_LEVEL ) )
		{
			// NOTE: Mali does not return proper values
			if ( ( NervGear::EglGetGpuType() & NervGear::GPU_TYPE_MALI ) == 0 )
			{
				OVR_ASSERT( 0 );
				const char * cpuValid = ( ( cpuFreq == INVALID_CLOCK_LEVEL ) ) ? "denied" : "accepted";
				const char * gpuValid = ( ( gpuFreq == INVALID_CLOCK_LEVEL ) ) ? "denied" : "accepted";
				WARN( "WARNING: Invalid clock level combination for this device (cpu:%d=%s,gpu:%d=%s), forcing dynamic mode on",
					cpuLevel, cpuValid, gpuLevel, gpuValid );
			}
		}

		VrJni->ReleaseIntArrayElements( jintClocks, clocks, 0 );
	}
	VrJni->DeleteLocalRef( jintClocks );

	for ( int i = 0; i < 4; i++ )
	{
		WriteFreq( 1200000, "/sys/devices/system/cpu/cpu%i/cpufreq/scaling_min_freq", i );
	}
	for ( int i = 4; i < 8; i++ )
	{
		WriteFreq( 1300000, "/sys/devices/system/cpu/cpu%i/cpufreq/scaling_min_freq", i );
	}
}*/

/*
 * Enter (-1,-1) to release the current clock levels.
 */
static void ReleaseVrSystemPerformance( JNIEnv * VrJni, jclass vrActivityClass, jobject activityObject )
{
	// Clear any previous exceptions.
	// NOTE: This can be removed once security exception handling is moved to Java IF.
	if ( VrJni->ExceptionOccurred() )
	{
		VrJni->ExceptionClear();
		LOG( "ReleaseVrSystemPerformance: Enter: JNI Exception occurred" );
	}

	LOG( "ReleaseVrSystemPerformance" );

	// Release the fixed cpu and gpu clock levels
	const jmethodID releaseSystemPerformanceId = JniUtils::GetStaticMethodID( VrJni, vrActivityClass,
			"releaseSystemPerformanceStatic", "(Landroid/app/Activity;)V" );
	VrJni->CallStaticVoidMethod( vrActivityClass, releaseSystemPerformanceId, activityObject );
}

static void UpdateHmdInfo( ovrMobile * ovr )
{
	short hmdVendorId = 0;
	short hmdProductId = 0;
	unsigned int hmdVersion = 0;

	// There is no sensor when entering VR mode before the device is docked.
	// Note that this may result in the wrong HMD info if GetDeviceHmdInfo()
	// needs the vendor/product id to identify the HMD.
	if ( OvrHmdState != NULL )
	{
		const NervGear::SensorInfo si = OvrHmdState->sensorInfo();
		hmdVendorId = si.VendorId;
		hmdProductId = si.ProductId;
		hmdVersion = si.Version;	// JDC: needed to disambiguate Samsung headsets
	}

	LOG( "VendorId = %i", hmdVendorId );
	LOG( "ProductId = %i", hmdProductId );
	LOG( "Version = %i", hmdVersion );

    const char *model = VOsBuild::getString(VOsBuild::Model).toCString();
    ovr->HmdInfo = NervGear::GetDeviceHmdInfo(model, ovr->Jni, ovr->Parms.ActivityObject, VrLibClass);
    delete[] model;

	// Update the dimensions in pixels directly from the window
	ovr->HmdInfo.widthPixels = windowSurfaceWidth;
	ovr->HmdInfo.heightPixels = windowSurfaceHeight;

	LOG( "hmdInfo.lensSeparation = %f", ovr->HmdInfo.lensSeparation );
	LOG( "hmdInfo.widthMeters = %f", ovr->HmdInfo.widthMeters );
	LOG( "hmdInfo.heightMeters = %f", ovr->HmdInfo.heightMeters );
	LOG( "hmdInfo.widthPixels = %i", ovr->HmdInfo.widthPixels );
	LOG( "hmdInfo.heightPixels = %i", ovr->HmdInfo.heightPixels );
	LOG( "hmdInfo.eyeTextureResolution[0] = %i", ovr->HmdInfo.eyeTextureResolution[0] );
	LOG( "hmdInfo.eyeTextureResolution[1] = %i", ovr->HmdInfo.eyeTextureResolution[1] );
	LOG( "hmdInfo.eyeTextureFov[0] = %f", ovr->HmdInfo.eyeTextureFov[0] );
	LOG( "hmdInfo.eyeTextureFov[1] = %f", ovr->HmdInfo.eyeTextureFov[1] );
}

int ovr_GetSystemBrightness( ovrMobile * ovr )
{
	int cur = 255;
	jmethodID getSysBrightnessMethodId = JniUtils::GetStaticMethodID( ovr->Jni, VrLibClass, "getSystemBrightness", "(Landroid/app/Activity;)I" );
    if (getSysBrightnessMethodId != NULL && VOsBuild::getString(VOsBuild::Model).icompare("SM-G906S") != 0) {
		cur = ovr->Jni->CallStaticIntMethod( VrLibClass, getSysBrightnessMethodId, ovr->Parms.ActivityObject );
		LOG( "System brightness = %i", cur );
	}
	return cur;
}

void ovr_SetSystemBrightness(  ovrMobile * ovr, int const v )
{
	int v2 = v < 0 ? 0 : v;
	v2 = v2 > 255 ? 255 : v2;
	jmethodID setSysBrightnessMethodId = JniUtils::GetStaticMethodID( ovr->Jni, VrLibClass, "setSystemBrightness", "(Landroid/app/Activity;I)V" );
    if (setSysBrightnessMethodId != NULL && VOsBuild::getString(VOsBuild::Model).icompare("SM-G906S") != 0) {
		ovr->Jni->CallStaticVoidMethod( VrLibClass, setSysBrightnessMethodId, ovr->Parms.ActivityObject, v2 );
		LOG( "Set brightness to %i", v2 );
		ovr_GetSystemBrightness( ovr );
	}
}

bool ovr_GetComfortModeEnabled( ovrMobile * ovr )
{
	jmethodID getComfortViewModeMethodId = JniUtils::GetStaticMethodID( ovr->Jni, VrLibClass, "getComfortViewModeEnabled", "(Landroid/app/Activity;)Z" );
	bool r = true;
    if ( getComfortViewModeMethodId != NULL && VOsBuild::getString(VOsBuild::Model).icompare("SM-G906S") != 0)
	{
		r = ovr->Jni->CallStaticBooleanMethod( VrLibClass, getComfortViewModeMethodId, ovr->Parms.ActivityObject );
		LOG( "System comfort mode = %s", r ? "true" : "false" );
	}
	return r;
}

void ovr_SetComfortModeEnabled( ovrMobile * ovr, bool const enabled )
{
	jmethodID enableComfortViewModeMethodId = JniUtils::GetStaticMethodID( ovr->Jni, VrLibClass, "enableComfortViewMode", "(Landroid/app/Activity;Z)V" );
    if ( enableComfortViewModeMethodId != NULL && VOsBuild::getString(VOsBuild::Model).icompare("SM-G906S") != 0)
	{
		ovr->Jni->CallStaticVoidMethod( VrLibClass, enableComfortViewModeMethodId, ovr->Parms.ActivityObject, enabled );
		LOG( "Set comfort mode to %s", enabled ? "true" : "false" );
		ovr_GetComfortModeEnabled( ovr );
	}
}

void ovr_SetDoNotDisturbMode( ovrMobile * ovr, bool const enabled )
{
	jmethodID setDoNotDisturbModeMethodId = JniUtils::GetStaticMethodID( ovr->Jni, VrLibClass, "setDoNotDisturbMode", "(Landroid/app/Activity;Z)V" );
    if ( setDoNotDisturbModeMethodId != NULL && VOsBuild::getString(VOsBuild::Model).icompare("SM-G906S") != 0)
	{
		ovr->Jni->CallStaticVoidMethod( VrLibClass, setDoNotDisturbModeMethodId, ovr->Parms.ActivityObject, enabled );
		LOG( "System DND mode = %s", enabled ? "true" : "false" );
	}
}

bool ovr_GetDoNotDisturbMode( ovrMobile * ovr )
{
	bool r = false;
	jmethodID getDoNotDisturbModeMethodId = JniUtils::GetStaticMethodID( ovr->Jni, VrLibClass, "getDoNotDisturbMode", "(Landroid/app/Activity;)Z" );
    if ( getDoNotDisturbModeMethodId != NULL && VOsBuild::getString(VOsBuild::Model).icompare("SM-G906S") != 0)
	{
		r = ovr->Jni->CallStaticBooleanMethod( VrLibClass, getDoNotDisturbModeMethodId, ovr->Parms.ActivityObject );
		LOG( "Set DND mode to %s", r ? "true" : "false" );
	}
	return r;
}

// This can be called before ovr_EnterVrMode() so hybrid apps can tell
// when they need to go to vr mode.
void ovr_RegisterHmtReceivers( JNIEnv * Jni, jobject ActivityObject )
{
	if ( registerHMTReceivers )
	{
		return;
	}
	const jmethodID startProximityReceiverId = JniUtils::GetStaticMethodID( Jni, ProximityReceiverClass,
			"startReceiver", "(Landroid/app/Activity;)V" );
	Jni->CallStaticVoidMethod( ProximityReceiverClass, startProximityReceiverId, ActivityObject );

	const jmethodID startDockReceiverId = JniUtils::GetStaticMethodID( Jni, DockReceiverClass,
			"startDockReceiver", "(Landroid/app/Activity;)V" );
	Jni->CallStaticVoidMethod( DockReceiverClass, startDockReceiverId, ActivityObject );

	const jmethodID startConsoleReceiverId = JniUtils::GetStaticMethodID( Jni, ConsoleReceiverClass,
			"startReceiver", "(Landroid/app/Activity;)V" );
	Jni->CallStaticVoidMethod( ConsoleReceiverClass, startConsoleReceiverId, ActivityObject );

	registerHMTReceivers = true;
}

// Starts up TimeWarp, vsync tracking, sensor reading, clock locking, and sets video options.
// Should be called when the app is both resumed and has a window surface.
// The application must have their preferred OpenGL ES context current so the correct
// version and config ccan be configured for the background TimeWarp thread.
// On return, the context will be current on an invisible pbuffer, because TimeWarp
// will own the window.
ovrMobile * ovr_EnterVrMode( ovrModeParms parms, ovrHmdInfo * returnedHmdInfo )
{
	LOG( "---------- ovr_EnterVrMode ----------" );
#if defined( OVR_BUILD_DEBUG )
	char const * buildConfig = "DEBUG";
#else
	char const * buildConfig = "RELEASE";
#endif

	// This returns the existing jni if the caller has already created
	// one, or creates a new one.
	JNIEnv	* Jni = NULL;
	const jint rtn = VrLibJavaVM->AttachCurrentThread( &Jni, 0 );
	if ( rtn != JNI_OK )
	{
		FAIL( "AttachCurrentThread returned %i", rtn );
	}

	// log the application name, version, activity, build, model, etc.
	jmethodID logApplicationNameMethodId = JniUtils::GetStaticMethodID( Jni, VrLibClass, "logApplicationName", "(Landroid/app/Activity;)V" );
	Jni->CallStaticVoidMethod( VrLibClass, logApplicationNameMethodId, parms.ActivityObject );

	jmethodID logApplicationVersionId = JniUtils::GetStaticMethodID( Jni, VrLibClass, "logApplicationVersion", "(Landroid/app/Activity;)V" );
	Jni->CallStaticVoidMethod( VrLibClass, logApplicationVersionId, parms.ActivityObject );

	jmethodID logApplicationVrType = JniUtils::GetStaticMethodID( Jni, VrLibClass, "logApplicationVrType", "(Landroid/app/Activity;)V" );
	Jni->CallStaticVoidMethod( VrLibClass, logApplicationVrType, parms.ActivityObject );

    VString currentClassName = JniUtils::GetCurrentActivityName(Jni, parms.ActivityObject);
    vInfo("ACTIVITY =" << currentClassName);

    vInfo("BUILD =" << VOsBuild::getString(VOsBuild::Display) << buildConfig);
    vInfo("MODEL =" << VOsBuild::getString(VOsBuild::Model));
	LOG( "OVR_VERSION = %s", ovr_GetVersionString() );
	LOG( "ovrModeParms.AsynchronousTimeWarp = %i", parms.AsynchronousTimeWarp );
	LOG( "ovrModeParms.AllowPowerSave = %i", parms.AllowPowerSave );
	LOG( "ovrModeParms.DistortionFileName = %s", parms.DistortionFileName ? parms.DistortionFileName : "" );
	LOG( "ovrModeParms.GameThreadTid = %i", parms.GameThreadTid );
	LOG( "ovrModeParms.CpuLevel = %i", parms.CpuLevel );
	LOG( "ovrModeParms.GpuLevel = %i", parms.GpuLevel );

	ovrMobile * ovr = new ovrMobile;
	ovr->Jni = Jni;
	ovr->EnterTid = gettid();
	ovr->Parms = parms;
	ovr->Destroyed = false;

	ovrSensorState state = ovr_GetSensorStateInternal( ovr_GetTimeInSeconds() );
	if ( state.Status & ovrStatus_OrientationTracked )
	{
		LOG( "HMD sensor attached.");
	}
	else
	{
		WARN( "Operating without a sensor.");
	}

	// Let GlUtils look up extensions
    VGlOperation glOperation;
    glOperation.GL_FindExtensions();

	// Look up the window surface size (NOTE: This must happen before Direct Render
	// Mode is initiated and the pbuffer surface is bound).
	{
		EGLDisplay display = eglGetDisplay( EGL_DEFAULT_DISPLAY );
		EGLSurface surface = eglGetCurrentSurface( EGL_DRAW );
		eglQuerySurface( display, surface, EGL_WIDTH, &windowSurfaceWidth );
		eglQuerySurface( display, surface, EGL_HEIGHT, &windowSurfaceHeight );
		LOG( "Window Surface Size: [%dx%d]", windowSurfaceWidth, windowSurfaceHeight );
	}

	// Based on sensor ID and platform, determine the HMD
	UpdateHmdInfo( ovr );

	// Start up our vsync callbacks.
	const jmethodID startVsyncId = JniUtils::GetStaticMethodID( ovr->Jni, VrLibClass,
    		"startVsync", "(Landroid/app/Activity;)V" );
	ovr->Jni->CallStaticVoidMethod( VrLibClass, startVsyncId, ovr->Parms.ActivityObject );

	// Register our HMT receivers if they have not already been registered.
	ovr_RegisterHmtReceivers( ovr->Jni, ovr->Parms.ActivityObject );

	// Register our receivers
	const jmethodID startReceiversId = JniUtils::GetStaticMethodID( ovr->Jni, VrLibClass,
    		"startReceivers", "(Landroid/app/Activity;)V" );
	ovr->Jni->CallStaticVoidMethod( VrLibClass, startReceiversId, ovr->Parms.ActivityObject );

	getPowerLevelStateID = JniUtils::GetStaticMethodID( ovr->Jni, VrLibClass, "getPowerLevelState", "(Landroid/app/Activity;)I" );
	setActivityWindowFullscreenID = JniUtils::GetStaticMethodID( ovr->Jni, VrLibClass, "setActivityWindowFullscreen", "(Landroid/app/Activity;)V" );
	notifyMountHandledID = JniUtils::GetStaticMethodID( ovr->Jni, VrLibClass, "notifyMountHandled", "(Landroid/app/Activity;)V" );

	// get external storage directory
	const jmethodID getExternalStorageDirectoryMethodId = JniUtils::GetStaticMethodID( ovr->Jni, VrLibClass, "getExternalStorageDirectory", "()Ljava/lang/String;" );
	jstring externalStorageDirectoryString = (jstring)ovr->Jni->CallStaticObjectMethod( VrLibClass, getExternalStorageDirectoryMethodId );
    VString externalStorageDirectory = JniUtils::Convert(ovr->Jni, externalStorageDirectoryString);
    ovr->Jni->DeleteLocalRef(externalStorageDirectoryString);

	// Enable cpu and gpu clock locking
	SetVrSystemPerformance( ovr->Jni, VrLibClass, ovr->Parms.ActivityObject,
			ovr->Parms.CpuLevel, ovr->Parms.GpuLevel );

	if ( ovr->Jni->ExceptionOccurred() )
	{
		ovr->Jni->ExceptionClear();
		LOG( "Cleared JNI exception" );
	}

	ovr->Warp = new VFrameSmooth( ovr->Parms.AsynchronousTimeWarp,ovr->HmdInfo);

	// reset brightness, DND and comfort modes because VRSVC, while maintaining the value, does not actually enforce these settings.
	int brightness = ovr_GetSystemBrightness( ovr );
	ovr_SetSystemBrightness( ovr, brightness );

	bool dndMode = ovr_GetDoNotDisturbMode( ovr );
	ovr_SetDoNotDisturbMode( ovr, dndMode );

	bool comfortMode = ovr_GetComfortModeEnabled( ovr );
	ovr_SetComfortModeEnabled( ovr, comfortMode );

	ovrHmdInfo info = {};
	info.SuggestedEyeResolution[0] = ovr->HmdInfo.eyeTextureResolution[0];
	info.SuggestedEyeResolution[1] = ovr->HmdInfo.eyeTextureResolution[1];
	info.SuggestedEyeFov[0] = ovr->HmdInfo.eyeTextureFov[0];
	info.SuggestedEyeFov[1] = ovr->HmdInfo.eyeTextureFov[1];

	*returnedHmdInfo = info;

	double WAIT_AFTER_ENTER_VR_MODE_TIME = 5.0;
	startPowerLevelCheckTime = ovr_GetTimeInSeconds() + WAIT_AFTER_ENTER_VR_MODE_TIME;

	/*

	PERFORMANCE WARNING

	Upon return from the platform activity, an additional full-screen
	HWC layer with the fully-qualified activity name would be active in
	the SurfaceFlinger compositor list, consuming nearly a GB/s of bandwidth.

	"adb shell dumpsys SurfaceFlinger":

	HWC			| b84364f0 | 00000002 | 00000000 | 00 | 00100 | 00000001 | [    0.0,    0.0, 1440.0, 2560.0] | [    0,    0, 1440, 2560] SurfaceView
	HWC			| b848e020 | 00000002 | 00000000 | 00 | 00105 | 00000001 | [    0.0,    0.0, 1440.0, 2560.0] | [    0,    0, 1440, 2560] com.oculusvr.vrcene/com.oculusvr.vrscene.MainActivity
	FB TARGET	| b843d6f0 | 00000000 | 00000000 | 00 | 00105 | 00000001 | [    0.0,    0.0, 1440.0, 2560.0] | [    0,    0, 1440, 2560] HWC_FRAMEBUFFER_TARGET

	The additional layer is tied to the decor view of the activity
	and is normally not visible. The decor view becomes visible because
	the SurfaceView window is no longer fullscreen after returning
	from the platform activity.

	By always setting the SurfaceView window as full screen, the decor view
	will not be rendered and won't waste any precious bandwidth.

	*/
	// TODO_PLATFORMUI - once platformactivity is in it's own apk, remove setActivityWindowFullscreen functionality.
	if ( setActivityWindowFullscreenID != NULL && !ovr->Parms.SkipWindowFullscreenReset )
	{
		ovr->Jni->CallStaticVoidMethod( VrLibClass, setActivityWindowFullscreenID, ovr->Parms.ActivityObject );
	}

	return ovr;
}

void ovr_notifyMountHandled( ovrMobile * ovr )
{
	if ( ovr == NULL )
	{
		return;
	}

	if ( notifyMountHandledID != NULL )
	{
		ovr->Jni->CallStaticVoidMethod( VrLibClass, notifyMountHandledID, ovr->Parms.ActivityObject );
	}
}

// Should be called before the window is destroyed.
void ovr_LeaveVrMode( ovrMobile * ovr )
{
	LOG( "---------- ovr_LeaveVrMode ----------" );

	if ( ovr == NULL )
	{
		WARN( "NULL ovr" );
		return;
	}

	if ( ovr->Destroyed )
	{
		WARN( "Skipping ovr_LeaveVrMode: ovr already Destroyed" );
		return;
	}

	if ( gettid() != ovr->EnterTid )
	{
		FAIL( "ovr_LeaveVrMode: Called with tid %i instead of %i", gettid(), ovr->EnterTid );
	}

	// log the application name, version, activity, build, model, etc.
	jmethodID logApplicationNameMethodId = JniUtils::GetStaticMethodID( ovr->Jni, VrLibClass, "logApplicationName", "(Landroid/app/Activity;)V" );
	ovr->Jni->CallStaticVoidMethod( VrLibClass, logApplicationNameMethodId, ovr->Parms.ActivityObject );

	jmethodID logApplicationVersionId = JniUtils::GetStaticMethodID( ovr->Jni, VrLibClass, "logApplicationVersion", "(Landroid/app/Activity;)V" );
	ovr->Jni->CallStaticVoidMethod( VrLibClass, logApplicationVersionId, ovr->Parms.ActivityObject );

    VString currentClassName = JniUtils::GetCurrentActivityName(ovr->Jni, ovr->Parms.ActivityObject);
    vInfo("ACTIVITY =" << currentClassName);

	delete ovr->Warp;
	ovr->Warp = 0;

	// This must be after pushing the textures to timewarp.
	ovr->Destroyed = true;

	getPowerLevelStateID = NULL;

	// Always release clock locks.
	ReleaseVrSystemPerformance( ovr->Jni, VrLibClass, ovr->Parms.ActivityObject );

	// Stop our vsync callbacks.
	const jmethodID stopVsyncId = JniUtils::GetStaticMethodID( ovr->Jni, VrLibClass,
			"stopVsync", "(Landroid/app/Activity;)V" );
	ovr->Jni->CallStaticVoidMethod( VrLibClass, stopVsyncId, ovr->Parms.ActivityObject );

	// Unregister our receivers
#if 1
	const jmethodID stopReceiversId = JniUtils::GetStaticMethodID( ovr->Jni, VrLibClass,
			"stopReceivers", "(Landroid/app/Activity;)V" );
	ovr->Jni->CallStaticVoidMethod( VrLibClass, stopReceiversId, ovr->Parms.ActivityObject );
#else
	const jmethodID stopCellularReceiverId = ovr_GetStaticMethodID( ovr->Jni, VrLibClass,
			"stopCellularReceiver", "(Landroid/app/Activity;)V" );
	ovr->Jni->CallStaticVoidMethod( VrLibClass, stopCellularReceiverId, ovr->Parms.ActivityObject );

	const jmethodID stopWifiReceiverId = ovr_GetStaticMethodID( ovr->Jni, VrLibClass,
			"stopWifiReceiver", "(Landroid/app/Activity;)V" );
	ovr->Jni->CallStaticVoidMethod( VrLibClass, stopWifiReceiverId, ovr->Parms.ActivityObject );

	const jmethodID stopVolumeReceiverId = ovr_GetStaticMethodID( ovr->Jni, VrLibClass,
			"stopVolumeReceiver", "(Landroid/app/Activity;)V" );
	ovr->Jni->CallStaticVoidMethod( VrLibClass, stopVolumeReceiverId, ovr->Parms.ActivityObject );

	const jmethodID stopBatteryReceiverId = ovr_GetStaticMethodID( ovr->Jni, VrLibClass,
			"stopBatteryReceiver", "(Landroid/app/Activity;)V" );
	ovr->Jni->CallStaticVoidMethod( VrLibClass, stopBatteryReceiverId, ovr->Parms.ActivityObject );

	const jmethodID stopHeadsetReceiverId = ovr_GetStaticMethodID( ovr->Jni, VrLibClass,
			"stopHeadsetReceiver", "(Landroid/app/Activity;)V" );
	ovr->Jni->CallStaticVoidMethod( VrLibClass, stopHeadsetReceiverId, ovr->Parms.ActivityObject );
#endif
}

static void ResetTimeWarp( ovrMobile * ovr )
{
	LOG( "ResetTimeWarp" );

	// restart TimeWarp to generate new distortion meshes
	delete ovr->Warp;
	ovr->Warp = new VFrameSmooth( ovr->Parms.AsynchronousTimeWarp,ovr->HmdInfo);
}

void ovr_HandleHmdEvents( ovrMobile * ovr )
{
	if ( ovr == NULL )
	{
		return;
	}

	// check if the HMT has been undocked
	HMTDockState_t dockState = HMTDockState.state();
	if ( dockState.DockState == HMT_DOCK_UNDOCKED )
	{
		LOG( "ovr_HandleHmdEvents::Hmt was disconnected" );

		// reset the sensor info
		if ( OvrHmdState != NULL )
		{
			OvrHmdState->resetSensor();
		}

		// reset the real dock state since we're handling the change
		HMTDockState.setState( HMTDockState_t( HMT_DOCK_NONE ) );

		// ovr_ExitActivity() with EXIT_TYPE_FINISH_AFFINITY will finish
		// all activities in the stack.
		ovr_ExitActivity( ovr, EXIT_TYPE_FINISH_AFFINITY );

		return;
	}

	// check if the HMT has been mounted or unmounted
	HMTMountState_t mountState = HMTMountState.state();
	if ( mountState.MountState != HMT_MOUNT_NONE )
	{
		// reset the real mount state since we're handling the change
		HMTMountState.setState( HMTMountState_t( HMT_MOUNT_NONE ) );

		if ( mountState.MountState == HMT_MOUNT_MOUNTED )
		{
			if ( hmtIsMounted )
			{
				LOG( "ovr_HandleHmtEvents: HMT is already mounted" );
			}
			else
			{
				LOG( "ovr_HandleHmdEvents: HMT was mounted" );
				hmtIsMounted = true;
				alwaysSkipPowerLevelCheck = false;
				double WAIT_AFTER_MOUNT_TIME = 5.0;
				startPowerLevelCheckTime = ovr_GetTimeInSeconds() + WAIT_AFTER_MOUNT_TIME;

				// broadcast to background apps that mount has been handled
				ovr_notifyMountHandled( ovr );

				NervGear::VString reorientMessage;
                CreateSystemActivitiesCommand( "", SYSTEM_ACTIVITY_EVENT_REORIENT, "", reorientMessage );
                NervGear::SystemActivities_AddEvent( reorientMessage );
			}
		}
		else if ( mountState.MountState == HMT_MOUNT_UNMOUNTED )
		{
			LOG( "ovr_HandleHmdEvents: HMT was UNmounted" );

			alwaysSkipPowerLevelCheck = true;
			hmtIsMounted = false;

			// Reset power level state action if waiting for an unmount event
			if ( powerLevelAction == POWERLEVEL_ACTION_WAITING_FOR_UNMOUNT )
			{
				LOG( "powerLevelAction = POWERLEVEL_ACTION_WAITING_FOR_RESET" );
				powerLevelAction = POWERLEVEL_ACTION_WAITING_FOR_RESET;
			}
		}
	}
}

void ovr_HandleDeviceStateChanges( ovrMobile * ovr )
{
	if ( ovr == NULL )
	{
		return;
	}

	// Test for Hmd Events such as mount/unmount, dock/undock
	ovr_HandleHmdEvents( ovr );

	// Check for device power level state changes
	ovr_CheckPowerLevelState( ovr );

	// check for pending events that must be handled natively
	size_t const MAX_EVENT_SIZE = 4096;
//	char eventBuffer[MAX_EVENT_SIZE];
    VString eventBuffer;

	for ( eVrApiEventStatus status = NervGear::SystemActivities_nextPendingInternalEvent( eventBuffer, MAX_EVENT_SIZE );
		status >= VRAPI_EVENT_PENDING;
		status = NervGear::SystemActivities_nextPendingInternalEvent( eventBuffer, MAX_EVENT_SIZE ) )
	{
		if ( status != VRAPI_EVENT_PENDING )
		{
			WARN( "Error %i handing internal System Activities Event", status );
			continue;
		}

        VJson reader = VJson::Parse(eventBuffer.toLatin1());
        if ( reader.isObject() )
		{
            VString command = reader.value("Command").toString();
            int32_t platformUIVersion = reader.value("PlatformUIVersion").toInt();
            if (command == SYSTEM_ACTIVITY_EVENT_REORIENT)
			{
				// for reorient, we recenter yaw natively, then pass the event along so that the client
				// application can also handle the event (for instance, to reposition menus)
				LOG( "ovr_HandleDeviceStateChanges: Acting on System Activity reorient event." );
				ovr_RecenterYawInternal();
			}
            else if (command == SYSTEM_ACTIVITY_EVENT_RETURN_TO_LAUNCHER && platformUIVersion < 2 )
			{
				// In the case of the returnToLauncher event, we always handler it internally and pass
				// along an empty buffer so that any remaining events still get processed by the client.
				LOG( "ovr_HandleDeviceStateChanges: Acting on System Activity returnToLauncher event." );
				// PlatformActivity and Home should NEVER get one of these!
				ovr_ReturnToHome( ovr );
            }
		}
		else
		{
			// a malformed event string was pushed! This implies an error in the native code somewhere.
            WARN( "Error parsing System Activities Event");
		}
	}
}

double ovr_GetPredictedDisplayTime( ovrMobile * ovr, int minimumVsyncs, int pipelineDepth )
{
	if ( ovr == NULL )
	{
		return ovr_GetTimeInSeconds();
	}
	if ( ovr->Destroyed )
	{
		LOG( "ovr_GetPredictedDisplayTime: Returning due to Destroyed" );
		return ovr_GetTimeInSeconds();
	}
	// Handle power throttling the same way ovr_WarpSwap() does.
	const int throttledMinimumVsyncs = ovr_GetPowerLevelStateThrottled() ? 2 : minimumVsyncs;
	const double vsyncBase = floor( NervGear::GetFractionalVsync() );
	const double predictedFrames = (double)throttledMinimumVsyncs * ( (double)pipelineDepth + 0.5 );
	const double predictedVsync = vsyncBase + predictedFrames;
	const double predictedTime = NervGear::FramePointTimeInSeconds( predictedVsync );
	//LOG( "synthesis: vsyncBase = %1.0f, predicted frames = %1.1f, predicted V-sync = %1.1f, predicted time = %1.3f", vsyncBase, predictedFrames, predictedVsync, predictedTime );
	return predictedTime;
}

ovrSensorState ovr_GetPredictedSensorState( ovrMobile * ovr, double absTime )
{
	return ovr_GetSensorStateInternal( absTime );
}

void ovr_RecenterYaw( ovrMobile * ovr )
{
	ovr_RecenterYawInternal();
}

void ovr_WarpSwap( ovrMobile * ovr, const ovrTimeWarpParms * parms )
{
	if ( ovr == NULL )
	{
		return;
	}

	if ( ovr->Warp == NULL )
	{
		return;
	}
	// If we are changing to a new activity, leave the screen black
	if ( ovr->Destroyed )
	{
		LOG( "ovr_WarpSwap: Returning due to Destroyed" );
		return;
	}

	if ( gettid() != ovr->EnterTid )
	{
		FAIL( "ovr_WarpSwap: Called with tid %i instead of %i", gettid(), ovr->EnterTid );
	}

	// Push the new data
	ovr->Warp->doSmooth( *parms );
}

void ovr_SetDistortionTuning( ovrMobile * ovr, NervGear::DistortionEqnType type, float scale, float* distortionK)
{
	// Dynamic adjustment of distortion
	ovr->HmdInfo.lens.Eqn = type;
	ovr->HmdInfo.lens.MetersPerTanAngleAtCenter = scale;
	if ( type == NervGear::Distortion_RecipPoly4 )
	{
		for ( int i = 0; i < 4; i++ )
		{
			ovr->HmdInfo.lens.K[i] = distortionK[i];
		}
	}
	else if ( type == NervGear::Distortion_CatmullRom10 )
	{
		for ( int i = 0; i < 11; i++ )
		{
			ovr->HmdInfo.lens.K[i] = distortionK[i];
		}
	}
	else
	{
		for ( int i = 0; i < ovr->HmdInfo.lens.MaxCoefficients; i++ )
		{
			ovr->HmdInfo.lens.K[i] = distortionK[i];
		}
	}

	ResetTimeWarp( ovr );
}

void ovr_AdjustClockLevels( ovrMobile * ovr, int cpuLevel, int gpuLevel )
{
	if ( ovr == NULL )
	{
		return;
	}

	// release the existing clock locks
	ReleaseVrSystemPerformance( ovr->Jni, VrLibClass, ovr->Parms.ActivityObject );

	ovr->Parms.CpuLevel = cpuLevel;
	ovr->Parms.GpuLevel = gpuLevel;

	// set to new values
	SetVrSystemPerformance( ovr->Jni, VrLibClass, ovr->Parms.ActivityObject,
			ovr->Parms.CpuLevel, ovr->Parms.GpuLevel );
}
/*
 * CreateSchedulingReport
 *
 * Display information related to CPU scheduling
 */
const char * ovr_CreateSchedulingReport( ovrMobile * ovr )
{
	if ( ovr == NULL )
	{
		return "";
	}

	static char report[16384];
	static int reportLength = 0;

	const pthread_t thisThread = pthread_self();
	const pthread_t warpThread = ovr->Warp->warpThread();

	int thisPolicy = 0;
	struct sched_param thisSchedParma = {};
	if ( !pthread_getschedparam( thisThread, &thisPolicy, &thisSchedParma ) )
	{
		WARN( "pthread_getschedparam() failed" );
	}

	reportLength += NervGear::VAlgorithm::Max( 0, snprintf( report + reportLength, sizeof( report ) - reportLength - 1,
			"VrThread:%s:%i WarpThread:\n"
			, thisPolicy == SCHED_FIFO ? "SCHED_FIFO" : "SCED_NORMAL"
			, thisSchedParma.sched_priority ) );

	if ( warpThread != 0 )
	{
		int timeWarpPolicy = 0;
		struct sched_param timeWarpSchedParma = {};
		if ( pthread_getschedparam( warpThread, &timeWarpPolicy, &timeWarpSchedParma ) )
		{
			WARN( "pthread_getschedparam() failed" );
	    	reportLength += NervGear::VAlgorithm::Max( 0, snprintf( report + reportLength, sizeof( report ) - reportLength - 1, "???" ) );
		}
		else
		{
	    	reportLength += NervGear::VAlgorithm::Max( 0, snprintf( report + reportLength, sizeof( report ) - reportLength - 1, "%s:%i"
	    			, timeWarpPolicy == SCHED_FIFO ? "SCHED_FIFO" : "SCED_NORMAL"
	    			, timeWarpSchedParma.sched_priority ) );
		}
	}
	else
	{
		reportLength += NervGear::VAlgorithm::Max( 0, snprintf( report + reportLength, sizeof( report ) - reportLength - 1, "sync" ) );
	}

	static const int maxCores = 8;
	for ( int core = 0; core < maxCores; core++ )
	{
		char path[1024] = {};
		snprintf( path, sizeof( path ) - 1, "/sys/devices/system/cpu/cpu%i/online", core );

		const char * current = ReadSmallFile( path );
		if ( current[0] == 0 )
		{	// no more files
			break;
		}
		const int online = atoi( current );
		if ( !online )
		{
			continue;
		}

		snprintf( path, sizeof( path ) - 1, "/sys/devices/system/cpu/cpu%i/cpufreq/scaling_governor", core );
		NervGear::VString governor = ReadSmallFile( path );
		governor = StripLinefeed( governor );

		// we won't be able to read the cpu clock unless it has been chmod'd to 0666, but try anyway.
		//float curFreqGHz = ReadFreq( "/sys/devices/system/cpu/cpu%i/cpufreq/cpuinfo_cur_freq", core ) * ( 1.0f / 1000.0f / 1000.0f );
		float curFreqGHz = ReadFreq( "/sys/devices/system/cpu/cpu%i/cpufreq/scaling_cur_freq", core ) * ( 1.0f / 1000.0f / 1000.0f );
		float minFreqGHz = ReadFreq( "/sys/devices/system/cpu/cpu%i/cpufreq/cpuinfo_min_freq", core ) * ( 1.0f / 1000.0f / 1000.0f );
		float maxFreqGHz = ReadFreq( "/sys/devices/system/cpu/cpu%i/cpufreq/cpuinfo_max_freq", core ) * ( 1.0f / 1000.0f / 1000.0f );

		reportLength += NervGear::VAlgorithm::Max( 0, snprintf( report + reportLength, sizeof( report ) - reportLength - 1,
									"cpu%i: \"%s\" %1.2f GHz (min:%1.2f max:%1.2f)\n",
									core, governor.toCString(), curFreqGHz, minFreqGHz, maxFreqGHz ) );
	}

	NervGear::VString governor = ReadSmallFile( "/sys/class/kgsl/kgsl-3d0/pwrscale/trustzone/governor" );
	governor = StripLinefeed( governor );

	const uint64_t gpuUnit = ( ( NervGear::EglGetGpuType() & NervGear::GPU_TYPE_MALI ) != 0 ) ? 1000000LL : 1000LL;
	const uint64_t gpuFreq = ReadFreq( ( ( NervGear::EglGetGpuType() & NervGear::GPU_TYPE_MALI ) != 0 ) ?
									"/sys/devices/14ac0000.mali/clock" :
									"/sys/class/kgsl/kgsl-3d0/gpuclk" );

	reportLength += NervGear::VAlgorithm::Max( 0, snprintf( report + reportLength, sizeof( report ) - reportLength - 1,
								"gpu: \"%s\" %3.0f MHz",
								governor.toCString(), gpuFreq * gpuUnit * ( 1.0f / 1000.0f / 1000.0f ) ) );

	return report;
}
int ovr_GetVolume()
{
	return CurrentVolume.state().Value;
}

double ovr_GetTimeSinceLastVolumeChange()
{
	double value = TimeOfLastVolumeChange.state().Value;
	if ( value == -1 )
	{
		//LOG( "ovr_GetTimeSinceLastVolumeChange() : Not initialized.  Returning -1" );
		return -1;
	}
	return ovr_GetTimeInSeconds() - value;
}

void ovr_RequestAudioFocus( ovrMobile * ovr )
{
	if ( ovr == NULL )
	{
		return;
	}

	const jmethodID requestAudioFocusId = JniUtils::GetStaticMethodID( ovr->Jni, VrLibClass,
    		"requestAudioFocus", "(Landroid/app/Activity;)V" );
	ovr->Jni->CallStaticVoidMethod( VrLibClass, requestAudioFocusId, ovr->Parms.ActivityObject );
}

void ovr_ReleaseAudioFocus( ovrMobile * ovr )
{
	if ( ovr == NULL )
	{
		return;
	}

	const jmethodID releaseAudioFocusId = JniUtils::GetStaticMethodID( ovr->Jni, VrLibClass,
			"releaseAudioFocus", "(Landroid/app/Activity;)V" );
	ovr->Jni->CallStaticVoidMethod( VrLibClass, releaseAudioFocusId, ovr->Parms.ActivityObject );
}

int ovr_GetWifiSignalLevel()
{
	return WifiSignalLevel.state().Value;
}

eWifiState ovr_GetWifiState()
{
	return WifiState.state().Value;
}

int ovr_GetCellularSignalLevel()
{
	return CellularSignalLevel.state().Value;
}

eCellularState ovr_GetCellularState()
{
	return CellularState.state().Value;
}

batteryState_t ovr_GetBatteryState()
{
	return BatteryState.state();
}

bool ovr_GetHeadsetPluggedState()
{
	return HeadsetPluggedState.state();
}

bool ovr_IsDeviceDocked()
{
	return DockState.state();
}

eVrApiEventStatus ovr_nextPendingEvent( VString& buffer, unsigned int const bufferSize )
{
	eVrApiEventStatus status = NervGear::SystemActivities_nextPendingMainEvent( buffer, bufferSize );
	if ( status < VRAPI_EVENT_PENDING )
	{
		return status;
	}

	// Parse to JSON here to determine if we should handle the event natively, or pass it along to the client app.
    VJson reader = VJson::Parse(buffer.toLatin1());
    if (reader.isObject())
	{
        VString command = reader.value( "Command" ).toString();
        if (command == SYSTEM_ACTIVITY_EVENT_REORIENT) {
			// for reorient, we recenter yaw natively, then pass the event along so that the client
			// application can also handle the event (for instance, to reposition menus)
			LOG( "Queuing internal reorient event." );
			ovr_RecenterYawInternal();
			// also queue as an internal event
			NervGear::SystemActivities_AddInternalEvent( buffer );
        } else if (command == SYSTEM_ACTIVITY_EVENT_RETURN_TO_LAUNCHER) {
			// In the case of the returnToLauncher event, we always handler it internally and pass
			// along an empty buffer so that any remaining events still get processed by the client.
			LOG( "Queuing internal returnToLauncher event." );
			// queue as an internal event
			NervGear::SystemActivities_AddInternalEvent( buffer );
			// treat as an empty event externally
            buffer = "";
			status = VRAPI_EVENT_CONSUMED;
        }
	}
	else
	{
		// a malformed event string was pushed! This implies an error in the native code somewhere.
        WARN( "Error parsing System Activities Event");
		return VRAPI_EVENT_INVALID_JSON;
	}
	return status;
}

void ovr_ReturnToHome( ovrMobile * ovr )
{
    /*jclass activityClass = ovr->Jni->GetObjectClass( ovr->Parms.ActivityObject );

	if ( !ovr_IsCurrentActivity( ovr->Jni, ovr->Parms.ActivityObject, PUI_CLASS_NAME ) &&
			!ovr_IsOculusHomePackage( ovr->Jni, activityClass, ovr->Parms.ActivityObject ) )
	{
		char homePackageName[128];
		ovr_GetHomePackageName( homePackageName, sizeof( homePackageName ) );
		ovr_SendLaunchIntent( ovr, homePackageName, "", "", EXIT_TYPE_FINISH_AFFINITY );
	}

    ovr->Jni->DeleteLocalRef( activityClass );*/
    ovr_ExitActivity(ovr, EXIT_TYPE_FINISH_AFFINITY);
}

VString ovr_GetCurrentLanguage(ovrMobile * ovr)
{
    jmethodID getCurrentLanguageMethodId = JniUtils::GetStaticMethodID(ovr->Jni, VrLibClass, "getCurrentLanguage", "()Ljava/lang/String;");
    if (getCurrentLanguageMethodId != NULL) {
        VString language = JniUtils::Convert(ovr->Jni, (jstring) ovr->Jni->CallStaticObjectMethod(VrLibClass, getCurrentLanguageMethodId));
        if (!ovr->Jni->ExceptionOccurred()) {
            return language;
		}
	}
    return VString();
}

#include "embedded/dependency_error_de.h"
#include "embedded/dependency_error_en.h"
#include "embedded/dependency_error_es.h"
#include "embedded/dependency_error_fr.h"
#include "embedded/dependency_error_it.h"
#include "embedded/dependency_error_ja.h"
#include "embedded/dependency_error_ko.h"


struct embeddedImage_t
{
	char const *	ImageName;
	void *			ImageBuffer;
	size_t			ImageSize;
};

// for each error type, add an array of errorImage_t with an entry for each language
embeddedImage_t EmbeddedImages[] =
{
	{ "dependency_error_en.png",		dependencyErrorEnData,		dependencyErrorEnSize },
	{ "dependency_error_de.png",		dependencyErrorDeData,		dependencyErrorDeSize },
	{ "dependency_error_en-rGB.png",	dependencyErrorEnData,		dependencyErrorEnSize },
	{ "dependency_error_es.png",		dependencyErrorEsData,		dependencyErrorEsSize },
	{ "dependency_error_es-rES.png",	dependencyErrorEsData,		dependencyErrorEsSize },
	{ "dependency_error_fr.png",		dependencyErrorFrData,		dependencyErrorFrSize },
	{ "dependency_error_it.png",		dependencyErrorItData,		dependencyErrorItSize },
	{ "dependency_error_ja.png",		dependencyErrorJaData,		dependencyErrorJaSize },
	{ "dependency_error_ko.png",		dependencyErrorKoData,		dependencyErrorKoSize },
	{ NULL, NULL, 0 }
};

embeddedImage_t const * FindErrorImage( embeddedImage_t const * list, char const * name )
{
	for ( int i = 0; list[i].ImageName != NULL; ++i )
	{
        if ( strcasecmp( list[i].ImageName, name ) == 0 )
        {
			LOG( "Found embedded image for '%s'", name );
			return &list[i];
		}
	}

	return NULL;
}

bool ovr_FindEmbeddedImage( ovrMobile * ovr, char const * imageName, void * & buffer, int & bufferSize )
{
	buffer = NULL;
	bufferSize = 0;

	embeddedImage_t const * image = FindErrorImage( EmbeddedImages, imageName );
	if ( image == NULL )
	{
		WARN( "No embedded image named '%s' was found!", imageName );
	}

	buffer = image->ImageBuffer;
	bufferSize = image->ImageSize;
	return true;
}
