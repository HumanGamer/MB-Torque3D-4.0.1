//-----------------------------------------------------------------------------
// Verve
// Copyright (C) 2014 - Violent Tulip
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
#ifndef _VT_VSPAWNSPHERESPAWNTARGETTRACK_H_
#define _VT_VSPAWNSPHERESPAWNTARGETTRACK_H_

#ifndef _VT_VSCENEOBJECTTRACK_H_
#include "Verve/Extension/SceneObject/VSceneObjectTrack.h"
#endif

#ifndef _VT_TORQUE_LIGHTOBJECT_H_
#include "Verve/Torque/TLightObject.h"
#endif

//-----------------------------------------------------------------------------

class VSpawnSphereSpawnTargetTrack : public VSceneObjectTrack
{
    typedef VSceneObjectTrack Parent;

protected:

    SimSet mSpawnList;

    bool mDespawnOnStop;
    bool mDespawnOnLoop;

public:

    VSpawnSphereSpawnTargetTrack( void );

    static void initPersistFields( void );

    // Controller Methods.

    virtual bool      onControllerEvent( VController::eControllerEventType pEvent );

    // Spawn Methods.

    virtual void      spawnTarget( void );
    virtual void      despawnTargets( void );

    // Console Declaration.

    DECLARE_CONOBJECT( VSpawnSphereSpawnTargetTrack );
};

//-----------------------------------------------------------------------------

#endif // _VT_VSPAWNSPHERESPAWNTARGETTRACK_H_