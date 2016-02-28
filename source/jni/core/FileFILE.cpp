/**************************************************************************

Filename    :   OVR_FileFILE.cpp
Content     :   File wrapper class implementation (Win32)

Created     :   April 5, 1999
Authors     :   Michael Antonov

Copyright   :   Copyright 2014 Oculus VR, LLC. All Rights reserved.

**************************************************************************/

#define  GFILE_CXX

#include "Types.h"
#include "Log.h"

// Standard C library (Captain Obvious guarantees!)
#include <stdio.h>
#ifndef OVR_OS_WINCE
#include <sys/stat.h>
#endif

#include "SysFile.h"

#ifndef OVR_OS_WINCE
#include <errno.h>
#endif

namespace NervGear {

// ***** File interface

// ***** FILEFile - C streams file

static int SFerror ()
{
    if (errno == ENOENT)
        return FileConstants::FileNotFoundError;
    else if (errno == EACCES || errno == EPERM)
        return FileConstants::AccessError;
    else if (errno == ENOSPC)
        return FileConstants::iskFullError;
    else
        return FileConstants::IOError;
};

#ifdef OVR_OS_WIN32
#include "windows.h"
// A simple helper class to disable/enable system error mode, if necessary
// Disabling happens conditionally only if a drive name is involved
class SysErrorModeDisabler
{
    BOOL    Disabled;
    UINT    OldMode;
public:
    SysErrorModeDisabler(const char* pfileName)
    {
        if (pfileName && (pfileName[0]!=0) && pfileName[1]==':')
        {
            Disabled = 1;
            OldMode = ::SetErrorMode(SEM_FAILCRITICALERRORS);
        }
        else
            Disabled = 0;
    }

    ~SysErrorModeDisabler()
    {
        if (Disabled) ::SetErrorMode(OldMode);
    }
};
#else
class SysErrorModeDisabler
{
public:
    SysErrorModeDisabler(const char* pfileName) { }
};
#endif // OVR_OS_WIN32


// This macro enables verification of I/O results after seeks against a pre-loaded
// full file buffer copy. This is generally not necessary, but can been used to debug
// memory corruptions; we've seen this fail due to EAX2/DirectSound corrupting memory
// under FMOD with XP64 (32-bit) and Realtek HA Audio driver.
//#define GFILE_VERIFY_SEEK_ERRORS


// This is the simplest possible file implementation, it wraps around the descriptor
// This file is delegated to by SysFile.

class FILEFile : public File
{
protected:

    // Allocated filename
    String      FileName;

    // File handle & open mode
    bool        Opened;
    FILE*       fs;
    int         OpenFlags;
    // Error code for last request
    int         ErrorCode;

    int         LastOp;

#ifdef OVR_FILE_VERIFY_SEEK_ERRORS
    UByte*      pFileTestBuffer;
    unsigned    FileTestLength;
    unsigned    TestPos; // File pointer position during tests.
#endif

public:

    FILEFile()
    {
        Opened = 0; FileName = "";

#ifdef OVR_FILE_VERIFY_SEEK_ERRORS
        pFileTestBuffer =0;
        FileTestLength  =0;
        TestPos         =0;
#endif
    }
    // Initialize file by opening it
    FILEFile(const String& fileName, int flags, int Mode);
    // The 'pfileName' should be encoded as UTF-8 to support international file names.
    FILEFile(const char* pfileName, int flags, int Mode);

    ~FILEFile()
    {
        if (Opened)
            close();
    }

    virtual const char* filePath();

    // ** File Information
    virtual bool        isValid();
    virtual bool        isWritable();

    // Return position / file size
    virtual int         tell();
    virtual SInt64      tell64();
    virtual int         length();
    virtual SInt64      length64();

//  virtual bool        Stat(FileStats *pfs);
    virtual int         errorCode();

