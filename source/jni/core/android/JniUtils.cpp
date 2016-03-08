#include "JniUtils.h"

#include "Std.h"
#include "VJson.h"
#include "VLog.h"

#include <fstream>

NV_NAMESPACE_BEGIN

namespace JniUtils {
    VString Convert(JNIEnv *jni, jstring jstr)
    {
        VString str;
        jsize length = jni->GetStringLength(jstr);
        if (length > 0) {
            str.resize(length);
            const jchar *chars = jni->GetStringChars(jstr, NULL);
            for (jsize i = 0; i < length; i++) {
                str[i] = chars[i];
            }
            jni->ReleaseStringChars(jstr, chars);
        }
        return str;
    }

    jstring Convert(JNIEnv *jni, const VString &str)
    {
        jsize len = str.size();
        jchar *chars = new jchar[len];
        jstring jstr = jni->NewString(chars, len);
        delete[] chars;
        return jstr;
    }

    VString GetPackageCodePath(JNIEnv *jni, jclass activityClass, jobject activityObject)
    {
        jmethodID getPackageCodePathId = jni->GetMethodID( activityClass, "getPackageCodePath", "()Ljava/lang/String;" );
        if (getPackageCodePathId == 0) {
            vInfo("Failed to find getPackageCodePath on class" << (ulonglong) activityClass << ", object" << (ulonglong) activityObject);
            return VString();
        }

        VString packageCodePath = Convert(jni, (jstring) jni->CallObjectMethod(activityObject, getPackageCodePathId));
        if (!jni->ExceptionOccurred()) {
            vInfo("ovr_GetPackageCodePath() =" << packageCodePath);
            return packageCodePath;
        } else {
            jni->ExceptionClear();
            vInfo("Cleared JNI exception");
        }

        return VString();
    }

    VString GetCurrentPackageName(JNIEnv * jni, jobject activityObject)
    {
        JavaClass curActivityClass( jni, jni->GetObjectClass( activityObject ) );
        jmethodID getPackageNameId = jni->GetMethodID( curActivityClass.GetJClass(), "getPackageName", "()Ljava/lang/String;");
        if ( getPackageNameId != 0 )
        {
            VString packageName = Convert(jni, (jstring) jni->CallObjectMethod(activityObject, getPackageNameId));
            if (!jni->ExceptionOccurred()) {
                vInfo("ovr_GetCurrentPackageName() =" << packageName);
                return packageName;
            } else {
                jni->ExceptionClear();
                vInfo("Cleared JNI exception");
            }
        }
        return VString();
    }
}

NV_NAMESPACE_END

jobject ovr_NewStringUTF( JNIEnv * jni, char const * str )
{
	//if ( strstr( str, "com.oculus.home" ) != NULL || strstr( str, "globalMenu" ) != NULL ) {
	//	DROIDLOG( "OvrJNI", "ovr_NewStringUTF: These are not the strings your looking for: %s", str );
	//}
	return jni->NewStringUTF( str );
}

char const * ovr_GetStringUTFChars( JNIEnv * jni, jstring javaStr, jboolean * isCopy )
{
	char const * str = jni->GetStringUTFChars( javaStr, isCopy );
	//if ( strstr( str, "com.oculus.home" ) != NULL || strstr( str, "globalMenu" ) != NULL ) {
	//	DROIDLOG( "OvrJNI", "ovr_GetStringUTFChars: These are not the strings your looking for: %s", str );
	//}
	return str;
}

jclass ovr_GetGlobalClassReference( JNIEnv * jni, const char * className )
{
	jclass lc = jni->FindClass(className);
	if ( lc == 0 )
	{
		FAIL( "FindClass( %s ) failed", className );
	}
	// Turn it into a global ref, so we can safely use it in other threads
	jclass gc = (jclass)jni->NewGlobalRef( lc );

	jni->DeleteLocalRef( lc );

	return gc;
}

jmethodID ovr_GetMethodID( JNIEnv * jni, jclass jniclass, const char * name, const char * signature )
{
	const jmethodID methodId = jni->GetMethodID( jniclass, name, signature );
	if ( !methodId )
	{
		FAIL( "couldn't get %s, %s", name, signature );
	}
	return methodId;
}

