//-----------------------------------------------------------------------------
// Torque Shader Engine
// Copyright (C) GarageGames.com, Inc.
//-----------------------------------------------------------------------------

#include "marble.h"
#include "sfx/sfxSystem.h"

//----------------------------------------------------------------------------

static U32 sCollisionMask = StaticObjectType |
                            TerrainObjectType |
                            //InteriorObjectType |
                            WaterObjectType |
                            StaticShapeObjectType |
                            PlayerObjectType;

static U32 sContactMask = StaticObjectType |
                          TerrainObjectType |
                          //InteriorObjectType |
                          WaterObjectType |
                          StaticShapeObjectType |
                          PlayerObjectType;

bool gMarbleAxisSet = false;
Point3F gWorkGravityDir;
Point3F gMarbleSideDir;

#define SurfaceDotThreshold 0.0001

Point3D Marble::getVelocityD() const
{
    return mVelocity;
}

void Marble::setVelocityD(const Point3D& vel)
{
    dMemcpy(mVelocity, vel, sizeof(mVelocity));
    mSinglePrecision.mVelocity = vel;

    setMaskBits(MoveMask);
}

void Marble::setVelocityRotD(const Point3D& rot)
{
    dMemcpy(mOmega, rot, sizeof(mOmega));

    setMaskBits(MoveMask);
}

void Marble::applyImpulse(const Point3F& pos, const Point3F& vec)
{
    setVelocityD(vec / getMass() + mVelocity);
}

void Marble::clearMarbleAxis()
{
    gMarbleAxisSet = false;
    mGravityFrame.mulP(Point3F(0.0f, 0.0f, -1.0f), &gWorkGravityDir);
}

