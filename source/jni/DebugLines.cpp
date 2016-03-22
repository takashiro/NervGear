/************************************************************************************

Filename    :   DebugLines.cpp
Content     :   Class that manages and renders debug lines.
Created     :   April 22, 2014
Authors     :   Jonathan E. Wright

Copyright   :   Copyright 2014 Oculus VR, LLC. All Rights reserved.

*************************************************************************************/

#include "DebugLines.h"

#include <stdlib.h>

#include "OVR.h"
#include "Android/GlUtils.h"
#include "Android/LogUtils.h"

#include "api/VGlGeometry.h"
#include "api/VGlShader.h"

NV_NAMESPACE_BEGIN

static const char* DebugLineVertexSrc =
	"uniform mat4 Mvpm;\n"
	"attribute vec4 Position;\n"
	"attribute vec4 VertexColor;\n"
	"varying lowp vec4 outColor;\n"
	"void main()\n"
	"{\n"
	"   gl_Position = Mvpm * Position;\n"
	"   outColor = VertexColor;\n"
	"}\n";

static const char* DebugLineFragmentSrc =
	"varying lowp vec4 outColor;\n"
	"void main()\n"
	"{\n"
	"	gl_FragColor = outColor;\n"
	"}\n";

//==============================================================
// OvrDebugLinesLocal
//
class OvrDebugLinesLocal : public OvrDebugLines
{
public:
	// a single debug line
	struct DebugLine_t
	{
        DebugLine_t( const V3Vectf & start, const V3Vectf & end,
                const V4Vectf & startColor, const V4Vectf & endColor,
				const long long endFrame ) :
			Start( start ),
			End( end ),
			StartColor( startColor ),
			EndColor( endColor ),
			EndFrame( endFrame )
		{
		}

        V3Vectf	Start;
        V3Vectf	End;
        V4Vectf	StartColor;
        V4Vectf	EndColor;
		long long	EndFrame;
	};

	struct LineVertex_t 
	{
		LineVertex_t() :
			x( 0.0f ),
			y( 0.0f ),
			z( 0.0f ),
			r( 0.0f ),
			g( 0.0f ),
			b( 0.0f ),
			a( 0.0f )
		{
		}
                
		float           x;
		float           y;
		float			z;
		float			r;
		float			g;
		float			b;
		float			a;
	};

	typedef unsigned short LineIndex_t;

	static const int MAX_DEBUG_LINES = 2048;

						OvrDebugLinesLocal();
	virtual				~OvrDebugLinesLocal();

	virtual void		Init();
	virtual void		Shutdown();

	virtual void		BeginFrame( const long long frameNum );
    virtual void		Render( VR4Matrixf const & mvp ) const;

    virtual void		AddLine(	const V3Vectf & start, const V3Vectf & end,
                                const V4Vectf & startColor, const V4Vectf & endColor,
								const long long endFrame, const bool depthTest );
    virtual void		AddPoint(	const V3Vectf & pos, const float size,
                                const V4Vectf & color, const long long endFrame,
								const bool depthTest );
	// Add a debug point without a specified color. The axis lines will use default
	// colors: X = red, Y = green, Z = blue (same as Maya).
    virtual void		AddPoint(	const V3Vectf & pos, const float size,
								const long long endFrame, const bool depthTest );

    virtual void		AddBounds( VPosf const & pose, VBoxf const & bounds, V4Vectf const & color );

private:
	mutable VGlGeometry				DepthGeo;
	mutable VGlGeometry				NonDepthGeo;
//	NervGear::ArrayPOD< DebugLine_t >	DepthTestedLines;
//	NervGear::ArrayPOD< DebugLine_t >	NonDepthTestedLines;
	NervGear::VArray< DebugLine_t >	DepthTestedLines;
	NervGear::VArray< DebugLine_t >	NonDepthTestedLines;
	LineVertex_t *					Vertices;
	
	bool							Initialized;
	VGlShader						LineProgram;

	void		InitVBO( VGlGeometry & geo, LineVertex_t * vertices, const int maxVerts,
						 LineIndex_t * indices, const int maxIndices );
    void		Render( VR4Matrixf const & mvp, VGlGeometry & geo,
						NervGear::VArray< DebugLine_t > const & lines,
						const bool depthTest ) const;
	void		RemoveExpired( const long long frameNum, NervGear::VArray< DebugLine_t > & lines );
};

