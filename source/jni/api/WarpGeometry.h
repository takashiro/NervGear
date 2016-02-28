/************************************************************************************

Filename    :   WarpGeometry.h
Content     :   Geometry used by the time warp.
Created     :   March 3, 2015
Authors     :   J.M.P. van Waveren

Copyright   :   Copyright 2015 Oculus VR, LLC. All Rights reserved.

*************************************************************************************/
#ifndef OVR_WarpGeometry_h
#define OVR_WarpGeometry_h

#include "Android/GlUtils.h"

namespace NervGear {

struct WarpGeometry
{
				WarpGeometry() :
					vertexBuffer( 0 ),
					indexBuffer( 0 ),
					vertexArrayObject( 0 ),
					vertexCount( 0 ),
					indexCount( 0 ) {}

	GLuint		vertexBuffer;
	GLuint		indexBuffer;
	GLuint	 	vertexArrayObject;
	int			vertexCount;
	int 		indexCount;
};

void CreateQuadWarpGeometry( WarpGeometry * geometry );
void DestroyWarpGeometry( WarpGeometry * geometry );

} // namespace NervGear

#endif	// OVR_WarpGeometry_h