void Marble::applyContactForces(const Move* move, bool isCentered, Point3D& aControl, const Point3D& desiredOmega, F64 timeStep, Point3D& A, Point3D& a, F32& slipAmount)
{
    F32 force = 0.0f;
    S32 bestContactIndex = mContacts.size();
    for (S32 i = 0; i < mContacts.size(); i++)
    {
        Contact* contact = &mContacts[i];

        SceneObject* obj = contact->object;
        if (!obj || (obj->getTypeMask() & PlayerObjectType) == 0)
        {
            contact->normalForce = -mDot(contact->normal, A);

            if (contact->normalForce > force)
            {
                force = contact->normalForce;
                bestContactIndex = i;
            }
        }

    }

    if (bestContactIndex != mContacts.size() && (mMode & MoveMode) != 0)
        dMemcpy(&mBestContact, &mContacts[bestContactIndex], sizeof(mBestContact));

    if (move->trigger[2] && bestContactIndex != mContacts.size())
    {
        F64 friction = (mVelocity.z - mBestContact.surfaceVelocity.z) * mBestContact.normal.z
                     + (mVelocity.y - mBestContact.surfaceVelocity.y) * mBestContact.normal.y
                     + (mVelocity.x - mBestContact.surfaceVelocity.x) * mBestContact.normal.x;

        if (mDataBlock->jumpImpulse > friction)
        {
            mVelocity += (mDataBlock->jumpImpulse - friction) * mBestContact.normal;

            if (mDataBlock->sound[MarbleData::Jump])
            {
                if (isGhost())// || gSPMode)
                {
                    MatrixF mat(true);
                    mat.setColumn(3, getPosition());
                    SFX->playOnce(mDataBlock->sound[MarbleData::Jump], &mat);
                }
            }
        }
    }

    for (S32 i = 0; i < mContacts.size(); i++)
    {
        Contact* contact = &mContacts[i];

        F64 normalForce = -mDot(contact->normal, A);

        if (normalForce > 0.0 &&
              (mVelocity.x - contact->surfaceVelocity.x) * contact->normal.x
            + (mVelocity.y - contact->surfaceVelocity.y) * contact->normal.y
            + (mVelocity.z - contact->surfaceVelocity.z) * contact->normal.z <= 0.0001)
        {
            A += contact->normal * normalForce;
        }
    }

    if (bestContactIndex != mContacts.size() && (mMode & MoveMode) != 0)
    {
        Point3D vAtC = mVelocity + mCross(mOmega, -mBestContact.normal * mRadius) - mBestContact.surfaceVelocity;
        mBestContact.vAtCMag = vAtC.len();

        bool slipping = false;
        Point3D aFriction(0, 0, 0);
        Point3D AFriction(0, 0, 0);

        if (mBestContact.vAtCMag != 0.0)
        {
            slipping = true;

            F32 friction = 0.0f;
            if ((mMode & RestrictXYZMode) == 0)
                friction = mDataBlock->kineticFriction * mBestContact.friction;

            F64 angAMagnitude = friction * 5.0 * mBestContact.normalForce / (mRadius + mRadius);
            F64 AMagnitude = mBestContact.normalForce * friction;
            F64 totalDeltaV = (mRadius * angAMagnitude + AMagnitude) * timeStep;
            if (mBestContact.vAtCMag < totalDeltaV)
            {
                slipping = false;
                angAMagnitude *= mBestContact.vAtCMag / totalDeltaV;
                AMagnitude *= mBestContact.vAtCMag / totalDeltaV;
            }

            Point3D vAtCDir = vAtC / mBestContact.vAtCMag;

            aFriction = angAMagnitude * mCross(-mBestContact.normal, -vAtCDir);
            AFriction = -AMagnitude * vAtCDir;

            slipAmount = mBestContact.vAtCMag - totalDeltaV;
        }

        if (!slipping)
        {
            Point3D R = -gWorkGravityDir * mRadius;
            Point3D aadd = mCross(R, A) / R.lenSquared();

            if (isCentered)
            {
                Point3D nextOmega = mOmega + a * timeStep;
                aControl = desiredOmega - nextOmega;

                F64 aScalar = aControl.len();
                if (mDataBlock->brakingAcceleration < aScalar)
                    aControl *= mDataBlock->brakingAcceleration / aScalar;
            }
            
            Point3D Aadd = -mCross(aControl, -mBestContact.normal * mRadius);
            F64 aAtCMag = (mCross(aadd, -mBestContact.normal * mRadius) + Aadd).len();

            F64 friction2 = 0.0;
            if ((mMode & RestrictXYZMode) == 0)
                friction2 = mDataBlock->staticFriction * mBestContact.friction;

            if (friction2 * mBestContact.normalForce < aAtCMag)
            {
                friction2 = 0.0;
                if ((mMode & RestrictXYZMode) == 0)
                    friction2 = mDataBlock->kineticFriction * mBestContact.friction;

                Aadd *= friction2 * mBestContact.normalForce / aAtCMag;
            }

            A += Aadd;
            a += aadd;
        }

        A += AFriction;
        a += aFriction;
    }

    a += aControl;
}

void Marble::getMarbleAxis(Point3D& sideDir, Point3D& motionDir, Point3D& upDir)
{
    if (!gMarbleAxisSet)
    {
        MatrixF camMat;
        mGravityFrame.setMatrix(&camMat);


        MatrixF xRot;
        m_matF_set_euler(Point3F(mMouseY, 0, 0), xRot);

        MatrixF zRot;
        m_matF_set_euler(Point3F(0, 0, mMouseX), zRot);

        camMat.mul(zRot);
        camMat.mul(xRot);

        gMarbleMotionDir.x = camMat[1];
        gMarbleMotionDir.y = camMat[5];
        gMarbleMotionDir.z = camMat[9];

        gMarbleSideDir = mCross(gMarbleMotionDir, -gWorkGravityDir);
        m_point3F_normalize(&gMarbleSideDir.x);

        gMarbleMotionDir = mCross(-gWorkGravityDir, gMarbleSideDir);
        
        gMarbleAxisSet = true;
    }

    sideDir = gMarbleSideDir;
    motionDir = gMarbleMotionDir;
    upDir = -gWorkGravityDir;
}

