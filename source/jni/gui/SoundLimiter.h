/************************************************************************************

Filename    :   SoundLimiter.cpp
Content     :   Utility class for limiting how often sounds play.
Created     :   June 23, 2014
Authors     :   Jonathan E. Wright

Copyright   :   Copyright 2014 Oculus VR, LLC. All Rights reserved.


*************************************************************************************/

#if !defined( OVR_SoundLimiter_h )
#define OVR_SoundLimiter_h

namespace NervGear {

//==============================================================
// SoundLimiter
class SoundLimiter
{
public:
	SoundLimiter() :
        m_lastPlayTime( 0 )
	{
	}

    void			playSound( class App * app, char const * soundName, double const limitSeconds );
	// Checks if menu specific sounds exists before playing the default vrlib sound passed in
    void			playMenuSound( class App * app,  char const * menuName, char const * soundName, double const limitSeconds );

private:
    double			m_lastPlayTime;
};

} // namespace NervGear

#endif // OVR_SoundLimiter_h
