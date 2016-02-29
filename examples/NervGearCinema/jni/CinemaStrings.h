#if !defined( CinemaStrings_h )
#define CinemaStrings_h

#include <VString.h>

using namespace NervGear;

namespace OculusCinema {

class CinemaApp;

class CinemaStrings {
public:
	static void		OneTimeInit( CinemaApp &cinema );

	static String	LoadingMenu_Title;

	static String	Category_Trailers;
	static String	Category_MyVideos;

	static String	MovieSelection_Resume;
	static String	MovieSelection_Next;

	static String	ResumeMenu_Title;
	static String	ResumeMenu_Resume;
	static String	ResumeMenu_Restart;

	static String	TheaterSelection_Title;

	static String	Error_NoVideosOnPhone;
	static String	Error_NoVideosInMyVideos;
	static String	Error_UnableToPlayMovie;

	static String	MoviePlayer_Reorient;
};

} // namespace OculusCinema

#endif // CinemaStrings_h
