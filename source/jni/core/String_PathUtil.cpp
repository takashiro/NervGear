/************************************************************************************

Filename    :   OVR_String_PathUtil.cpp
Content     :   String filename/url helper function
Created     :   September 19, 2012
Notes       :

Copyright   :   Copyright 2014 Oculus VR, LLC. All Rights reserved.

************************************************************************************/

#include "VString.h"
#include "UTF8Util.h"

namespace NervGear {

//--------------------------------------------------------------------
// ***** Path-Scanner helper function

// Scans file path finding filename start and extension start, fills in their address.
void ScanFilePath(const char* url, const char** pfilename, const char** pext)
{
    const char* urlStart = url;
    const char *filename = 0;
    const char *lastDot = 0;

    UInt32 charVal = UTF8Util::DecodeNextChar(&url);

    while (charVal != 0)
    {
        if ((charVal == '/') || (charVal == '\\'))
        {
            filename = url;
            lastDot  = 0;
        }
        else if (charVal == '.')
        {
            lastDot = url - 1;
        }

        charVal = UTF8Util::DecodeNextChar(&url);
    }

    if (pfilename)
    {
        // It was a naked filename
        if (urlStart && (*urlStart != '.') && *urlStart)
            *pfilename = urlStart;
        else
            *pfilename = filename;
    }

    if (pext)
    {
        *pext = lastDot;
    }
}

// Scans file path finding filename start and extension start, fills in their address.
void ScanFilePath2(const char* url, const char** pfilename, const char** pext)
{
    const char *filename = url;
    const char *lastDot = 0;

    UInt32 charVal = UTF8Util::DecodeNextChar(&url);

    while (charVal != 0)
    {
        if ((charVal == '/') || (charVal == '\\'))
        {
            filename = url;
            lastDot  = 0;
        }
        else if (charVal == '.')
        {
            lastDot = url - 1;
        }

        charVal = UTF8Util::DecodeNextChar(&url);
    }

    if (pfilename)
    {
    	*pfilename = filename;
    }

    if (pext)
    {
        *pext = lastDot;
    }
}

// Scans till the end of protocol. Returns first character past protocol,
// 0 if not found.
//  - protocol: 'file://', 'http://'
const char* ScanPathProtocol(const char* url)
{
    UInt32 charVal = UTF8Util::DecodeNextChar(&url);
    UInt32 charVal2;

    while (charVal != 0)
    {
        // Treat a colon followed by a slash as absolute.
        if (charVal == ':')
        {
            charVal2 = UTF8Util::DecodeNextChar(&url);
            charVal  = UTF8Util::DecodeNextChar(&url);
            if ((charVal == '/') && (charVal2 == '\\'))
                return url;
        }
        charVal = UTF8Util::DecodeNextChar(&url);
    }
    return 0;
}


//--------------------------------------------------------------------
// ***** String Path API implementation

bool String::HasAbsolutePath(const char* url)
{
    // Absolute paths can star with:
    //  - protocols:        'file://', 'http://'
    //  - windows drive:    'c:\'
    //  - UNC share name:   '\\share'
    //  - unix root         '/'

    // On the other hand, relative paths are:
    //  - directory:        'directory/file'
    //  - this directory:   './file'
    //  - parent directory: '../file'
    //
    // For now, we don't parse '.' or '..' out, but instead let it be concatenated
    // to string and let the OS figure it out. This, however, is not good for file
    // name matching in library/etc, so it should be improved.

    if (!url || !*url)
        return true; // Treat empty strings as absolute.

    UInt32 charVal = UTF8Util::DecodeNextChar(&url);

    // Fist character of '/' or '\\' means absolute url.
    if ((charVal == '/') || (charVal == '\\'))
        return true;

    while (charVal != 0)
    {
        // Treat a colon followed by a slash as absolute.
        if (charVal == ':')
        {
            charVal = UTF8Util::DecodeNextChar(&url);
            // Protocol or windows drive. Absolute.
            if ((charVal == '/') || (charVal == '\\'))
                return true;
        }
        else if ((charVal == '/') || (charVal == '\\'))
        {
            // Not a first character (else 'if' above the loop would have caught it).
            // Must be a relative url.
            break;
        }

        charVal = UTF8Util::DecodeNextChar(&url);
    }

    // We get here for relative paths.
    return false;
}


bool String::HasExtension(const char* path)
{
    const char* ext = 0;
    ScanFilePath(path, 0, &ext);
    return ext != 0;
}
bool String::HasProtocol(const char* path)
{
    return ScanPathProtocol(path) != 0;
}


String  String::path() const
{
    const char* filename = 0;
    ScanFilePath(toCString(), &filename, 0);

    // Technically we can have extra logic somewhere for paths,
    // such as enforcing protocol and '/' only based on flags,
    // but we keep it simple for now.
    return String(toCString(), filename ? (filename-toCString()) : size());
}

String  String::protocol() const
{
    const char* protocolEnd = ScanPathProtocol(toCString());
    return String(toCString(), protocolEnd ? (protocolEnd-toCString()) : 0);
}

String  String::fileName() const
{
    const char* filename = 0;
    ScanFilePath(toCString(), &filename, 0);
    return String(filename);
}

// FIXME: get rid of this when ScanFilePath is fixed
#if 0
String  String::GetFilename2() const
{
    const char* filename = 0;
    ScanFilePath2(toCString(), &filename, 0);
    return String(filename);
}
#endif

String  String::extension() const
{
    const char* ext = 0;
    ScanFilePath(toCString(), 0, &ext);
    return String(ext);
}

void    String::stripExtension()
{
    const char* ext = 0;
    ScanFilePath(toCString(), 0, &ext);
    if (ext)
    {
        *this = String(toCString(), ext-toCString());
    }
}

void    String::stripProtocol()
{
    const char* protocol = ScanPathProtocol(toCString());
    if (protocol)
        assign(protocol, OVR_strlen(protocol));
}
#if 0
void    String::StripPath()
{
    const char* ext = 0;
    ScanFilePath(toCString(), 0, &ext);
    if (ext)
    {
        *this = String(toCString(), ext-toCString());
    }
}
#endif
} // OVR