const Point3F& Marble::getMotionDir()
{
    Point3D side;
    Point3D motion;
    Point3D up;
    Marble::getMarbleAxis(side, motion, up);

    return gMarbleMotionDir;
}

bool Marble::computeMoveForces(Point3D& aControl, Point3D& desiredOmega, const Move* move)
{
    aControl.set(0, 0, 0);
    desiredOmega.set(0, 0, 0);

    Point3F invGrav = -gWorkGravityDir;

    Point3D r = invGrav * mRadius;

    Point3D rollVelocity;
    rollVelocity = mCross(mOmega, r);

    Point3D sideDir;
    Point3D motionDir;
    Point3D upDir;
    getMarbleAxis(sideDir, motionDir, upDir);

    Point2F currentVelocity(mDot(sideDir, rollVelocity), mDot(motionDir, rollVelocity));

    Point2F mv(move->x, move->y);
    mv *= 1.538461565971375;

    if (mv.len() > 1.0f)
        m_point2F_normalize_f(mv, 1.0f);

    Point2F desiredVelocity = mv * mDataBlock->maxRollVelocity;

    if (desiredVelocity.x == 0.0f && desiredVelocity.y == 0.0f)
        return 1;

    // TODO: Clean up gotos

    float cY;

    float desiredYVel = desiredVelocity.y;
    float currentYVel = currentVelocity.y;
    float desiredVelX;
    if (currentVelocity.y > desiredVelocity.y)
    {
        cY = currentVelocity.y;
        if (desiredVelocity.y > 0.0f)
        {
LABEL_11:
            desiredVelX = desiredVelocity.x;
            desiredVelocity.y = cY;
            goto LABEL_13;
        }
        currentYVel = currentVelocity.y;
        desiredYVel = desiredVelocity.y;
    }

    if (currentYVel < desiredYVel)
    {
        cY = currentYVel;
        if (desiredYVel < 0.0f)
            goto LABEL_11;
    }
    desiredVelX = desiredVelocity.x;
LABEL_13:
    float cX = currentVelocity.x;
    float v17;
    if (currentVelocity.x > desiredVelX)
    {
        if (desiredVelX > 0.0f)
        {
            v17 = currentVelocity.x;
LABEL_16:
            desiredVelocity.x = v17;
            goto LABEL_20;
        }
        cX = currentVelocity.x;
    }

    if (cX < desiredVelX)
    {
        v17 = cX;
        if (desiredVelX < 0.0f)
            goto LABEL_16;
    }

LABEL_20:
    Point3D newMotionDir = sideDir * desiredVelocity.x + motionDir * desiredVelocity.y;

    Point3D newSideDir;
    newSideDir = mCross(r, newMotionDir);

    desiredOmega = newSideDir * (1.0f / r.lenSquared());
    aControl = desiredOmega - mOmega;

    // Prevent increasing marble speed with diagonal movement
    if (mDataBlock->angularAcceleration < aControl.len())
        aControl *= mDataBlock->angularAcceleration / aControl.len();

    return false;
}

