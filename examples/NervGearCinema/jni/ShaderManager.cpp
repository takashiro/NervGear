/************************************************************************************

Filename    :   ShaderManager.cpp
Content     :	Allocates and builds shader programs.
Created     :	7/3/2014
Authors     :   Jim Dos?and John Carmack

Copyright   :   Copyright 2014 Oculus VR, LLC. All Rights reserved.

This source code is licensed under the BSD-style license found in the
LICENSE file in the Cinema/ directory. An additional grant
of patent rights can be found in the PATENTS file in the same directory.

*************************************************************************************/

#include "ShaderManager.h"
#include "CinemaApp.h"


using namespace NervGear;

namespace OculusCinema {

//=======================================================================================

static const char* copyMovieVertexShaderSrc =
	"uniform highp mat4 Mvpm;\n"
	"uniform highp mat4 Texm;\n"
	"attribute vec4 Position;\n"
	"attribute vec2 TexCoord;\n"
	"varying  highp vec2 oTexCoord;\n"
	"void main()\n"
	"{\n"
	"   gl_Position = Position;\n"
	"   oTexCoord = vec2( TexCoord.x, 1.0 - TexCoord.y );\n"	// need to flip Y
	"}\n";

static const char* copyMovieFragmentShaderSource =
	"#extension GL_OES_EGL_image_external : require\n"
	"uniform samplerExternalOES Texture0;\n"
	"uniform sampler2D Texture1;\n"				// edge vignette
	"varying highp vec2 oTexCoord;\n"
	"void main()\n"
	"{\n"
	"	gl_FragColor = texture2D( Texture0, oTexCoord ) *  texture2D( Texture1, oTexCoord );\n"
	"}\n";

static const char* movieUiVertexShaderSrc =
	"uniform highp mat4 Mvpm;\n"
	"uniform highp mat4 Texm;\n"
	"attribute vec4 Position;\n"
	"attribute vec2 TexCoord;\n"
	"uniform lowp vec4 UniformColor;\n"
	"varying  highp vec2 oTexCoord;\n"
	"varying  lowp vec4 oColor;\n"
	"void main()\n"
	"{\n"
	"   gl_Position = Mvpm * Position;\n"
	"   oTexCoord = vec2( Texm * vec4(TexCoord,1,1) );\n"
	"   oColor = UniformColor;\n"
	"}\n";


const char* movieExternalUiFragmentShaderSource =
	"#extension GL_OES_EGL_image_external : require\n"
	"uniform samplerExternalOES Texture0;\n"
	"uniform sampler2D Texture1;\n"	// fade / clamp texture
	"uniform lowp vec4 ColorBias;\n"
	"varying highp vec2 oTexCoord;\n"
	"varying lowp vec4	oColor;\n"
	"void main()\n"
	"{\n"
	"	lowp vec4 movieColor = texture2D( Texture0, oTexCoord ) * texture2D( Texture1, oTexCoord );\n"
	"	gl_FragColor = ColorBias + oColor * movieColor;\n"
	"}\n";

static const char* SceneStaticVertexShaderSrc =
	"uniform mat4 Mvpm;\n"
	"uniform lowp vec4 UniformColor;\n"
	"attribute vec4 Position;\n"
	"attribute vec2 TexCoord;\n"
	"varying highp vec2 oTexCoord;\n"
	"varying lowp vec4 oColor;\n"
	"void main()\n"
	"{\n"
	"   gl_Position = Mvpm * Position;\n"
	"   oTexCoord = TexCoord;\n"
	"   oColor = UniformColor;\n"
	"}\n";

static const char* SceneDynamicVertexShaderSrc =
	"uniform sampler2D Texture2;\n"
	"uniform mat4 Mvpm;\n"
	"uniform lowp vec4 UniformColor;\n"
	"attribute vec4 Position;\n"
	"attribute vec2 TexCoord;\n"
	"varying highp vec2 oTexCoord;\n"
	"varying lowp vec4 oColor;\n"
	"void main()\n"
	"{\n"
	"   gl_Position = Mvpm * Position;\n"
	"   oTexCoord = TexCoord;\n"
	"   oColor = texture2DLod(Texture2, vec2( 0.0, 0.0), 16.0 );\n"	// bottom mip of screen texture
	"   oColor.xyz += vec3( 0.05, 0.05, 0.05 );\n"
	"	oColor.w = UniformColor.w;\n"
	"}\n";

static const char* SceneBlackFragmentShaderSrc =
	"void main()\n"
	"{\n"
	"	gl_FragColor = vec4( 0.0, 0.0, 0.0, 1.0 );\n"
	"}\n";

static const char* SceneStaticFragmentShaderSrc =
	"uniform sampler2D Texture0;\n"
	"varying highp vec2 oTexCoord;\n"
	"varying lowp vec4 oColor;\n"
	"void main()\n"
	"{\n"
	"	gl_FragColor.xyz = oColor.w * texture2D(Texture0, oTexCoord).xyz;\n"
	"	gl_FragColor.w = 1.0;\n"
	"}\n";

static const char* SceneStaticAndDynamicFragmentShaderSrc =
	"uniform sampler2D Texture0;\n"
	"uniform sampler2D Texture1;\n"
	"varying highp vec2 oTexCoord;\n"
	"varying lowp vec4 oColor;\n"
	"void main()\n"
	"{\n"
	"	gl_FragColor.xyz = oColor.w * texture2D(Texture0, oTexCoord).xyz + (1.0 - oColor.w) * oColor.xyz * texture2D(Texture1, oTexCoord).xyz;\n"
	"	gl_FragColor.w = 1.0;\n"
	"}\n";

static const char* SceneDynamicFragmentShaderSrc =
	"uniform sampler2D Texture0;\n"
	"varying highp vec2 oTexCoord;\n"
	"varying lowp vec4 oColor;\n"
	"void main()\n"
	"{\n"
	"	gl_FragColor.xyz = (1.0 - oColor.w) * oColor.xyz * texture2D(Texture0, oTexCoord).xyz;\n"
	"	gl_FragColor.w = 1.0;\n"
	"}\n";

static const char* SceneAdditiveFragmentShaderSrc =
	"uniform sampler2D Texture0;\n"
	"varying highp vec2 oTexCoord;\n"
	"varying lowp vec4 oColor;\n"
	"void main()\n"
	"{\n"
	"	gl_FragColor.xyz = (1.0 - oColor.w) * texture2D(Texture0, oTexCoord).xyz;\n"
	"	gl_FragColor.w = 1.0;\n"
	"}\n";

static char const * UniformColorVertexProgSrc =
	"uniform mat4 Mvpm;\n"
	"uniform lowp vec4 UniformColor;\n"
	"attribute vec4 Position;\n"
	"void main() {\n"
	"  gl_Position = Mvpm * Position;\n"
	"}\n";

static char const * UniformColorFragmentProgSrc =
	"uniform lowp vec4 UniformColor;\n"
	"void main() {\n"
	"  gl_FragColor = UniformColor;\n"
	"}\n";



//=======================================================================================

ShaderManager::ShaderManager( CinemaApp &cinema ) :
	Cinema( cinema )
{
}

void ShaderManager::OneTimeInit( const char * launchIntent )
{
	LOG( "ShaderManager::OneTimeInit" );

	const double start = ovr_GetTimeInSeconds();

	MovieExternalUiProgram 		.initShader( movieUiVertexShaderSrc, movieExternalUiFragmentShaderSource );
	CopyMovieProgram 			.initShader( copyMovieVertexShaderSrc, copyMovieFragmentShaderSource );
	UniformColorProgram			.initShader( UniformColorVertexProgSrc, UniformColorFragmentProgSrc );

	ScenePrograms[SCENE_PROGRAM_BLACK]			.initShader( SceneStaticVertexShaderSrc, SceneBlackFragmentShaderSrc );
	ScenePrograms[SCENE_PROGRAM_STATIC_ONLY]	.initShader( SceneStaticVertexShaderSrc, SceneStaticFragmentShaderSrc );
	ScenePrograms[SCENE_PROGRAM_STATIC_DYNAMIC]	.initShader( SceneDynamicVertexShaderSrc, SceneStaticAndDynamicFragmentShaderSrc );
	ScenePrograms[SCENE_PROGRAM_DYNAMIC_ONLY]	.initShader( SceneDynamicVertexShaderSrc, SceneDynamicFragmentShaderSrc );
	ScenePrograms[SCENE_PROGRAM_ADDITIVE]		.initShader( SceneStaticVertexShaderSrc, SceneAdditiveFragmentShaderSrc );

	// NOTE: make sure to load with SCENE_PROGRAM_STATIC_DYNAMIC because the textures are initially not swapped
	DynamicPrograms = ModelGlPrograms( &ScenePrograms[ SCENE_PROGRAM_STATIC_DYNAMIC ] );

	ProgVertexColor				.initShader( VertexColorVertexShaderSrc, VertexColorFragmentShaderSrc );
	ProgSingleTexture			.initShader( SingleTextureVertexShaderSrc, SingleTextureFragmentShaderSrc );
	ProgLightMapped				.initShader( LightMappedVertexShaderSrc, LightMappedFragmentShaderSrc );
	ProgReflectionMapped		.initShader( ReflectionMappedVertexShaderSrc, ReflectionMappedFragmentShaderSrc );
	ProgSkinnedVertexColor		.initShader( VertexColorSkinned1VertexShaderSrc, VertexColorFragmentShaderSrc );
	ProgSkinnedSingleTexture	.initShader( SingleTextureSkinned1VertexShaderSrc, SingleTextureFragmentShaderSrc );
	ProgSkinnedLightMapped		.initShader( LightMappedSkinned1VertexShaderSrc, LightMappedFragmentShaderSrc );
	ProgSkinnedReflectionMapped	.initShader( ReflectionMappedSkinned1VertexShaderSrc, ReflectionMappedFragmentShaderSrc );

	DefaultPrograms.ProgVertexColor				= & ProgVertexColor;
	DefaultPrograms.ProgSingleTexture			= & ProgSingleTexture;
	DefaultPrograms.ProgLightMapped				= & ProgLightMapped;
	DefaultPrograms.ProgReflectionMapped		= & ProgReflectionMapped;
	DefaultPrograms.ProgSkinnedVertexColor		= & ProgSkinnedVertexColor;
	DefaultPrograms.ProgSkinnedSingleTexture	= & ProgSkinnedSingleTexture;
	DefaultPrograms.ProgSkinnedLightMapped		= & ProgSkinnedLightMapped;
	DefaultPrograms.ProgSkinnedReflectionMapped	= & ProgSkinnedReflectionMapped;

	LOG( "ShaderManager::OneTimeInit: %3.1f seconds", ovr_GetTimeInSeconds() - start );
}

void ShaderManager::OneTimeShutdown()
{
	LOG( "ShaderManager::OneTimeShutdown" );

	 MovieExternalUiProgram .destroy();
	 CopyMovieProgram .destroy();
	 UniformColorProgram .destroy();

	 ScenePrograms[SCENE_PROGRAM_BLACK] .destroy();
	 ScenePrograms[SCENE_PROGRAM_STATIC_ONLY] .destroy();
	 ScenePrograms[SCENE_PROGRAM_STATIC_DYNAMIC] .destroy();
	 ScenePrograms[SCENE_PROGRAM_DYNAMIC_ONLY] .destroy();
	 ScenePrograms[SCENE_PROGRAM_ADDITIVE] .destroy();

	//TODO: Review DynamicPrograms

	 ProgVertexColor .destroy();
	 ProgSingleTexture .destroy();
	 ProgLightMapped .destroy();
	 ProgReflectionMapped .destroy();
	 ProgSkinnedVertexColor .destroy();
	 ProgSkinnedSingleTexture	.destroy();
	 ProgSkinnedLightMapped .destroy();
	 ProgSkinnedReflectionMapped .destroy();
}

} // namespace OculusCinema