//==============================
// OvrDebugLinesLocal::OvrDebugLinesLocal
OvrDebugLinesLocal::OvrDebugLinesLocal() :
	Vertices( NULL ),
	Initialized( false )
{
}

//==============================
// OvrDebugLinesLocal::OvrDebugLinesLocal
OvrDebugLinesLocal::~OvrDebugLinesLocal()
{
	Shutdown();
}

//==============================
// OvrDebugLinesLocal::Init
void OvrDebugLinesLocal::Init()
{
	if ( Initialized )
	{
// JDC: multi-activity test		DROID_ASSERT( !Initialized, "DebugLines" );
		return;
	}

	// this is only freed by the OS when the program exits
	if ( LineProgram.vertexShader == 0 || LineProgram.fragmentShader == 0 )
	{
		LineProgram.initShader( DebugLineVertexSrc, DebugLineFragmentSrc );
	}

	const int MAX_VERTS = MAX_DEBUG_LINES * 2;
	Vertices = new LineVertex_t[ MAX_VERTS ];
	
	// the indices will never change once we've set them up, we just won't necessarily
	// use all of the index buffer to render.
	const int MAX_INDICES = MAX_DEBUG_LINES * 2;
	LineIndex_t * indices = new LineIndex_t[ MAX_INDICES ];

	for ( int i = 0; i < MAX_INDICES; ++i )
	{
		indices[i] = i;
	}

	InitVBO( DepthGeo, Vertices, MAX_VERTS, indices, MAX_INDICES );
	InitVBO( NonDepthGeo, Vertices, MAX_VERTS, indices, MAX_INDICES );

	glBindVertexArrayOES_( 0 );

	delete [] indices;	// never needs to change so we don't keep it around

	Initialized = true;
}

//==============================
// OvrDebugLinesLocal::InitVBO
void OvrDebugLinesLocal::InitVBO( VGlGeometry & geo, LineVertex_t * vertices, const int maxVerts,
		LineIndex_t * indices, const int maxIndices )
{
	const int numVertexBytes = maxVerts * sizeof( LineVertex_t );

	// create vertex array object
    glGenVertexArraysOES_( 1, &geo.vertexArrayObject );
    glBindVertexArrayOES_( geo.vertexArrayObject );

	// create the vertex buffer
	glGenBuffers( 1, &geo.vertexBuffer );
	glBindBuffer( GL_ARRAY_BUFFER, geo.vertexBuffer );
	glBufferData( GL_ARRAY_BUFFER, numVertexBytes, (void*)vertices, GL_DYNAMIC_DRAW );

	glEnableVertexAttribArray( SHADER_ATTRIBUTE_LOCATION_POSITION ); // x, y and z
    glVertexAttribPointer( SHADER_ATTRIBUTE_LOCATION_POSITION, 3, GL_FLOAT, false, sizeof( LineVertex_t ), (void*)0 );

    glEnableVertexAttribArray( SHADER_ATTRIBUTE_LOCATION_COLOR ); // color
    glVertexAttribPointer( SHADER_ATTRIBUTE_LOCATION_COLOR, 4, GL_FLOAT, true, sizeof( LineVertex_t ), (void*)12 );

	const int numIndexBytes = maxIndices * sizeof( LineIndex_t );
	glGenBuffers( 1, &geo.indexBuffer );
	glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, geo.indexBuffer );
	glBufferData( GL_ELEMENT_ARRAY_BUFFER, numIndexBytes, (void*)indices, GL_STATIC_DRAW );
	
	geo.indexCount = 0;	// nothing to render right now
}

//==============================
// OvrDebugLinesLocal::Shutdown
void OvrDebugLinesLocal::Shutdown()
{
	if ( !Initialized )
	{
		DROID_ASSERT( !Initialized, "DebugLines" );
		return;
	}
	DepthGeo.Free();
	NonDepthGeo.Free();
	delete [] Vertices;
	Vertices = NULL;
	Initialized = false;
}

//==============================
// OvrDebugLinesLocal::Render
void OvrDebugLinesLocal::Render( VR4Matrixf const & mvp ) const
{
	// LOG( "OvrDebugLinesLocal::Render" );

	Render( mvp, DepthGeo, DepthTestedLines, true );
	Render( mvp, NonDepthGeo, NonDepthTestedLines, false );
}

