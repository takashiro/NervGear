#pragma once

#include "VSysFile.h"


#include <sys/stat.h>
#include <errno.h>

NV_NAMESPACE_BEGIN

static int FError ();

//实现文件具体操作的类，VSysFile通过指针授权给父类调用该类的函数
class VFileOperation : public VFile, std::fstream
{
protected:

    // 文件名
    VString m_fileName;

    // 文件打开标识
    bool m_opened;

    // 文件打开模式
    int m_openFlag;
    // 文件操作错误码
    int m_errorCode;
    // 前一个文件操作
    int m_lastOp;

#ifdef OVR_FILE_VERIFY_SEEK_ERRORS
    //UByte*      pFileTestBuffer;
    unsigned char* pFileTestBuffer;
    unsigned FileTestLength;
    unsigned TestPos; // File pointer position during tests.
#endif

public:
    VFileOperation()
        : m_fileName("")
        , m_opened(false)
    {

#ifdef OVR_FILE_VERIFY_SEEK_ERRORS
        pFileTestBuffer =0;
        FileTestLength =0;
        TestPos =0;
#endif
    }
    // 两种形式的构造函数VString和const char * 形式给出文件名
    VFileOperation(const VString& m_fileName, int flags);

    VFileOperation(const char* pfileName, int flags);

    ~VFileOperation()
    {
        if (m_opened) {
            close();
        }
    }

    virtual const std::string filePath() override;

    bool isOpened() override;
    bool isWritable() override;

    // 文件位置、长度
    int tell() override;
    long long tell64() override;
    int length() override;
    long long length64() override;
    int errorCode() override;

    //  文件读写
    int write(const uchar *pbuffer, int numBytes) override;
    int read(uchar *pbuffer, int numBytes) override;
    int skipBytes(int numBytes) override;
    int bytesAvailable() override;
    bool bufferFlush() override;
    int seek(int offset, std::ios_base::seekdir origin) override;
    long long seek64(long long offset, std::ios_base::seekdir origin) override;

    int copyStream(VFile *pStream, int byteSize) override;
    bool close() override;
private:
    void fileInit();
};

NV_NAMESPACE_END