void Marble::velocityCancel(bool surfaceSlide, bool noBounce, bool& bouncedYet, bool& stoppedPaths)//, Vector<PathedInterior*>& pitrVec)
{
    bool looped = false;
    U32 itersIn = 0;
    bool done;

    do
    {
        done = true;
        itersIn++;
        
        for (S32 i = 0; i < mContacts.size(); i++)
        {
            Contact* contact =  &mContacts[i];

            Point3D sVel = mVelocity - contact->surfaceVelocity;
            F64 surfaceDot = mDot(contact->normal, sVel);

            if ((!looped && surfaceDot < 0.0) || surfaceDot < -SurfaceDotThreshold)
            {
                F64 velLen = mVelocity.len();
                Point3D surfaceVel = contact->normal * surfaceDot;

#ifdef MB_ULTRA_PREVIEWS
                if ((isGhost() || gSPMode) && !bouncedYet)
#else
                if (isGhost() && !bouncedYet)
#endif
                {
                    playBounceSound(*contact, -surfaceDot);
                    bouncedYet = true;
                }

                if (noBounce)
                {
                    mVelocity -= surfaceVel;
                } else if (contact->object != NULL && (contact->object->getTypeMask() & PlayerObjectType) != 0)
                {
                    Marble* otherMarble = (Marble*)contact->object;
                    
                    F64 ourMass = getMass();
                    F64 theirMass = otherMarble->getMass();

                    F64 bounce = getMax(mDataBlock->bounceRestitution, otherMarble->mDataBlock->bounceRestitution);

                    Point3D dp = mVelocity * ourMass - otherMarble->getVelocityD() * theirMass;
                    Point3D normP = mDot(dp, contact->normal) * contact->normal;

                    normP *= bounce + 1.0;

                    mVelocity -= normP / ourMass;

                    otherMarble->setVelocityD(otherMarble->getVelocityD() + normP / theirMass);
                    contact->surfaceVelocity = otherMarble->getVelocityD();
                } else
                {
                    if (contact->surfaceVelocity.len() == 0.0 && !surfaceSlide && surfaceDot > -mDataBlock->maxDotSlide * velLen)
                    {

                        mVelocity -= surfaceVel;
                        m_point3D_normalize(mVelocity);
                        mVelocity *= velLen;
                        surfaceSlide = true;
                    } else if (surfaceDot >= -mDataBlock->minBounceVel)
                    {
                        mVelocity -= surfaceVel;
                    } else
                    {
                        F64 restitution = contact->restitution * mPowerUpParams.bounce;
                        Point3D velocityAdd = -(1.0 + restitution) * surfaceVel;
                        Point3D vAtC = sVel + mCross(mOmega, -contact->normal * mRadius);
                        F64 normalVel = -mDot(contact->normal, sVel);

                        bounceEmitter(sVel.len() * restitution, contact->normal);

                        vAtC -= contact->normal * mDot(contact->normal, sVel);

                        F64 vAtCMag = vAtC.len();
                        if (vAtCMag != 0.0) {
                            F64 friction = mDataBlock->bounceKineticFriction * contact->friction;

                            F64 angVMagnitude = friction * 5.0 * normalVel / (mRadius + mRadius);
                            if (vAtCMag / mRadius < angVMagnitude)
                                angVMagnitude = vAtCMag / mRadius;

                            Point3D vAtCDir = vAtC / vAtCMag;

                            Point3D deltaOmega = angVMagnitude * mCross(-contact->normal, -vAtCDir);
                            mOmega += deltaOmega;
                            
                            mVelocity -= mCross(-deltaOmega, -contact->normal * mRadius);
                        }
                        mVelocity += velocityAdd;
                    }

                }

                done = false;
            }
        }

        looped = true;

        if (itersIn > 6 && !stoppedPaths)
        {
            stoppedPaths = true;
            if (noBounce)
                done = true;

            for (S32 j = 0; j < mContacts.size(); j++)
            {
                mContacts[j].surfaceVelocity.set(0, 0, 0);
            }

            /*for (S32 k = 0; k < pitrVec.size(); k++)
            {
                PathedInterior* pitr = pitrVec[k];

                if (pitr->getExtrudedBox().isOverlapped(mWorldBox))
                    pitr->setStopped();
            }*/
        }

    // MBU X360
    } while (!done && itersIn < 20);
    
    if (mVelocity.lenSquared() < 625.0)
    {
        bool gotOne = false;
        Point3F dir(0, 0, 0);
        for (S32 j = 0; j < mContacts.size(); j++)
        {
            Point3F dir2 = dir + mContacts[j].normal;
            if (dir2.lenSquared() < 0.0099999998)
                dir2 += mContacts[j].normal;

            dir = dir2;
            gotOne = true;
        }

        if (gotOne)
        {
            m_point3F_normalize(dir);

            F32 soFar = 0.0;
            for (S32 j = 0; j < mContacts.size(); j++)
            {
                Contact* contact = &mContacts[j];

                if (mRadius > contact->contactDistance)
                {
                    Point3F normal = contact->normal;

                    F32 timeToSeparate = 0.1f;
                    F32 dist = mRadius - contact->contactDistance;
                    Point3F velocity = mVelocity - contact->surfaceVelocity;
                    F32 outVel = mDot(velocity + soFar * dir, normal);
                    if (dist > timeToSeparate * outVel)
                    {
                        soFar += (dist - outVel * timeToSeparate) / timeToSeparate / mDot(normal, dir);
                    }
                }
            }

            soFar = mClampF(soFar, -25.0f, 25.0f);
            mVelocity += soFar * dir;
        }
    }
    
}