jmethodID ovr_GetStaticMethodID( JNIEnv * jni, jclass jniclass, const char * name, const char * signature )
{
	const jmethodID method = jni->GetStaticMethodID( jniclass, name, signature );
	if ( !method )
	{
		FAIL( "couldn't get %s, %s", name, signature );
	}
	return method;
}

const char * ovr_GetCurrentActivityName( JNIEnv * jni, jobject activityObject, char * className, int const maxLen )
{
	className[0] = '\0';

	JavaClass curActivityClass( jni, jni->GetObjectClass( activityObject ) );
	jmethodID getClassMethodId = jni->GetMethodID( curActivityClass.GetJClass(), "getClass", "()Ljava/lang/Class;" );
	if ( getClassMethodId != 0 )
	{
		JavaObject classObj( jni, jni->CallObjectMethod( activityObject, getClassMethodId ) );
		JavaClass activityClass( jni, jni->GetObjectClass( classObj.GetJObject() ) );

		jmethodID getNameMethodId = jni->GetMethodID( activityClass.GetJClass(), "getName", "()Ljava/lang/String;" );
		if ( getNameMethodId != 0 )
		{
			JavaUTFChars utfCurrentClassName( jni, (jstring)jni->CallObjectMethod( classObj.GetJObject(), getNameMethodId ) );
			const char * currentClassName = utfCurrentClassName.ToStr();
			if ( currentClassName != NULL )
			{
				NervGear::OVR_sprintf( className, maxLen, "%s", currentClassName );
			}
		}
}

	LOG( "ovr_GetCurrentActivityName() = %s", className );
	return className;
}

bool ovr_IsCurrentActivity( JNIEnv * jni, jobject activityObject, const char * className )
{
    char currentClassName[128];
    ovr_GetCurrentActivityName( jni, activityObject, currentClassName, sizeof( currentClassName ) );
	const bool isCurrentActivity = ( NervGear::OVR_stricmp( currentClassName, className ) == 0 );
	LOG( "ovr_IsCurrentActivity( %s ) = %s", className, isCurrentActivity ? "true" : "false" );
	return isCurrentActivity;
}

static NervGear::Json *DevConfig = NULL;

void ovr_LoadDevConfig( bool const forceReload )
{
#if !defined( RETAIL )
	if ( DevConfig != NULL )
	{
		if ( !forceReload )
		{
			return;	// already loading and not forcing a reload
		}
		delete DevConfig;
		DevConfig = NULL;
	}

	// load the dev config if possible
	std::ifstream fp("/storage/extSdCard/Oculus/dev.cfg", std::ios::binary);
	if (fp.is_open())
	{
		DevConfig = new NervGear::Json;
		fp >> (*DevConfig);
	}
#endif
}

//#define DEFAULT_DASHBOARD_PACKAGE_NAME "com.Oculus.UnitySample" -- for testing only
#define DEFAULT_DASHBOARD_PACKAGE_NAME "com.oculus.home"

const char * ovr_GetHomePackageName( char * packageName, int const maxLen )
{
#if defined( RETAIL )
	NervGear::OVR_sprintf( packageName, maxLen, "%s", DEFAULT_DASHBOARD_PACKAGE_NAME );
	return packageName;
#else
	// make sure the dev config is loaded
	ovr_LoadDevConfig( false );

	// set to default value
	NervGear::OVR_sprintf( packageName, maxLen, "%s", DEFAULT_DASHBOARD_PACKAGE_NAME );

	if ( DevConfig != NULL )
	{
		// try to get it from the Launcher/Packagename JSON entry
		NervGear::Json jsonLauncher = DevConfig->value( "Launcher" );
		if ( jsonLauncher.type() != NervGear::Json::None )
		{
			NervGear::Json jsonPackageName = jsonLauncher.value( "PackageName" );
			if ( jsonPackageName.type() != NervGear::Json::None )
			{
				NervGear::OVR_sprintf( packageName, maxLen, "%s", jsonPackageName.toString().c_str() );
				LOG( "Found Home package name: '%s'", packageName );
			}
			else
			{
				LOG( "No override for Home package name found." );
			}
		}
	}
	return packageName;
#endif
}
