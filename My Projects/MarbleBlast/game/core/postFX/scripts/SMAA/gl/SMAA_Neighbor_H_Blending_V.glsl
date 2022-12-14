//-----------------------------------------------------------------------------
// Copyright (c) 2012 GarageGames, LLC
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
//-----------------------------------------------------------------------------
#include "core/rendering/shaders/gl/hlslCompat.glsl"
#include "core/rendering/shaders/gl/torque.glsl"


in vec4 vPosition;
in vec2 vTexCoord0;

out vec4 hpos;
out vec2 uv0;
out vec4 offset;

uniform vec4 rtParams0;

#include "SMAA_Params.glsl"
#define SMAA_INCLUDE_VS 1
#define SMAA_INCLUDE_PS 0

#include "SMAA.glsl"

        
void main()
{    

#ifndef T3D_FLIP
    vTexCoord0 = vTexCoord0 * vec2(1.0, -1.0) + vec2(0.0, 1.0);
#endif  // T3D_FLIP

   gl_Position = vPosition;   
   hpos = gl_Position;
   uv0 = viewportCoordToRenderTarget( vTexCoord0, rtParams0 ); 
   
   SMAANeighborhoodBlendingVS(uv0, offset);
   correctSSP(gl_Position);
}