Point3D Marble::getExternalForces(const Move* move, F64 timeStep)
{
    if ((mMode & MoveMode) == 0)
        return mVelocity * -16.0;

    Point3D ret = gWorkGravityDir * mDataBlock->gravity * mPowerUpParams.gravityMod;

    Box3F marbleBox(mPosition - mDataBlock->maxForceRadius, mPosition + mDataBlock->maxForceRadius);

    SimpleQueryList sql;
    mContainer->findObjects(marbleBox, ForceObjectType, SimpleQueryList::insertionCallback, &sql);

    Point3F force(0.0f, 0.0f, 0.0f);
    Point3F position = mPosition;

    for (S32 i = 0; i < sql.mList.size(); i++)
    {
        GameBase* obj = (GameBase*)sql.mList[i];
        if (obj != this)
            obj->getForce(position, &force);
    }
    
    ret += force / getMass();

    S32 forceObjectCount = 0;

    if (!mContacts.empty())
    {
        Point3F contactNormal(0.0f, 0.0f, 0.0f);
        F32 contactForce = 0.0f;

        for (S32 i = 0; i < mContacts.size(); i++)
        {
            if (mContacts[i].force != 0.0f)
            {
                forceObjectCount++;
                contactNormal += mContacts[i].normal;
                contactForce = mContacts[i].force;
            }
        }

        if (forceObjectCount != 0)
        {
            m_point3F_normalize(contactNormal);

            F32 contactForceOverMass = contactForce / getMass();

            F32 thing = mDot((Point3F)mVelocity, contactNormal);
            if(contactForceOverMass > thing)
            {
                if (thing > 0.0f)
                    contactForceOverMass -= thing;

                ret += contactNormal * (contactForceOverMass / timeStep);
            }
        }
    }

    if (mContacts.empty() && (mMode & RestrictXYZMode) == 0)
    {
        Point3D sideDir;
        Point3D motionDir;
        Point3D upDir;
        getMarbleAxis(sideDir, motionDir, upDir);

        Point3F movement = sideDir * move->x + motionDir * move->y;
        ret += movement * mPowerUpParams.airAccel;
    }

    return ret;
}