    // ** Stream implementation & I/O
    virtual int         write(const UByte *pbuffer, int numBytes);
    virtual int         read(UByte *pbuffer, int numBytes);
    virtual int         skipBytes(int numBytes);
    virtual int         bytesAvailable();
    virtual bool        flush();
    virtual int         seek(int offset, int origin);
    virtual SInt64      seek64(SInt64 offset, int origin);

    virtual int         copyFromStream(File *pStream, int byteSize);
    virtual bool        close();
private:
    void                init();
};


// Initialize file by opening it
FILEFile::FILEFile(const String& fileName, int flags, int mode)
  : FileName(fileName), OpenFlags(flags)
{
    OVR_UNUSED(mode);
    init();
}

// The 'pfileName' should be encoded as UTF-8 to support international file names.
FILEFile::FILEFile(const char* pfileName, int flags, int mode)
  : FileName(pfileName), OpenFlags(flags)
{
    OVR_UNUSED(mode);
    init();
}

void FILEFile::init()
{
    // Open mode for file's open
    const char *omode = "rb";

    if (OpenFlags & Open_Truncate)
    {
        if (OpenFlags & Open_Read)
            omode = "w+b";
        else
            omode = "wb";
    }
    else if (OpenFlags & Open_Create)
    {
        if (OpenFlags & Open_Read)
            omode = "a+b";
        else
            omode = "ab";
    }
    else if (OpenFlags & Open_Write)
        omode = "r+b";

#ifdef OVR_OS_WIN32
    SysErrorModeDisabler disabler(FileName.toCString());
#endif

#if defined(OVR_CC_MSVC) && (OVR_CC_MSVC >= 1400)
    wchar_t womode[16];
    wchar_t *pwFileName = (wchar_t*)OVR_ALLOC((UTF8Util::GetLength(FileName.toCString())+1) * sizeof(wchar_t));
    UTF8Util::DecodeString(pwFileName, FileName.toCString());
    OVR_ASSERT(strlen(omode) < sizeof(womode)/sizeof(womode[0]));
    UTF8Util::DecodeString(womode, omode);
    _wfopen_s(&fs, pwFileName, womode);
    OVR_FREE(pwFileName);
#else
    fs = fopen(FileName.toCString(), omode);
#endif
    if (fs)
        rewind (fs);
    Opened = (fs != NULL);
    // Set error code
    if (!Opened)
        ErrorCode = SFerror();
    else
    {
        // If we are testing file seek correctness, pre-load the entire file so
        // that we can do comparison tests later.
#ifdef OVR_FILE_VERIFY_SEEK_ERRORS
        TestPos         = 0;
        fseek(fs, 0, SEEK_END);
        FileTestLength  = ftell(fs);
        fseek(fs, 0, SEEK_SET);
        pFileTestBuffer = (UByte*)OVR_ALLOC(FileTestLength);
        if (pFileTestBuffer)
        {
            OVR_ASSERT(FileTestLength == (unsigned)Read(pFileTestBuffer, FileTestLength));
            Seek(0, Seek_Set);
        }
#endif

        ErrorCode = 0;
    }
    LastOp = 0;
}


const char* FILEFile::filePath()
{
    return FileName.toCString();
}


// ** File Information
bool    FILEFile::isValid()
{
    return Opened;
}
bool    FILEFile::isWritable()
{
    return isValid() && (OpenFlags&Open_Write);
}
/*
bool    FILEFile::IsRecoverable()
{
    return IsValid() && ((OpenFlags&OVR_FO_SAFETRUNC) == OVR_FO_SAFETRUNC);
}
*/

// Return position / file size
int     FILEFile::tell()
{
    int pos = (int)ftell (fs);
    if (pos < 0)
        ErrorCode = SFerror();
    return pos;
}

SInt64  FILEFile::tell64()
{
    SInt64 pos = ftell(fs);
    if (pos < 0)
        ErrorCode = SFerror();
    return pos;
}

int     FILEFile::length()
{
    int pos = tell();
    if (pos >= 0)
    {
        seek (0, Seek_End);
        int size = tell();
        seek (pos, Seek_Set);
        return size;
    }
    return -1;
}
SInt64  FILEFile::length64()
{
    SInt64 pos = tell64();
    if (pos >= 0)
    {
        seek64 (0, Seek_End);
        SInt64 size = tell64();
        seek64 (pos, Seek_Set);
        return size;
    }
    return -1;
}

int     FILEFile::errorCode()
{
    return ErrorCode;
}

// ** Stream implementation & I/O
int     FILEFile::write(const UByte *pbuffer, int numBytes)
{
    if (LastOp && LastOp != Open_Write)
        fflush(fs);
    LastOp = Open_Write;
    int written = (int) fwrite(pbuffer, 1, numBytes, fs);
    if (written < numBytes)
        ErrorCode = SFerror();

#ifdef OVR_FILE_VERIFY_SEEK_ERRORS
    if (written > 0)
        TestPos += written;
#endif

    return written;
}

int     FILEFile::read(UByte *pbuffer, int numBytes)
{
    if (LastOp && LastOp != Open_Read)
        fflush(fs);
    LastOp = Open_Read;
    int read = (int) fread(pbuffer, 1, numBytes, fs);
    if (read < numBytes)
        ErrorCode = SFerror();

#ifdef OVR_FILE_VERIFY_SEEK_ERRORS
    if (read > 0)
    {
        // Read-in data must match our pre-loaded buffer data!
        UByte* pcompareBuffer = pFileTestBuffer + TestPos;
        for (int i=0; i< read; i++)
        {
            OVR_ASSERT(pcompareBuffer[i] == pbuffer[i]);
        }

        //OVR_ASSERT(!memcmp(pFileTestBuffer + TestPos, pbuffer, read));
        TestPos += read;
        OVR_ASSERT(ftell(fs) == (int)TestPos);
    }
#endif

    return read;
}

// Seeks ahead to skip bytes
int     FILEFile::skipBytes(int numBytes)
{
    SInt64 pos    = tell64();
    SInt64 newPos = seek64(numBytes, Seek_Cur);

    // Return -1 for major error
    if ((pos==-1) || (newPos==-1))
    {
        return -1;
    }
    //ErrorCode = ((NewPos-Pos)<numBytes) ? errno : 0;

    return int (newPos-(int)pos);
}

// Return # of bytes till EOF
int     FILEFile::bytesAvailable()
{
    SInt64 pos    = tell64();
    SInt64 endPos = length64();

    // Return -1 for major error
    if ((pos==-1) || (endPos==-1))
    {
        ErrorCode = SFerror();
        return 0;
    }
    else
        ErrorCode = 0;

    return int (endPos-(int)pos);
}

// Flush file contents
bool    FILEFile::flush()
{
    return !fflush(fs);
}

int     FILEFile::seek(int offset, int origin)
{
    int newOrigin = 0;
    switch(origin)
    {
    case Seek_Set: newOrigin = SEEK_SET; break;
    case Seek_Cur: newOrigin = SEEK_CUR; break;
    case Seek_End: newOrigin = SEEK_END; break;
    }

    if (newOrigin == SEEK_SET && offset == tell())
        return tell();

    if (fseek (fs, offset, newOrigin))
    {
#ifdef OVR_FILE_VERIFY_SEEK_ERRORS
        OVR_ASSERT(0);
#endif
        return -1;
    }

#ifdef OVR_FILE_VERIFY_SEEK_ERRORS
    // Track file position after seeks for read verification later.
    switch(origin)
    {
    case Seek_Set:  TestPos = offset;       break;
    case Seek_Cur:  TestPos += offset;      break;
    case Seek_End:  TestPos = FileTestLength + offset; break;
    }
    OVR_ASSERT((int)TestPos == Tell());
#endif

    return (int)tell();
}

SInt64  FILEFile::seek64(SInt64 offset, int origin)
{
    return seek((int)offset,origin);
}

int FILEFile::copyFromStream(File *pstream, int byteSize)
{
    UByte   buff[0x4000];
    int     count = 0;
    int     szRequest, szRead, szWritten;

    while (byteSize)
    {
        szRequest = (byteSize > int(sizeof(buff))) ? int(sizeof(buff)) : byteSize;

        szRead    = pstream->read(buff, szRequest);
        szWritten = 0;
        if (szRead > 0)
            szWritten = write(buff, szRead);

        count    += szWritten;
        byteSize -= szWritten;
        if (szWritten < szRequest)
            break;
    }
    return count;
}


bool FILEFile::close()
{
#ifdef OVR_FILE_VERIFY_SEEK_ERRORS
    if (pFileTestBuffer)
    {
        OVR_FREE(pFileTestBuffer);
        pFileTestBuffer = 0;
        FileTestLength  = 0;
    }
#endif

    bool closeRet = !fclose(fs);

    if (!closeRet)
    {
        ErrorCode = SFerror();
        return 0;
    }
    else
    {
        Opened    = 0;
        fs        = 0;
        ErrorCode = 0;
    }

    // Handle safe truncate
    /*
    if ((OpenFlags & OVR_FO_SAFETRUNC) == OVR_FO_SAFETRUNC)
    {
        // Delete original file (if it existed)
        DWORD oldAttributes = FileUtilWin32::GetFileAttributes(FileName);
        if (oldAttributes!=0xFFFFFFFF)
            if (!FileUtilWin32::DeleteFile(FileName))
            {
                // Try to remove the readonly attribute
                FileUtilWin32::SetFileAttributes(FileName, oldAttributes & (~FILE_ATTRIBUTE_READONLY) );
                // And delete the file again
                if (!FileUtilWin32::DeleteFile(FileName))
                    return 0;
            }

        // Rename temp file to real filename
        if (!FileUtilWin32::MoveFile(TempName, FileName))
        {
            //ErrorCode = errno;
            return 0;
        }
    }
    */
    return 1;
}

/*
bool    FILEFile::CloseCancel()
{
    bool closeRet = (bool)::CloseHandle(fd);

    if (!closeRet)
    {
        //ErrorCode = errno;
        return 0;
    }
    else
    {
        Opened    = 0;
        fd        = INVALID_HANDLE_VALUE;
        ErrorCode = 0;
    }

    // Handle safe truncate (delete tmp file, leave original unchanged)
    if ((OpenFlags&OVR_FO_SAFETRUNC) == OVR_FO_SAFETRUNC)
        if (!FileUtilWin32::DeleteFile(TempName))
        {
            //ErrorCode = errno;
            return 0;
        }
    return 1;
}
*/

File *FileFILEOpen(const String& path, int flags, int mode)
{
    return new FILEFile(path, flags, mode);
}

// Helper function: obtain file information time.
bool    SysFile::getFileStat(FileStat* pfileStat, const String& path)
{
#if defined(OVR_OS_WIN32)
    // 64-bit implementation on Windows.
    struct __stat64 fileStat;
    // Stat returns 0 for success.
    wchar_t *pwpath = (wchar_t*)OVR_ALLOC((UTF8Util::GetLength(path.toCString())+1)*sizeof(wchar_t));
    UTF8Util::DecodeString(pwpath, path.toCString());

    int ret = _wstat64(pwpath, &fileStat);
    OVR_FREE(pwpath);
    if (ret) return false;
#else
    struct stat fileStat;
    // Stat returns 0 for success.
    if (stat(path, &fileStat) != 0)
        return false;
#endif
    pfileStat->accessTime = fileStat.st_atime;
    pfileStat->modifyTime = fileStat.st_mtime;
    pfileStat->fileSize   = fileStat.st_size;
    return true;
}

} // Namespace OVR
