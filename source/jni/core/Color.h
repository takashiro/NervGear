#pragma once

#include "vglobal.h"

#include "Types.h"

NV_NAMESPACE_BEGIN

struct Color
{
    UByte R,G,B,A;

    Color() {}

    // Constructs color by channel. Alpha is set to 0xFF (fully visible)
    // if not specified.
    Color(unsigned char r,unsigned char g,unsigned char b, unsigned char a = 0xFF)
        : R(r), G(g), B(b), A(a) { }

    // 0xAARRGGBB - Common HTML color Hex layout
    Color(unsigned c)
        : R((unsigned char)(c>>16)), G((unsigned char)(c>>8)),
        B((unsigned char)c), A((unsigned char)(c>>24)) { }

    bool operator==(const Color& b) const
    {
        return R == b.R && G == b.G && B == b.B && A == b.A;
    }

    void  GetRGBA(float *r, float *g, float *b, float* a) const
    {
        *r = R / 255.0f;
        *g = G / 255.0f;
        *b = B / 255.0f;
        *a = A / 255.0f;
    }
};

NV_NAMESPACE_END
