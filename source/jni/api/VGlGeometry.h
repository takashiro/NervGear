#pragma once

#include "../vglobal.h"

#include "../core/VArray.h"
#include "../core/VMath.h"
#include "../core/MemBuffer.h"

NV_NAMESPACE_BEGIN

struct VertexAttribs
{
    VArray< Vector3f > position;
    VArray< Vector3f > normal;
    VArray< Vector3f > tangent;
    VArray< Vector3f > binormal;
    VArray< Vector4f > color;
    VArray< Vector2f > uv0;
    VArray< Vector2f > uv1;
    VArray< Vector4i > jointIndices;
    VArray< Vector4f > jointWeights;
};

typedef unsigned short TriangleIndex;
//typedef unsigned int TriangleIndex;

static const int MAX_GEOMETRY_VERTICES	= 1 << ( sizeof( TriangleIndex ) * 8 );
static const int MAX_GEOMETRY_INDICES	= 1024 * 1024 * 3;

class VGlGeometry
{
public:
    VGlGeometry() :
            vertexBuffer( 0 ),
            indexBuffer( 0 ),
            vertexArrayObject( 0 ),
            vertexCount( 0 ),
            indexCount( 0 ) {}

    VGlGeometry( const VertexAttribs & attribs, const VArray< TriangleIndex > & indices ) :
            vertexBuffer( 0 ),
            indexBuffer( 0 ),
            vertexArrayObject( 0 ),
            vertexCount( 0 ),
            indexCount( 0 ) { Create( attribs, indices ); }

    // Create the VAO and vertex and index buffers from arrays of data.
    void	Create( const VertexAttribs & attribs, const VArray< TriangleIndex > & indices );
    void	Update( const VertexAttribs & attribs );

    // Assumes the correct program, uniforms, textures, etc, are all bound.
    // Leaves the VAO bound for efficiency, be careful not to inadvertently
    // modify any of the state.
    // You need to manually bind and draw if you want to use GL_LINES / GL_POINTS / GL_TRISTRIPS,
    // or only draw a subset of the indices.
    void	Draw() const;

    // Free the buffers and VAO, assuming that they are strictly for this geometry.
    // We could save some overhead by packing an entire model into a single buffer, but
    // it would add more coupling to the structures.
    // This is not in the destructor to allow objects of this class to be passed by value.
    void	Free();

public:
    unsigned 	vertexBuffer;
    unsigned 	indexBuffer;
    unsigned 	vertexArrayObject;
    int			vertexCount;
    int 		indexCount;
};

class VGlGeometryFactory
{
public:
    // Build it in a -1 to 1 range, which will be scaled to the appropriate
    // aspect ratio for each usage.
    //
    // A horizontal and vertical value of 1 will give a single quad.
    //
    // Texcoords range from 0 to 1.
    //
    // Color is 1, fades alpha to 0 along the outer edge.
    static VGlGeometry CreateTesselatedQuad( const int horizontal, const int vertical, bool twoSided = false );

    static VGlGeometry CreateFadedScreenMask( const float xFraction, const float yFraction );

    // 8 quads making a thin border inside the -1 tp 1 square.
    // The fractions are the total fraction that will be faded,
    // half on one side, half on the other.
    static VGlGeometry CreateVignette( const float xFraction, const float yFraction );

    // Build it in a -1 to 1 range, which will be scaled to the appropriate
    // aspect ratio for each usage.
    // Fades alpha to 0 along the outer edge.
    static VGlGeometry CreateTesselatedCylinder( const float radius, const float height,
                                        const int horizontal, const int vertical, const float uScale, const float vScale );

    static VGlGeometry CreateDome( const float latRads, const float uScale = 1.0f, const float vScale = 1.0f );
    static VGlGeometry CreateGlobe( const float uScale = 1.0f, const float vScale = 1.0f );

    // Make a square patch on a sphere that can rotate with the viewer
    // so it always covers the screen.
    static VGlGeometry CreateSpherePatch( const float fov );

    // Builds a grid of lines inside the -1 to 1 bounds.
    // Always has lines through the origin, plus extraLines on each side
    // of the origin.
    // Go from very slightly inside the -1 to 1 bounds so lines won't get
    // clipped at the edge of the projection frustum.
    //
    // If not fullGrid, all lines but x=0 and y=0 will be short hashes
    static VGlGeometry CreateCalibrationLines( const int extraLines, const bool fullGrid );

    // 12 edges of a 0 to 1 unit cube.
    static VGlGeometry CreateUnitCubeLines();

    static VGlGeometry CreateCalibrationLines2( const int extraLines, const bool fullGrid );

    static VGlGeometry CreateQuad();

    static VGlGeometry LoadMeshFromMemory( const MemBuffer & buf,
                                           const int numSlicesPerEye, const float fovScale, const bool cursorOnly );

    static VGlGeometry LoadMeshFromFile( const char* src,
                                           const int numSlicesPerEye, const float fovScale, const bool cursorOnly );
};


NV_NAMESPACE_END