void Marble::advancePhysics(const Move* move, U32 timeDelta)
{
    dMemcpy(&delta.posVec, &mPosition, sizeof(delta.posVec));

    //smPathItrVec.clear();

    F32 dt = timeDelta / 1000.0;

    Box3F extrudedMarble = this->mWorldBox;

    Point3F velocityExpansion = (mVelocity * dt) * 1.100000023841858;
    Point3F absVelocityExpansion = velocityExpansion.abs();

    extrudedMarble.minExtents += (velocityExpansion - absVelocityExpansion) * 0.5f;
    extrudedMarble.maxExtents += (velocityExpansion + absVelocityExpansion) * 0.5f;

    extrudedMarble.minExtents -= dt * 25.0;
    extrudedMarble.maxExtents += dt * 25.0;

    /*for (PathedInterior* obj = PathedInterior::getPathedInteriors(this); ; obj = obj->getNext())
    {
        if (!obj)
            break;

        if (extrudedMarble.isOverlapped(obj->getExtrudedBox()))
        {
            obj->pushTickState();
            obj->computeNextPathStep(timeDelta);
            smPathItrVec.push_back(obj);
        }
    }*/

    resetObjectsAndPolys(sContactMask, extrudedMarble);

    bool bouncedYet = false;
    
    mMovePathSize = 0;
    
    F64 timeRemaining = timeDelta / 1000.0;
    F64 startTime = timeRemaining;
    F32 slipAmount = 0.0;
    F64 contactTime = 0.0;

    U32 it = 0;
    do
    {
        if (timeRemaining == 0.0)
            break;

        F64 timeStep = 0.00800000037997961;
        if (timeRemaining < 0.00800000037997961)
            timeStep = timeRemaining;

        Point3D aControl;
        Point3D desiredOmega;

        bool isCentered = computeMoveForces(aControl, desiredOmega, move);

        findContacts(sContactMask, NULL, NULL);

        bool stoppedPaths = false;
        velocityCancel(isCentered, false, bouncedYet, stoppedPaths);//, smPathItrVec);
        Point3D A = getExternalForces(move, timeStep);

        Point3D a(0, 0, 0);
        applyContactForces(move, isCentered, aControl, desiredOmega, timeStep, A, a, slipAmount);

        mVelocity += A * timeStep;
        mOmega += a * timeStep;

        if ((mMode & RestrictXYZMode) != 0)
            mVelocity.set(0, 0, 0);

        velocityCancel(isCentered, true, bouncedYet, stoppedPaths);//, smPathItrVec);

        F64 moveTime = timeStep;
        //computeFirstPlatformIntersect(moveTime, smPathItrVec);
        testMove(mVelocity, mPosition, moveTime, mRadius, sCollisionMask, false);
        //mPosition += mVelocity * moveTime;

        if (!mMovePathSize && timeStep * 0.99 > moveTime && moveTime > 0.001000000047497451)
        {
            F64 diff = startTime - timeRemaining;

            mMovePath[0] = mPosition;
            mMovePathTime[mMovePathSize] = (diff + moveTime) / startTime;

            mMovePathSize++;
        }

        F64 currentTimeStep = timeStep;
        if (timeStep != moveTime)
        {
            F64 diff = timeStep - moveTime;

            mVelocity -= A * diff;
            mOmega -= a * diff;

            currentTimeStep = moveTime;
        }

        if (!mContacts.empty())
            contactTime += currentTimeStep;

        timeRemaining -= currentTimeStep;

        timeStep = (startTime - timeRemaining) * 1000.0;

        /*for (S32 i = 0; i < smPathItrVec.size(); i++)
        {
            PathedInterior* pint = smPathItrVec[i];
            pint->resetTickState(false);
            pint->advance(timeStep);
        }*/

        it++;
    } while (it <= 10);

    //for (S32 i = 0; i < smPathItrVec.size(); i++)
    //    smPathItrVec[i]->popTickState();

    F32 contactPct = contactTime * 1000.0 / timeDelta;

    Con::setFloatVariable("testCount", contactPct);
    Con::setFloatVariable("marblePitch", mMouseY);

    updateRollSound(contactPct, slipAmount);

    dMemcpy(&delta.pos, &mPosition, sizeof(Point3D));

    delta.posVec -= delta.pos;

    setPosition(mPosition, false);
}

DefineEngineMethod(Marble, setVelocityRot, bool, (Point3F vel),, "(vel)")
{
    object->setVelocityRotD(vel);

    return 1;
}