//==============================
// OvrDebugLinesLocal::Render
void OvrDebugLinesLocal::Render( VR4Matrixf const & mvp, VGlGeometry & geo,
		NervGear::VArray< DebugLine_t > const & lines,  const bool depthTest ) const
{
	if ( lines.length() == 0 )
	{
		return;
	}

	//LOG( "Rendering %i debug lines", lines.GetSizeI() );

	// go through the debug lines and put them in the vertex list
    int numLines = lines.length() < MAX_DEBUG_LINES ? lines.length() : MAX_DEBUG_LINES;
	for ( int i = 0; i < numLines; ++i )
	{
		DebugLine_t const & line = lines[i];
		LineVertex_t & v1 = Vertices[i * 2 + 0];
		LineVertex_t & v2 = Vertices[i * 2 + 1];
		v1.x = line.Start.x;
		v1.y = line.Start.y;
		v1.z = line.Start.z;
		v1.r = line.StartColor.x;
		v1.g = line.StartColor.y;
		v1.b = line.StartColor.z;
		v1.a = line.StartColor.w;

		v2.x = line.End.x;
		v2.y = line.End.y;
		v2.z = line.End.z;
		v2.r = line.EndColor.x;
		v2.g = line.EndColor.y;
		v2.b = line.EndColor.z;
		v2.a = line.EndColor.w;
	}

	glBindVertexArrayOES_( geo.vertexArrayObject );

	int numVertices = numLines * 2;
	int numVertexBytes = numVertices * sizeof( LineVertex_t );
	glBindBuffer( GL_ARRAY_BUFFER, geo.vertexBuffer );
	glBufferSubData( GL_ARRAY_BUFFER, 0, numVertexBytes, (void*)Vertices );

	glBindVertexArrayOES_( geo.vertexArrayObject );
	geo.indexCount = numLines * 2;

	if ( depthTest )
	{
		glEnable( GL_DEPTH_TEST );
		glDepthMask( GL_TRUE );
	}
	else
	{
		glDisable( GL_DEPTH_TEST );
		glDepthMask( GL_FALSE );
	}

	glEnable( GL_BLEND );
	glBlendFunc( GL_ONE, GL_ONE_MINUS_SRC_ALPHA );
	glLineWidth( 2.0f );	// aliasing is really bad at 1.0

	glUseProgram( LineProgram.program );

    glUniformMatrix4fv( LineProgram.uniformModelViewProMatrix, 1, GL_FALSE, mvp.M[0] );

	glDrawElements( GL_LINES, geo.indexCount, GL_UNSIGNED_SHORT, NULL );

	glBindVertexArrayOES_( 0 );

	glEnable( GL_DEPTH_TEST );
	glDepthMask( GL_TRUE );
	glDisable( GL_BLEND );
}

//==============================
// OvrDebugLinesLocal::AddLine
void OvrDebugLinesLocal::AddLine( const V3Vectf & start, const V3Vectf & end,
        const V4Vectf & startColor, const V4Vectf & endColor,
		const long long endFrame, const bool depthTest )
{
	//LOG( "OvrDebugLinesLocal::AddDebugLine" );
	DebugLine_t line( start, end, startColor, endColor, endFrame );
	if ( depthTest )
	{
		DepthTestedLines.append( line );
	}
	else
	{
		NonDepthTestedLines.append( line );
	}
    //OVR_ASSERT( DepthTestedLines.GetSizeI() < MAX_DEBUG_LINES );
    //OVR_ASSERT( NonDepthTestedLines.GetSizeI() < MAX_DEBUG_LINES );
}

//==============================
// OvrDebugLinesLocal::AddPoint
void OvrDebugLinesLocal::AddPoint(	const V3Vectf & pos, const float size, const V4Vectf & color,
		const long long endFrame, const bool depthTest )
{
	float const hs = size * 0.5f;
    V3Vectf const fwd( 0.0f, 0.0f, hs );
    V3Vectf const right( hs, 0.0f, 0.0f );
    V3Vectf const up( 0.0f, hs, 0.0f );
	
	AddLine( pos - fwd, pos + fwd, color, color, endFrame, depthTest );
	AddLine( pos - right , pos + right, color, color, endFrame, depthTest );
	AddLine( pos - up, pos + up, color, color, endFrame, depthTest );
}

//==============================
// OvrDebugLinesLocal::AddPoint
void OvrDebugLinesLocal::AddPoint(	const V3Vectf & pos, const float size,
		const long long endFrame, const bool depthTest )
{
	float const hs = size * 0.5f;
    V3Vectf const fwd( 0.0f, 0.0f, hs );
    V3Vectf const right( hs, 0.0f, 0.0f );
    V3Vectf const up( 0.0f, hs, 0.0f );
	
    AddLine( pos - fwd, pos + fwd, V4Vectf( 0.0f, 0.0f, 1.0f, 1.0f ), V4Vectf( 0.0f, 0.0f, 1.0f, 1.0f ), endFrame, depthTest );
    AddLine( pos - right, pos + right, V4Vectf( 1.0f, 0.0f, 0.0f, 1.0f ), V4Vectf( 1.0f, 0.0f, 0.0f, 1.0f ), endFrame, depthTest );
    AddLine( pos - up, pos + up, V4Vectf( 0.0f, 1.0f, 0.0f, 1.0f ), V4Vectf( 0.0f, 1.0f, 0.0f, 1.0f ), endFrame, depthTest );
}

//==============================
// OvrDebugLinesLocal::AddBounds
void OvrDebugLinesLocal::AddBounds( VPosf const & pose, VBoxf const & bounds, V4Vectf const & color )
{
    V3Vectf const & mins = bounds.GetMins();
    V3Vectf const & maxs = bounds.GetMaxs();
    V3Vectf corners[8];
	corners[0] = mins;
	corners[7] = maxs;
    corners[1] = V3Vectf( mins.x, maxs.y, mins.z );
    corners[2] = V3Vectf( mins.x, maxs.y, maxs.z );
    corners[3] = V3Vectf( mins.x, mins.y, maxs.z );
    corners[4] = V3Vectf( maxs.x, mins.y, mins.z );
    corners[5] = V3Vectf( maxs.x, maxs.y, mins.z );
    corners[6] = V3Vectf( maxs.x, mins.y, maxs.z );

	// transform points
	for ( int i = 0; i < 8; ++i )
	{
		corners[i] = pose.Orientation.Rotate( corners[i] );
		corners[i] += pose.Position;
	}

	AddLine( corners[0], corners[1], color, color, 1, true );
	AddLine( corners[1], corners[2], color, color, 1, true );
	AddLine( corners[2], corners[3], color, color, 1, true );
	AddLine( corners[3], corners[0], color, color, 1, true );
	AddLine( corners[7], corners[6], color, color, 1, true );
	AddLine( corners[6], corners[4], color, color, 1, true );
	AddLine( corners[4], corners[5], color, color, 1, true );
	AddLine( corners[5], corners[7], color, color, 1, true );
	AddLine( corners[0], corners[4], color, color, 1, true );
	AddLine( corners[1], corners[5], color, color, 1, true );
	AddLine( corners[2], corners[7], color, color, 1, true );
	AddLine( corners[3], corners[6], color, color, 1, true );
}

//==============================
// OvrDebugLinesLocal::BeginFrame
void OvrDebugLinesLocal::BeginFrame( const long long frameNum )
{
	// LOG( "OvrDebugLinesLocal::RemoveExpired: frame %lli, removing %i lines", frameNum, DepthTestedLines.GetSizeI() + NonDepthTestedLines.GetSizeI() );
	DepthGeo.indexCount = 0;
	NonDepthGeo.indexCount = 0;
	RemoveExpired( frameNum, DepthTestedLines );
	RemoveExpired( frameNum, NonDepthTestedLines );
}

//==============================
// OvrDebugLinesLocal::RemoveExpired
void OvrDebugLinesLocal::RemoveExpired( const long long frameNum, NervGear::VArray< DebugLine_t > & lines )
{
	for ( int i = lines.length() - 1; i >= 0; --i )
	{
		const DebugLine_t & dl = lines[i];
		if ( frameNum >= dl.EndFrame )
		{
			lines.removeAtUnordered( i );
		}
	}
}

//==============================
// OvrDebugLines::Create
OvrDebugLines * OvrDebugLines::Create()
{
    return new OvrDebugLinesLocal;
}

//==============================
// OvrDebugLines::Free
void OvrDebugLines::Free( OvrDebugLines * & debugLines )
{
    if ( debugLines != NULL )
    {
        delete debugLines;
        debugLines = NULL;
    }
}

NV_NAMESPACE_END
