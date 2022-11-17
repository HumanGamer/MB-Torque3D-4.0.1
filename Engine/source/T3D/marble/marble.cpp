//-----------------------------------------------------------------------------
// Torque Shader Engine
// Copyright (C) GarageGames.com, Inc.
//-----------------------------------------------------------------------------

#include "T3D/marble/marble.h"

#include "math/mathIO.h"
#include "T3D/fx/particleEmitter.h"
#include "T3D/gameBase/gameProcess.h"
#include "T3D/item.h"
#include "T3D/trigger.h"
#include "materials/matInstance.h"
#include "scene/sceneManager.h"
#include "core/stream/bitStream.h"
#include "sfx/sfxSystem.h"
#include "sfx/sfxSource.h"
#include "math/mTransform.h"

//----------------------------------------------------------------------------

const float gMarbleCompressDists[7] = {5.0f, 11.0f, 23.0f, 47.0f, 96.0f, 195.0f, 500.0f};

Point3F gMarbleMotionDir = Point3F(0, 0, 0);

static U32 sTriggerItemMask = ItemObjectType | TriggerObjectType;
static U32 sCameraCollisionMask = /*InteriorObjectType |*/ StaticShapeObjectType;

//----------------------------------------------------------------------------

IMPLEMENT_CO_NETOBJECT_V1(Marble);

U32 Marble::smEndPadId = 0;
SimObjectPtr<StaticShape> Marble::smEndPad = NULL;
//Vector<PathedInterior*> Marble::smPathItrVec;
Vector<Marble*> Marble::marbles;
ConcretePolyList Marble::polyList;

#ifdef MB_PHYSICS_SWITCHABLE
bool Marble::smTrapLaunch = false;
#endif

Marble::Marble()
{
    mVertBuff = NULL;
    mPrimBuff = NULL;
    mRollHandle = NULL;
    mSlipHandle = NULL;
    mMegaHandle = NULL;

    mTrailEmitter = NULL;
    mMudEmitter = NULL;
    mGrassEmitter = NULL;

    mNetFlags.set(Ghostable | NetOrdered);

    mTypeMask |= PlayerObjectType | GameBaseHiFiObjectType;

    mDataBlock = NULL;
    mAnimateScale = true;

    delta.pos = Point3D(0, 0, 100);
    delta.posVec = Point3D(0, 0, 0);

    dMemcpy(&delta.move, &NullMove, sizeof(delta.move));

    mLastRenderPos = Point3F(0, 0, 0);
    mLastRenderVel = Point3F(0, 0, 0);

    mLastYaw = 0.0f;
    mBounceEmitDelay = 0;
    mRenderBlastPercent = 0.0f;
    
    mMarbleTime = 0;
    mMarbleBonusTime = 0;
    mFullMarbleTime = 0;
    mUseFullMarbleTime = false;

    mMovePathSize = 0;
    mBlastEnergy = 0;

    mObjToWorld.setColumn(3, Point3F(delta.pos.x, delta.pos.y, delta.pos.z));

    MatrixF mat(true);
    mat[0] = 1.0f;
    mat[4] = 0.0f;
    mat[8] = 0.0f;
    mat[1] = 0.0f;
    mat[5] = -1.0f;
    mat[10] = -1.0f;
    mat[9] = 0.0f;
    mat[2] = 0.0f;
    mat[6] = 0.0f;
    mGravityFrame.set(mat);
    mGravityRenderFrame = mGravityFrame;

    mVelocity = Point3D(0, 0, 0);
    delta.prevMouseX = 0.0f;
    delta.prevMouseY = 0.0f;
    mOmega = Point3D(0, 0, 0);
    mPosition = Point3D(0, 0, 0);

    mControllable = true;

    mLastContact.position = Point3D(1.0e10, 1.0e10, 1.0e10);
    mLastContact.normal = Point3D(0, 0, 1);
    mLastContact.actualNormal = Point3F(0, 0, 1);
    
    mBestContact.position = Point3D(1.0e10, 1.0e10, 1.0e10);
    mBestContact.surfaceVelocity = Point3D(0, 0, 0);
    mBestContact.normal = Point3D(0, 0, 1);
    mBestContact.actualNormal = Point3F(0, 0, 1);

    mCheckPointNumber = 0;
    mPadPtr = NULL;

    mMouseY = 0.0f;
    mMouseX = 0.0f;
    mOnPad = false;

    mShadowGenerated = false;
    mStencilMaterial = NULL;

    S32 i;
    for (i = 0; i < PowerUpData::MaxPowerUps; i++)
    {
        mPowerUpState[i].active = false;
        mPowerUpState[i].ticksLeft = 0;
        mPowerUpState[i].emitter = NULL;
        mPowerUpState[i].imageSlot = -1;
    }

    mPowerUpId = 0;
    mPowerUpTimer = 0;
    mBlastTimer = 0;
    mMode = 9;
    mModeTimer = 0;
    mOOB = false;

    mEffect.lastCamFocus = Point3F(0, 0, 0);
    mEffect.effectTime = 0.0f;

    mCameraInit = false;
    mNetSmoothPos = Point3F(0, 0, 0);

    mRadsLeftToCenter = 0.0f;
    mCenteringCamera = false;

    mSinglePrecision.mPosition = mPosition;
    mSinglePrecision.mVelocity = mVelocity;
    mSinglePrecision.mOmega = mOmega;
}

Marble::~Marble()
{
    S32 i;
    for (i = 0; i < PowerUpData::MaxPowerUps; i++)
    {
        if (!mPowerUpState[i].emitter.isNull())
            mPowerUpState[i].emitter->deleteWhenEmpty();
    }

    if (mTrailEmitter)
        mTrailEmitter->deleteWhenEmpty();

    if (mMudEmitter)
        mTrailEmitter->deleteWhenEmpty();

    if (mGrassEmitter)
        mTrailEmitter->deleteWhenEmpty();

    delete mStencilMaterial;
}

void Marble::initPersistFields()
{
    Parent::initPersistFields();

    addField("Controllable", TypeBool, Offset(mControllable, Marble));
}

void Marble::consoleInit()
{
    Parent::consoleInit();

#ifdef MB_PHYSICS_SWITCHABLE
    Con::addVariable("Pref::Marble::EnableTrapLaunch", TypeBool, &Marble::smTrapLaunch);
#endif
}

//----------------------------------------------------------------------------

Marble::Contact::Contact()
{
    // Empty
}

Marble::SinglePrecision::SinglePrecision()
{
    // Empty
}

Marble::StateDelta::StateDelta()
{
    // Empty
}

Marble::EndPadEffect::EndPadEffect()
{
    // Empty
}

Marble::PowerUpState::PowerUpState()
{
    emitter = NULL;
}

Marble::PowerUpState::~PowerUpState()
{
    // Empty
}

//----------------------------------------------------------------------------

SceneObject* Marble::getPad()
{
    return mPadPtr;
}

S32 Marble::getPowerUpId()
{
    return mPowerUpId;
}

const QuatF& Marble::getGravityFrame()
{
    return mGravityFrame;
}

const Point3F& Marble::getGravityDir(Point3F* result)
{
    return mGravityFrame.mulP(Point3F(0.0f, 0.0f, -1.0f), result);
}

U32 Marble::getMaxNaturalBlastEnergy()
{
    return mDataBlock->maxNaturalBlastRecharge >> 5;
}

U32 Marble::getMaxBlastEnergy()
{
    return mDataBlock->blastRechargeTime >> 5;
}

F32 Marble::getBlastPercent()
{
    return (F32)((F64)mBlastEnergy / (F64)(mDataBlock->maxNaturalBlastRecharge >> 5));
}

F32 Marble::getBlastEnergy() const
{
    return mBlastEnergy;
}

void Marble::setBlastEnergy(F32 energy)
{
    mBlastEnergy = (U32)energy;
}

void Marble::setUseFullMarbleTime(bool useFull)
{
    mUseFullMarbleTime = useFull;
    setMaskBits(OOBMask);
}

void Marble::setMarbleTime(U32 time)
{
    mFullMarbleTime = time;
    mMarbleTime = time;
}

U32 Marble::getMarbleTime()
{
    if (mUseFullMarbleTime)
        return mFullMarbleTime;
    else
        return mMarbleTime;
}

void Marble::setMarbleBonusTime(U32 time)
{
    mMarbleBonusTime = time;
}

U32 Marble::getMarbleBonusTime()
{
    return mMarbleBonusTime;
}

U32 Marble::getFullMarbleTime()
{
    return mFullMarbleTime;
}

Marble::Contact& Marble::getLastContact()
{
    return mLastContact;
}

void Marble::setGravityFrame(const QuatF& q, bool snap)
{
    mGravityFrame = q;
    setMaskBits(GravityMask);

    if (snap)
    {
        mGravityRenderFrame = q;
        setMaskBits(GravitySnapMask);
    }
}

void Marble::onSceneRemove()
{
    Parent::onSceneRemove();
}

void Marble::setPosition(const Point3D& pos, bool doWarp)
{
    MatrixF mat = mObjToWorld;
    mat[3] = pos.x;
    mat[7] = pos.y;
    mat[11] = pos.z;
    mPosition = pos;
    mSinglePrecision.mPosition = pos;

    Parent::setTransform(mat);

    setMaskBits(MoveMask);

    if (doWarp)
    {
        mLastRenderPos = pos;
        mLastRenderVel = mVelocity;
        mRenderScale = mObjScale;
        setMaskBits(WarpMask);
    }
}

void Marble::setPosition(const Point3D& pos, const AngAxisF& angAxis, float mouseY)
{
    MatrixF mat = mObjToWorld;
    mat[3] = pos.x;
    mat[7] = pos.y;
    mat[11] = pos.z;
    mPosition = pos;
    mSinglePrecision.mPosition = pos;

    Parent::setTransform(mat);

    angAxis.setMatrix(&mat);

    mMouseX = mAtan2(mat[1], mat[5]);
    mMouseY = mouseY;
    mLastRenderPos = pos;
    mLastRenderVel = mVelocity;
    mRenderScale = mObjScale;

    setMaskBits(MoveMask | WarpMask);
}

void Marble::setTransform(const MatrixF& mat)
{
    setPosition(Point3F(mat[3], mat[7], mat[11]), true);
}

Point3F& Marble::getPosition()
{
    static Point3F position;
    position = Point3F(mObjToWorld[3], mObjToWorld[7], mObjToWorld[11]);
    return position;
}

void Marble::victorySequence()
{
    setVelocity(Point3F(0.0f, 0.0f, 0.1f));
}

void Marble::setMode(U32 mode)
{
    U32 newMode = mode & (mode ^ mMode);

    mMode = mode;

    if ((newMode & StartingMode) != 0)
    {
        mModeTimer = mDataBlock->startModeTime >> 5;
    }
    else if ((newMode & (StoppingMode | FinishMode)) != 0)
    {
        if ((newMode & StoppingMode) != 0)
            mMode |= RestrictXYZMode;
        mModeTimer = mDataBlock->startModeTime >> 5;
    }

    setMaskBits(PowerUpMask);
}

void Marble::setOOB(bool isOOB)
{
    mOOB = isOOB;
    setMaskBits(OOBMask);
}

void Marble::interpolateTick(F32 delta)
{
    Parent::interpolateTick(delta);

    if (getControllingClient() && (mMode & TimerMode) != 0)
    {
        U32 marbleTime = getMarbleTime();
        S32 newMarbleTime = marbleTime > 32 ? marbleTime - 32 : 0;
        U32 marbleBonusTime = mMarbleBonusTime;
        U32 finalMarbleTime = newMarbleTime + (U64)(((F64)marbleTime - newMarbleTime) * (1.0 - delta));
        if (marbleBonusTime && !mUseFullMarbleTime)
            finalMarbleTime = marbleTime;

        Con::evaluatef("PlayGui.updateTimer(%i,%i);", finalMarbleTime, marbleBonusTime != 0);
    }
}

S32 Marble::mountPowerupImage(ShapeBaseImageData* imageData)
{
#ifdef MB_ULTRA_PREVIEWS
    if ((isServerObject() && !gSPMode) || !imageData)
#else
    if (isServerObject() || !imageData)
#endif
        return -1;

    U32 i = 0;
    while (getMountedImage(i))
    {
        if (++i >= 4)
            return -1;
    }

    NetStringHandle temp;
    mountImage(imageData, i, true, temp);

    if (temp.getIndex())
        gNetStringTable->removeString(temp.getIndex());

    return i;
}

void Marble::updatePowerUpParams()
{
    mPowerUpParams.init();
    mPowerUpParams.sizeScale *= mDataBlock->size;
    mPowerUpParams.bounce = mDataBlock->bounceRestitution;
    mPowerUpParams.airAccel = mDataBlock->airAcceleration;

    if (mDataBlock->powerUps)
    {
        for (S32 i = 0; i < PowerUpData::MaxPowerUps; i++)
        {
            if (mPowerUpState[i].active)
            {
                mPowerUpParams.airAccel *= mDataBlock->powerUps->airAccel[i];
                mPowerUpParams.gravityMod *= mDataBlock->powerUps->gravityMod[i];
                if (mDataBlock->powerUps->bounce[i] > 0.0f)
                    mPowerUpParams.bounce = mDataBlock->powerUps->bounce[i];

                F32 modRepulse;
                if (i == 0)
                    modRepulse = getBlastPercent(); // PowerUp 0 is always blast
                else
                    modRepulse = 1.0f;
                    
                mPowerUpParams.repulseDist = getMax(mPowerUpParams.repulseDist,
                                                    mDataBlock->powerUps->repulseDist[i] * modRepulse);
                mPowerUpParams.repulseMax += mDataBlock->powerUps->repulseMax[i] * modRepulse;
                mPowerUpParams.massScale *= mDataBlock->powerUps->massScale[i];
                mPowerUpParams.sizeScale *= mDataBlock->powerUps->sizeScale[i];
            }
        }
    }
    
    if (mPowerUpParams.repulseDist <= 0.0f)
        mTypeMask &= ~ForceObjectType;
    else
        mTypeMask |= ForceObjectType;

    updateMass();
}

bool Marble::getForce(Point3F& pos, Point3F* force)
{
    Point3F delta = pos - mPosition;
    F32 deltaLen = delta.len();

    if (mPowerUpParams.repulseDist < deltaLen)
        return false;

    if (deltaLen >= 0.05000000074505806)
    {
        F32 dist;
        if (deltaLen >= 1.0)
            dist = (1.0f / deltaLen - 1.0f / mPowerUpParams.repulseDist) * mPowerUpParams.repulseMax;
        else
            dist = mPowerUpParams.repulseMax / deltaLen;

        *force += delta * dist;
    } else
    {
        Point3F result;
        *force += mPowerUpParams.repulseMax * -getGravityDir(&result);
    }

    return true;
}

U32 Marble::packUpdate(NetConnection* conn, U32 mask, BitStream* stream)
{
    Parent::packUpdate(conn, mask, stream);

    bool isControl = false;

    // if it's the control object and the WarpMask is not set
    if (getControllingClient() == (GameConnection*)conn && (mask & WarpMask) == 0)
        isControl = true;

    bool gravityChange = false;
    if (isControl || (mask & GravityMask) == 0)
        gravityChange = (mask & (GravitySnapMask | WarpMask)) != 0;
    else
        gravityChange = true;

    stream->writeFlag(isControl);
    stream->writeFlag(gravityChange);

    if (stream->writeFlag((mask & PowerUpMask) != 0))
    {
        stream->writeRangedU32(mPowerUpId, 0, PowerUpData::MaxPowerUps - 1);
        
        for (S32 i = 0; i < PowerUpData::MaxPowerUps; i++)
        {
            if (stream->writeFlag(mPowerUpState[i].active))
            {
                stream->writeRangedU32(mPowerUpState[i].ticksLeft, 0, mDataBlock->powerUps->duration[i] >> 5);
            }
        }

        if (stream->writeFlag((mMode & ActiveModeMask) != mMode))
            stream->writeInt(mMode, 7);
        else
            stream->writeInt(mMode, 4);

        if (stream->writeFlag(mModeTimer != 0))
            stream->writeRangedU32(mModeTimer, 1, 31);

        if (stream->writeFlag(mPowerUpTimer != 0))
            stream->writeRangedU32(mPowerUpTimer >> 5, 1, 16);

        if (stream->writeFlag(mBlastTimer != 0))
            stream->writeRangedU32(mBlastTimer >> 5, 1, 16);

        if (mPowerUpState[0].active || mBlastTimer)
            stream->writeRangedU32(mBlastEnergy, 0, mDataBlock->blastRechargeTime >> 5);
    }

    if (isControl || gravityChange)
    {
        stream->writeFlag(mUseFullMarbleTime);
        stream->writeFlag(mOOB);

        if (gravityChange)
        {
            stream->write(mGravityFrame.x);
            stream->write(mGravityFrame.y);
            stream->write(mGravityFrame.z);

            stream->writeFlag(mGravityFrame.w < 0.0f);
            stream->writeFlag((mask & (GravitySnapMask | WarpMask)) != 0);
        }
    }

    if (!isControl)
    {
        if (getControllingClient() == (GameConnection*)conn && (mask & WarpMask) != 0)
            mask |= MoveMask;

        if (stream->writeFlag(mask != 0))
        {
            stream->writeFlag((mask & WarpMask) != 0);
            stream->writeFloat((mMouseY - -0.35f) / 1.85f, 12);
            stream->writeFloat(mMouseX / 6.283185307179586f, 12);
            stream->writeSignedFloat(mLastYaw, 12);

            Point3D vel = mVelocity;
            Point3D omega = mOmega;
            Point3F pos(mObjToWorld[3], mObjToWorld[7], mObjToWorld[11]);

            double err;
            if (gravityChange)
                err = 0.0002499999981373548;
            else
                err = 0.004999999888241291;

            stream->writeCompressedPointRP(pos, 7, gMarbleCompressDists, err);

            float maxRollVelocity = mDataBlock->maxRollVelocity;
            stream->writeVector(Point3F(vel.x, vel.y, vel.z), 0.0099999998f, maxRollVelocity + maxRollVelocity, 16, 16, 10);
            stream->writeVector(Point3F(omega.x, omega.y, omega.z), 0.0099999998f, 10.0f, 16, 16, 10);

            delta.move.pack(stream);
        }
    }

    return 0;
}

void Marble::unpackUpdate(NetConnection* conn, BitStream* stream)
{
    Parent::unpackUpdate(conn, stream);

    bool warp = stream->readFlag();
    bool isGravWarp = stream->readFlag();

    if (stream->readFlag())
    {
        mPowerUpId = stream->readRangedU32(0, PowerUpData::MaxPowerUps - 1);
        
        for (S32 i = 0; i < PowerUpData::MaxPowerUps; i++)
        {
            mPowerUpState[i].active = stream->readFlag();
            if (mPowerUpState[i].active)
            {
                mPowerUpState[i].ticksLeft = stream->readRangedU32(0, mDataBlock->powerUps->duration[i] >> 5);
            }
        }

        if (stream->readFlag())
            setMode(stream->readInt(7));
        else
            setMode(stream->readInt(4));

        if (stream->readFlag())
            mModeTimer = stream->readRangedU32(1, 31);
        else
            mModeTimer = 0;

        if (stream->readFlag())
            mPowerUpTimer = 32 * stream->readRangedU32(1, 16);
        else
            mPowerUpTimer = 0;

        if (stream->readFlag())
            mBlastTimer = 32 * stream->readRangedU32(1, 16);
        else
            mBlastTimer = 0;

        if (mPowerUpState[0].active || mBlastTimer)
            mBlastEnergy = stream->readRangedU32(0, mDataBlock->blastRechargeTime >> 5);

        updatePowerUpParams();
    }

    if (warp || isGravWarp)
    {
        mUseFullMarbleTime = stream->readFlag();
        mOOB = stream->readFlag();
    }

    bool doWarp = false;
    if (isGravWarp)
    {
        stream->read(&mGravityFrame.x);
        stream->read(&mGravityFrame.y);
        stream->read(&mGravityFrame.z);

        float wSq = 1.0f
                  - mGravityFrame.x * mGravityFrame.x
                  - mGravityFrame.y * mGravityFrame.y
                  - mGravityFrame.z * mGravityFrame.z;

        double w = 0.0f;
        if (wSq > 0.0f)
            w = sqrtf(wSq);
        mGravityFrame.w = w;

        if (stream->readFlag())
            mGravityFrame.w *= -1.0f;

        doWarp = stream->readFlag();

        if (doWarp)
        {
            mGravityRenderFrame.x = mGravityFrame.x;
            mGravityRenderFrame.y = mGravityFrame.y;
            mGravityRenderFrame.z = mGravityFrame.z;
            mGravityRenderFrame.w = mGravityFrame.w;
        }
    }

    if (!warp && stream->readFlag())
    {
        warp = stream->readFlag();

        mMouseY = stream->readFloat(12) * 1.85f - 0.35f;
        mMouseX = stream->readFloat(12) * 6.283185307179586;
        mLastYaw = stream->readSignedFloat(12);
        mCameraInit = false;

        float err;
        if (isGravWarp)
            err = 0.0002499999981373548;
        else
            err = 0.004999999888241291;

        Point3F pos;
        
        stream->readCompressedPointRP(&pos, 7, gMarbleCompressDists, err);

        Point3F vel;
        Point3F omega;
        double maxRollVelocity = mDataBlock->maxRollVelocity;
        stream->readVector(&vel, 0.0099999998, maxRollVelocity + maxRollVelocity, 16, 16, 10);
        stream->readVector(&omega, 0.0099999998, 10.0f, 16, 16, 10);

        mSinglePrecision.mVelocity = vel;
        mSinglePrecision.mOmega = omega;

        delta.move.unpack(stream);

        mOmega = mSinglePrecision.mOmega;
        mVelocity = mSinglePrecision.mVelocity;

        float nX = mPosition.x - pos.x;
        float nY = mPosition.y - pos.y;
        float nZ = mPosition.z - pos.z;

#ifdef MB_ULTRA_PREVIEWS
        if (getCurrentClientProcessList()->getLastDelta() <= 0.001 || warp)
#else
        if (ClientProcessList::get()->getLastDelta() <= 0.001 || warp)
#endif
        {
            delta.posVec = Point3D(0, 0, 0);
        }
        else
        {
#ifdef MB_ULTRA_PREVIEWS
            float invBD = 1.0f / getCurrentClientProcessList()->getLastDelta();
#else
            float invBD = 1.0f / ClientProcessList::get()->getLastDelta();
#endif
            float posX = nX * invBD;
            float posY = nY * invBD;
            float posZ = nZ * invBD;

            delta.posVec.x = posX + delta.posVec.x;
            delta.posVec.y = posY + delta.posVec.y;
            delta.posVec.z = posY + delta.posVec.z;
        }

        mMovePathSize = 0;
        delta.pos = pos;

        setPosition(pos, doWarp);
    }
}

U32 Marble::filterMaskBits(U32 mask, NetConnection* connection)
{
    if (getControllingClient() == (GameConnection*)connection)
        return mask & ~(MoveMask | PowerUpMask | CloakMask | NoWarpMask | ScaleMask);
    else
        return mask & ~(OOBMask | CloakMask | NoWarpMask | ScaleMask);
}

void Marble::writePacketData(GameConnection* conn, BitStream* stream)
{
    mathWrite(*stream, mSinglePrecision.mPosition);
    
    Point3F p(mPosition.x, mPosition.y, mPosition.z);
    stream->setCompressionPoint(p);

    mathWrite(*stream, mSinglePrecision.mVelocity);
    mathWrite(*stream, mSinglePrecision.mOmega);
    mathWrite(*stream, mGravityFrame);
    stream->write(mMouseX);
    stream->write(mMouseY);
    stream->write(mLastYaw);

    stream->write(mMarbleTime);

    if (stream->writeFlag(mMarbleTime != mFullMarbleTime))
        stream->write(mFullMarbleTime);
    
    if (stream->writeFlag(mMarbleBonusTime != 0))
        stream->writeRangedU32(mMarbleBonusTime, 0, 130000);
    
    stream->writeRangedU32(mBlastEnergy, 0, mDataBlock->blastRechargeTime >> 5);

    if (stream->writeFlag(mCenteringCamera))
    {
        stream->write(mRadsLeftToCenter);
        stream->write(mRadsStartingToCenter);
    }

    stream->writeRangedU32(mPowerUpId, 0, PowerUpData::MaxPowerUps - 1);

    S32 i;
    for (i = 0; i < PowerUpData::MaxPowerUps; i++)
    {
        if (stream->writeFlag(mPowerUpState[i].active))
        {
            stream->writeRangedU32(mPowerUpState[i].ticksLeft, 0, mDataBlock->powerUps->duration[i] >> 5);
        }
    }

    if (stream->writeFlag((mMode & ActiveModeMask) != mMode))
        stream->writeInt(mMode, 7);
    else
        stream->writeInt(mMode, 4);

    if (stream->writeFlag(mModeTimer != 0))
        stream->writeRangedU32(mModeTimer, 1, 31);
    
    if (stream->writeFlag(mPowerUpTimer != 0))
        stream->writeRangedU32(mPowerUpTimer >> 5, 1, 16);

    if (stream->writeFlag(mBlastTimer != 0))
        stream->writeRangedU32(mBlastTimer >> 5, 1, 16);
}

void Marble::readPacketData(GameConnection* conn, BitStream* stream)
{
    mathRead(*stream, &mSinglePrecision.mPosition);
    stream->setCompressionPoint(mSinglePrecision.mPosition);
    mathRead(*stream, &mSinglePrecision.mVelocity);
    mathRead(*stream, &mSinglePrecision.mOmega);
    mathRead(*stream, &mGravityFrame);

    stream->read(&mMouseX);
    stream->read(&mMouseY);
    stream->read(&mLastYaw);

    stream->read(&mMarbleTime);
    if (stream->readFlag())
        stream->read(&mFullMarbleTime);
    else
        mFullMarbleTime = mMarbleTime;

    if (stream->readFlag())
        mMarbleBonusTime = stream->readRangedU32(0, 130000);
    else
        mMarbleBonusTime = 0;

    mBlastEnergy = stream->readRangedU32(0, mDataBlock->blastRechargeTime >> 5);
    
    mCenteringCamera = stream->readFlag();
    if (mCenteringCamera)
    {
        stream->read(&mRadsLeftToCenter);
        stream->read(&mRadsStartingToCenter);
    }

    mPowerUpId = stream->readRangedU32(0, PowerUpData::MaxPowerUps - 1);

    S32 i;
    for (i = 0; i < PowerUpData::MaxPowerUps; i++)
    {
        mPowerUpState[i].active = stream->readFlag();
        if (mPowerUpState[i].active)
        {
            mPowerUpState[i].ticksLeft = stream->readRangedU32(0, mDataBlock->powerUps->duration[i] >> 5);
        }
    }

    if (stream->readFlag())
        setMode(stream->readInt(7));
    else
        setMode(stream->readInt(4));

    if (stream->readFlag())
        mModeTimer = stream->readRangedU32(1, 31);
    else
        mModeTimer = 0;

    if (stream->readFlag())
        mPowerUpTimer = stream->readRangedU32(1, 16) * 32;
    else
        mPowerUpTimer = 0;

    if (stream->readFlag())
        mBlastTimer = stream->readRangedU32(1, 16) * 32;
    else
        mBlastTimer = 0;

    mPosition = mSinglePrecision.mPosition;
    mVelocity = mSinglePrecision.mVelocity;
    mOmega = mSinglePrecision.mOmega;

    updatePowerUpParams();

    mMovePathSize = 0;

    delta.prevMouseX = mMouseX;
    delta.prevMouseY = mMouseY;

    mObjToWorld.setColumn(3, mSinglePrecision.mPosition);

    Parent::setTransform(mObjToWorld);
}

void Marble::renderShadowVolumes(SceneRenderState* state)
{
    // Empty
}

void Marble::renderShadow(F32 dist, F32 fogAmount)
{
    // Empty
}

void Marble::renderImage(SceneRenderState* state)
{
    // Empty
}

void Marble::bounceEmitter(F32 speed, const Point3F& normal)
{
#ifdef MB_ULTRA_PREVIEWS
    if ((isGhost() || gSPMode) && !mBounceEmitDelay)
#else
    if (isGhost() && !mBounceEmitDelay)
#endif
    {
        if (mDataBlock->bounceEmitter)
        {
            if (mDataBlock->minBounceSpeed <= speed)
            {
                ParticleEmitter* emitter = new ParticleEmitter;
                emitter->onNewDataBlock(mDataBlock->bounceEmitter, false);
                emitter->registerObject();
                
                emitter->emitParticles(mPosition - mRadius * normal, false, normal, mVelocity, getMin(speed * 100.0f, 2500.0f));

                emitter->deleteWhenEmpty();
                mBounceEmitDelay = 300;
            }
        }
    }
}

MatrixF Marble::getShadowTransform() const
{
    MatrixF result(true);
    mGravityRenderFrame.setMatrix(&result);

    result.setPosition(mRenderObjToWorld.getPosition());

    return result;
}

void Marble::setVelocity(const Point3F& vel)
{
    setVelocityD(vel);
}

Point3F Marble::getVelocity() const
{
    return mSinglePrecision.mVelocity;
}

Point3F Marble::getShadowScale() const
{
    return mRenderScale;
}

Point3F Marble::getGravityRenderDir()
{
    Point3F ret;
    mGravityRenderFrame.mulP(Point3F(0.0f, 0.0f, -1.0f), &ret);
    return ret;
}

void Marble::getShadowLightVectorHack(Point3F& lightVec)
{
    lightVec = getGravityRenderDir();
}

bool Marble::onSceneAdd()
{
    if (!Parent::onSceneAdd())
        return false;

/*#ifdef MB_ULTRA_PREVIEWS
    if ((isGhost() || gSPMode))
#else
    if (isGhost())
#endif
    {
        SceneGraphData sgData;
        sgData.useLightDir = true;
        sgData.useFog = SceneManager::renderFog;
        if (mStencilMaterial)
            delete mStencilMaterial;

        mStencilMaterial = new MatInstance("Material_Stencil");
        mStencilMaterial->init(sgData, GFXVertexFlagXYZ);
    }*/

    return true;
}

bool Marble::onNewDataBlock(GameBaseData* dptr, bool reload)
{
    if (!Parent::onNewDataBlock(dptr, reload))
        return false;

    mDataBlock = dynamic_cast<MarbleData*>(dptr);
    if (!mDataBlock)
        return false;
    
    updatePowerUpParams();

    if (this->mDataBlock->mShape)
    {
        updateMass();
        return true;
    }

    return false;
}

void Marble::onRemove()
{
    removeFromScene();

    SFX_DELETE(mRollHandle);
    SFX_DELETE(mSlipHandle);
    SFX_DELETE(mMegaHandle);

    Parent::onRemove();
}

bool Marble::updatePadState()
{
    // TODO: Cleanup Decompile
    
    bool result; // al
    int v4; // edi
    ConcretePolyList::Poly* v5; // esi
    float in_rRadius; // [esp+0h] [ebp-88h]
    Box3F box; // [esp+10h] [ebp-78h] BYREF
    float v10; // [esp+50h] [ebp-38h]
    float v11; // [esp+54h] [ebp-34h]
    float v12; // [esp+58h] [ebp-30h]
    Point3F boxCenter; // [esp+5Ch] [ebp-2Ch] BYREF
    Point3F v14; // [esp+68h] [ebp-20h]
    Point3F upDir; // [esp+74h] [ebp-14h] BYREF
    float dist; // [esp+80h] [ebp-8h]
    unsigned int i; // [esp+84h] [ebp-4h]

    Box3F pad(mPadPtr->getWorldBox());
    MatrixF v2 = mPadPtr->getTransform();
    upDir.x = v2[2];
    upDir.y = v2[6];
    upDir.z = v2[10];
    upDir.x = upDir.x * 10.0;
    upDir.y = upDir.y * 10.0;
    upDir.z = 10.0 * upDir.z;
    if (upDir.x <= 0.0)
        pad.minExtents.x = pad.minExtents.x + upDir.x;
    else
        pad.maxExtents.x = pad.maxExtents.x + upDir.x;
    if (upDir.y <= 0.0)
        pad.minExtents.y = pad.minExtents.y + upDir.y;
    else
        pad.maxExtents.y = pad.maxExtents.y + upDir.y;
    if (upDir.z <= 0.0)
        pad.minExtents.z = pad.minExtents.z + upDir.z;
    else
        pad.maxExtents.z = pad.maxExtents.z + upDir.z;
    result = mWorldBox.isOverlapped(pad);
    if (result)
    {
        v14.x = this->mPosition.x;
        v14.y = this->mPosition.y;
        v14.z = this->mPosition.z;
        v10 = this->mObjBox.minExtents.x + v14.x;
        v11 = this->mObjBox.minExtents.y + v14.y;
        v12 = this->mObjBox.minExtents.z + v14.z;
        pad.maxExtents.x = v10 - 0.0;
        pad.maxExtents.y = v11 - 0.0;
        pad.maxExtents.z = v12 - 10.0;
        box.minExtents.x = pad.maxExtents.x;
        v10 = this->mPosition.x;
        box.minExtents.y = pad.maxExtents.y;
        v11 = this->mPosition.y;
        v12 = this->mPosition.z;
        box.minExtents.z = pad.maxExtents.z;
        pad.maxExtents.x = this->mObjBox.maxExtents.x + v10;
        pad.maxExtents.y = this->mObjBox.maxExtents.y + v11;
        pad.maxExtents.z = this->mObjBox.maxExtents.z + v12;
        v14.x = pad.maxExtents.x + 0.0;
        v14.y = pad.maxExtents.y + 0.0;
        v14.z = pad.maxExtents.z + 10.0;
        box.maxExtents = v14;
        box.getCenter(&boxCenter);
        pad.maxExtents.x = v14.x - boxCenter.x;
        pad.maxExtents.y = v14.y - boxCenter.y;
        pad.maxExtents.z = v14.z - boxCenter.z;
        in_rRadius = pad.maxExtents.len();
        SphereF sphere(boxCenter, in_rRadius);
        polyList.clear();
        mPadPtr->buildPolyList(PLC_Collision, &Marble::polyList, box, sphere);
        v4 = 0;
        i = 0;
        if (!polyList.mPolyList.empty())
        {
            while (true)
            {
                v5 = &Marble::polyList.mPolyList[v4];
                pad.maxExtents.x = upDir.x * -10.0;
                pad.maxExtents.y = upDir.y * -10.0;
                pad.maxExtents.z = -10.0 * upDir.z;
                if (mDot(polyList.mPolyList[v4].plane, pad.maxExtents) < 0.0)
                {
                    dist = v5->plane.distToPlane(boxCenter);
                    if (dist >= 0.0 && dist < 5.0 && pointWithinPolyZ(*v5, boxCenter, upDir))
                        break;
                }
                ++i;
                ++v4;
                if (i >= Marble::polyList.mPolyList.size())
                    goto LABEL_17;
            }
            this->mOnPad = true;
            result = true;
        }
        else
        {
        LABEL_17:
            this->mOnPad = false;
            result = false;
        }
    }
    return result;
}

void Marble::doPowerUpBoost(S32 powerUpId)
{
    if (mDataBlock->powerUps)
    {
        PowerUpData* powerUps = mDataBlock->powerUps;
        Point3F boostDir = powerUps->boostDir[powerUpId];
        if (mDot(boostDir, boostDir) > 0.0099999998f)
        {
            F32 xboost = mDot(boostDir, Point3F(1.0f, 0.0f, 0.0f));
            F32 yboost = mDot(boostDir, Point3F(0.0f, 1.0f, 0.0f));
            F32 zboost = mDot(boostDir, Point3F(0.0f, 0.0f, 1.0f));

            MatrixF zRot;
            m_matF_set_euler(Point3F(0.0f, 0.0f, mMouseX), zRot);

            MatrixF gravMat;
            mGravityFrame.setMatrix(&gravMat);
            gravMat.mul(zRot);

            Point3F xThing(gravMat[0], gravMat[4], gravMat[8]);
            Point3F yThing(gravMat[1], gravMat[5], gravMat[9]);

            Point3F gravDir;
            Point3F invGravDir = -getGravityDir(&gravDir);

            F32 masslessa = mDot((Point3F)mBestContact.normal, yThing);

            yThing -= mBestContact.normal * masslessa;

            if (mDot(yThing, yThing) >= 0.0099999998)
                m_point3F_normalize(yThing);
            else
                yThing.set(gravMat[1], gravMat[5], gravMat[9]);

            Point3F zBoostThing = invGravDir * zboost;
            Point3F yBoostThing = yThing * yboost;
            Point3F xBoostThing = xThing * xboost;
            
            Point3F boostDir = xBoostThing + yBoostThing + zBoostThing;

            m_point3F_normalize(boostDir);

            F32 masslessb = powerUps->boostMassless[powerUpId];

            F32 massless = getMass() * masslessb + 1.0f - masslessb;

            if (powerUpId == 0) // Blast
                massless = getMin(1.0f, getBlastPercent()) * massless;

            F32 amount = powerUps->boostAmount[powerUpId];

            applyImpulse(Point3F(0, 0, 0), boostDir * amount * massless);
        }
    }
}

void Marble::doPowerUpPower(S32 powerUpId)
{
    // TODO: Cleanup Decompile

    char* v3; // eax
    int v4; // edi
    char* v5; // eax

    if (this->mDataBlock->powerUps)
    {
        mPowerUpState[powerUpId].active = true;
        mPowerUpState[powerUpId].ticksLeft = mDataBlock->powerUps->duration[powerUpId] >> 5;

        updatePowerUpParams();
#ifdef MB_ULTRA_PREVIEWS
        if (isServerObject() && !gSPMode)
#else
        if (isServerObject())
#endif
        {
            v4 = this->mDataBlock->powerUps->timeFreeze[powerUpId];
            if (v4)
            {
                v5 = Con::getIntArg(v4);
                Con::executef(this, "onTimeFreeze", v5);
            }
        }
        setMaskBits(PowerUpMask);
    }
}

void Marble::updatePowerups()
{
    bool expired = false;

    for (S32 i = 0; i < PowerUpData::MaxPowerUps; i++)
    {
        if (mPowerUpState[i].active)
        {
            mPowerUpState[i].ticksLeft--;
            if (mPowerUpState[i].ticksLeft == 0)
            {
#ifdef MB_ULTRA_PREVIEWS
                if (isServerObject() && !gSPMode)
#else
                if (isServerObject())
#endif
                    Con::executef(this, "onPowerUpExpired", Con::getIntArg(i));

                mPowerUpState[i].active = false;
                expired = true;
            }
        }
    }

    if(expired)
        updatePowerUpParams();
}

void Marble::updateMass()
{
    Parent::updateMass();

    if (!mDataBlock)
        return;

    mMass = mPowerUpParams.massScale * mMass;
    mOneOverMass = 1.0f / mMass;

    setScale(Point3F(mPowerUpParams.sizeScale, mPowerUpParams.sizeScale, mPowerUpParams.sizeScale));

    TSShape* shape = mDataBlock->mShape;

    Box3F bounds = shape->mBounds;
    Point3F extents(bounds.maxExtents.x + bounds.minExtents.x, bounds.maxExtents.y + bounds.minExtents.y, bounds.maxExtents.z + bounds.minExtents.z);
    Point3F center = extents * 0.5f;
    Point3F scale(bounds.maxExtents.x - bounds.minExtents.x, bounds.maxExtents.y - bounds.minExtents.y, bounds.maxExtents.z - bounds.minExtents.z);
    extents = scale * 0.5f;

    extents *= mPowerUpParams.sizeScale;

    mObjBox.minExtents = center - extents;
    mObjBox.maxExtents = extents + center;

    mRadius = getMax(getMax(mObjBox.maxExtents.x, mObjBox.maxExtents.y), mObjBox.maxExtents.z);

    resetWorldBox();
}

void Marble::trailEmitter(U32 timeDelta)
{
#ifdef MB_ULTRA_PREVIEWS
    if (!(isGhost() || gSPMode) || !mDataBlock->trailEmitter)
#else
    if (!isGhost() || !mDataBlock->trailEmitter)
#endif
        return;

    F32 speed = mVelocity.len();

    if (mDataBlock->minTrailSpeed > speed)
    {
        if (!mTrailEmitter)
            return;
        mTrailEmitter->deleteWhenEmpty();
        mTrailEmitter = NULL;
        
        return;
    }

    if (!mTrailEmitter)
    {
        mTrailEmitter = new ParticleEmitter;
        mTrailEmitter->onNewDataBlock(mDataBlock->trailEmitter, false);
        mTrailEmitter->registerObject();

        if (!mTrailEmitter)
            return;
    }

    if (mDataBlock->minTrailSpeed * 2 > speed)
        timeDelta = (U32)((speed - mDataBlock->minTrailSpeed) / mDataBlock->minTrailSpeed * (F32)timeDelta);

    Point3F normal = mVelocity;
    m_point3F_normalize(normal);

    Point3F pos = mLastRenderPos;
    pos.x += mRadius * ((gRandGen.randF() * 2) - 1.0f);
    pos.y += mRadius * ((gRandGen.randF() * 2) - 1.0f);
    pos.z += mRadius * ((gRandGen.randF() * 2) - 1.0f);

    mTrailEmitter->emitParticles(pos, true, normal, mVelocity, timeDelta);
}

void Marble::updateRollSound(F32 contactPct, F32 slipAmount)
{
    if (mRollHandle && mSlipHandle && mMegaHandle)
    {
        Point3F marblePos = mPosition;

        mRollHandle->setPosition(marblePos);
        mRollHandle->setVelocity(VectorF(0, 0, 0));
        
        mSlipHandle->setPosition(marblePos);
        mSlipHandle->setVelocity(VectorF(0, 0, 0));
        
        mMegaHandle->setPosition(marblePos);
        mMegaHandle->setVelocity(VectorF(0, 0, 0));

        float scale = mDataBlock->size;
        float megaAmt = (this->mRenderScale.x - scale) / (mDataBlock->megaSize - scale);
        float regAmt = 1.0 - megaAmt;

        Point3D rollVel = mVelocity - mBestContact.surfaceVelocity;

        scale = rollVel.len();
        scale = scale / mDataBlock->maxRollVelocity;

        float rollVolume = scale + scale;
        if (rollVolume > 1.0)
            rollVolume = 1.0;

        if (contactPct < 0.05)
            rollVolume = 0.0;

        float slipVolume = 0.0;
        if (slipAmount > 0.0)
        {
            slipVolume = slipAmount / 5.0;
            if (slipVolume > 1.0)
                slipVolume = 1.0;
            rollVolume = (1.0 - slipVolume) * rollVolume;
        }
        mRollHandle->setVolume(rollVolume * regAmt);
        mMegaHandle->setVolume(rollVolume * megaAmt);
        mSlipHandle->setVolume(slipVolume);
        
        if (!mRollHandle->isPlaying())
            mRollHandle->play();

        if (!mMegaHandle->isPlaying())
            mMegaHandle->play();

        if (!mSlipHandle->isPlaying())
            mSlipHandle->play();

        float pitch = scale;
        if (scale > 1.0f)
            pitch = 1.0f;

        mRollHandle->setPitch(pitch * 0.75f + 0.75f);
    }
}

void Marble::playBounceSound(Marble::Contact& contactSurface, F64 contactVel)
{
    bool mega;
    F32 softBounceSpeed;

    F32 minSize = mDataBlock->size;
    if ((mRenderScale.x - minSize) / (mDataBlock->megaSize - minSize) <= 0.5f)
    {
        softBounceSpeed = mDataBlock->minVelocityBounceSoft;
        mega = false;
    } else
    {
        softBounceSpeed = mDataBlock->minVelocityMegaBounceSoft;
        mega = true;
    }

    if (softBounceSpeed <= contactVel)
    {
        F32 hardBounceSpeed = mega ? mDataBlock->minVelocityMegaBounceHard : mDataBlock->minVelocityBounceHard;
        F32 bounceSoundNum = mFloor(Platform::getRandom() * 4);
        if (4 * (mega != 0) + bounceSoundNum <= 7)
        {
            S32 soundIndex = 4 * (mega != 0) + bounceSoundNum + MarbleData::Bounce1;
            if (mDataBlock->sound[soundIndex])
            {
                F32 gain;
                if (mega)
                    gain = mDataBlock->bounceMegaMinGain;
                else
                    gain = mDataBlock->bounceMinGain;

                if (hardBounceSpeed <= contactVel)
                    gain = 1.0f;
                else
                    gain = (contactVel - softBounceSpeed) / (hardBounceSpeed - softBounceSpeed) * (1.0f - gain) + gain;

                MatrixF mat(true);
                mat.setColumn(3, getPosition());
                SFXSource* source = SFX->playOnce(mDataBlock->sound[soundIndex], &mat);

                if (source)
                {
                    source->setVolume(gain);
                    source->play();
                }
            }
        }
    }
}

void Marble::setPad(SceneObject* obj)
{
    mPadPtr = obj;
    if (obj)
        mOnPad = updatePadState();
    else
        mOnPad = false;
}

void Marble::findRenderPos(F32 dt)
{
    F32 startTime = 0.0f;
    F32 dist = 1.0f;

    Point3F posVec = delta.posVec;
    Point3F dPos = delta.pos;
    Point3F around = posVec + dPos;

#ifdef MB_ULTRA_PREVIEWS
    float outforce = 1.0f - getCurrentClientProcessList()->getLastDelta();
#else
    float outforce = 1.0f - ClientProcessList::get()->getLastDelta();
#endif

    if (mMovePathSize != 0)
    {
        // TODO: Cleanup decompile
        U32 iter = 0;
        while (outforce > mMovePathTime[iter])
        {
            startTime = mMovePathTime[iter];
            around = mMovePath[iter];
            
            if (++iter >= mMovePathSize)
                goto LABEL_7;
        }

        dist = mMovePathTime[iter];
        dPos = mMovePath[iter];
    }
LABEL_7:

    F32 diff = (outforce - startTime) / (dist - startTime);

    Point3F f = dPos * diff;
    diff = 1.0f - diff;

    Point3F pos = diff * around + f;

    if (getControllingClient() != NULL)
    {
        pos += mNetSmoothPos;
    }
    else
    {
        static bool init = false;
        static F32 smooth1 = 0.0f;
        static F32 smooth2 = 0.0f;
        if (!init)
        {
            Con::addVariable("Marble::smooth1", TypeF32, &smooth1);
            Con::addVariable("Marble::smooth2", TypeF32, &smooth2);
            init = true;
        }

        Point3F posDiff = pos - mLastRenderPos;

        if (mDot(posDiff, posDiff) < 10.0f)
        {
            Point3F velocity = mLastRenderVel;
            velocity *= smooth2;

            Point3F nPos = mLastRenderPos + velocity;
            nPos *= smooth1;

            pos = (pos * 1.0 - smooth1) + nPos;
        }
    }

    if ((mMode & StoppingMode) != 0 && !Marble::smEndPad.isNull())
    {
        MatrixF trans = Marble::smEndPad->getTransform();

        Point3F around(trans[3], trans[7], trans[11]);
        Point3F forceEffect(trans[2], trans[6], trans[10]);
        around += forceEffect;
        
        Point3F* pLastRenderVel = &mLastRenderVel;

        Point3F offset = mLastRenderPos - around;

#ifdef MBU_FINISH_PAD_FIX
        *pLastRenderVel *= 1 - 0.05f * (60.0f * dt);
#else
        *pLastRenderVel *= 0.949999988079071f;
#endif // MBU_FINISH_PAD_FIX

        Point3F thing2 = forceEffect * 0.2000000029802322f;

#ifdef MBU_FINISH_PAD_FIX
        *pLastRenderVel += thing2 * (60.0f * dt);
#else
        *pLastRenderVel += thing2;
#endif // MBU_FINISH_PAD_FIX

        F32 dist = offset.len();

#ifdef MBU_FINISH_PAD_FIX
        *pLastRenderVel -= offset * 0.3499999940395355f * (60.0f * dt);
#else
        *pLastRenderVel -= offset * 0.3499999940395355f;
#endif // MBU_FINISH_PAD_FIX

        if (dist > 1.5f)
        {
            Point3F unk = offset * (1.0f / dist);
            Point3F outward = unk;
            F32 outforce = mDot(outward, *pLastRenderVel);
            if (outforce > 0.0f)
            {
#ifdef MBU_FINISH_PAD_FIX
                *pLastRenderVel -= unk * outforce * 0.75f * (60.0f * dt);
#else
                *pLastRenderVel -= unk * outforce * 0.75f;
#endif // MBU_FINISH_PAD_FIX

                Point3F noodles = offset * outforce * 0.5f;
                noodles = *pLastRenderVel - noodles;
                
                if (mDot(noodles, noodles) <= 0.1000000014901161f &&
                    (mCross(outward, forceEffect, &noodles), mDot(noodles, noodles) <= 0.1000000014901161f))
                {
                    noodles.set(trans[0], trans[4], trans[8]);
                } else
                {
                    m_point3F_normalize(noodles);
                }

#ifdef MBU_FINISH_PAD_FIX
                *pLastRenderVel += noodles * outforce * 0.25f * (60.0f * dt);
#else
                *pLastRenderVel += noodles * outforce * 0.25f;
#endif // MBU_FINISH_PAD_FIX
            }
        }

        Point3F addVel(mSin(mEffect.effectTime * 3.0f),
                     mSin(mEffect.effectTime * 3.0f + 2.450442179918352f),
                     mSin(mEffect.effectTime * 1.5f + 1.19380519338384f));

        *pLastRenderVel += addVel * 2.5f * dt;
        
        pos = mLastRenderPos;
        pos += *pLastRenderVel * dt;

        disableCollision();

        Point3D position = mLastRenderPos;
        Point3F velocity = pos - mLastRenderPos;

        F64 time = 1.0;
        F64 totalTime = time;

        S32 i = 0;
        do
        {
            if (totalTime <= 0.0)
                break;
            if (!testMove(velocity, position, time, mRadius, sCameraCollisionMask, true))
                break;

            ++i;
            totalTime -= time;
            time = totalTime;

            pos = position;

            Point3F normal = mLastContact.normal;

            Point3F newAround = normal * 1.5f;
            newAround *= mDot(*pLastRenderVel, normal);

#ifdef MBU_FINISH_PAD_FIX
            *pLastRenderVel -= newAround * (60.0f * dt);
#else
            *pLastRenderVel -= newAround;
#endif // MBU_FINISH_PAD_FIX

        } while (i < 4);

        enableCollision();

        mLastRenderPos = pos;
        mEffect.effectTime += dt;
    }
    else
    {
        if ((mMode & StoppingMode) == 0)
        {
            mLastRenderPos = pos;
            mLastRenderVel = mVelocity;
        }
        mEffect.effectTime = 0.0f;
    }
}

void Marble::advanceTime(F32 dt)
{
    Parent::advanceTime(dt);

    F32 deltaTime = dt * 1000.0f;

    if (mBlastEnergy >= mDataBlock->maxNaturalBlastRecharge >> 5)
        mRenderBlastPercent = getMin(F32((F32)mBlastEnergy / (mDataBlock->blastRechargeTime >> 5)), dt * 0.75f + mRenderBlastPercent);
    else
        mRenderBlastPercent = (F32)mBlastEnergy / (mDataBlock->blastRechargeTime >> 5);

    F32 newDt = dt / 0.4f * 2.302585124969482f;
    F32 smooth = 1.0f / (newDt * (newDt * 0.235f * newDt) + newDt + 1.0f + 0.48f * newDt * newDt);
    QuatF interp;
    interp.interpolate(mGravityFrame, mGravityRenderFrame, smooth);
    interp.normalize();
    mGravityRenderFrame = interp;

    mNetSmoothPos *= smooth;
    findRenderPos(dt);
    mRenderObjToWorld.setColumn(3, mLastRenderPos);
    setRenderTransform(mRenderObjToWorld);

    if (mDataBlock->powerUps)
    {
        for (S32 i = 0; i < PowerUpData::MaxPowerUps; i++)
        {
            if (mDataBlock->powerUps->emitter[i])
            {
                if (mPowerUpState[i].active)
                {
                    if (mPowerUpState[i].emitter.isNull())
                    {
                        mPowerUpState[i].emitter = new ParticleEmitter;
                        mPowerUpState[i].emitter->onNewDataBlock(mDataBlock->powerUps->emitter[i], false);
                        mPowerUpState[i].emitter->registerObject();
                    }
                } else
                {
                    if (!mPowerUpState[i].emitter.isNull())
                    {
                        mPowerUpState[i].emitter->deleteWhenEmpty();
                        mPowerUpState[i].emitter = NULL;
                    }
                }
            }

            if (!mPowerUpState[i].emitter.isNull())
            {
                Point3F renderP = mVelocity;
                m_point3F_normalize(renderP);

                mPowerUpState[i].emitter->emitParticles(mLastRenderPos, true, renderP, mVelocity, deltaTime);
            }

            if (mPowerUpState[i].active || (i == 0 && mBlastTimer))
            {
                if (mPowerUpState[i].imageSlot < 0)
                    mPowerUpState[i].imageSlot = mountPowerupImage(mDataBlock->powerUps->image[i]);
            } else
            {
                if (mPowerUpState[i].imageSlot >= 0)
                {
                    unmountImage(mPowerUpState[i].imageSlot);
                    mPowerUpState[i].imageSlot = -1;
                }
            }
        }
    }

    trailEmitter(deltaTime);
    bool resetBounceEmitDelay = mBounceEmitDelay - deltaTime < 0;
    mBounceEmitDelay--;
    if (resetBounceEmitDelay)
        mBounceEmitDelay = 0;

    if (mOmega.len() > 0.000001)
    {
        Point3D omegaDelta = mOmega * dt;

        if (mEffect.effectTime > 1.0f)
            omegaDelta *= 1.0f / mEffect.effectTime;

        rotateMatrix(mObjToWorld, omegaDelta);

        Point3F renderPos(mRenderObjToWorld[3], mRenderObjToWorld[7], mRenderObjToWorld[11]);

        dMemcpy(mRenderObjToWorld, mObjToWorld, sizeof(MatrixF));
        mRenderObjToWorld.setColumn(3, renderPos);
    }
}

void Marble::computeNetSmooth(F32 backDelta)
{
    mNetSmoothPos.set(0, 0, 0);

    Point3F oldPos = mLastRenderPos;

    findRenderPos(0.0f);

    mNetSmoothPos = oldPos - mLastRenderPos;
}

void Marble::doPowerUp(S32 powerUpId)
{
    doPowerUpBoost(powerUpId);
    doPowerUpPower(powerUpId);
}

void Marble::prepShadows()
{
    // Serves no purpose in MBO but was left over from MBU
    // Not really worth decompiling at this time.
}

bool Marble::onAdd()
{
    if (!Parent::onAdd())
        return false;

#ifdef MB_ULTRA_PREVIEWS
    if (isGhost() || gSPMode)
#else
    if (isGhost())
#endif
    {
        Point3F velocity = Point3F(0, 0, 0);
        mRollHandle = SFX->createSource(mDataBlock->sound[0], &getTransform(), &velocity);
        mSlipHandle = SFX->createSource(mDataBlock->sound[3], &getTransform(), &velocity);
        mMegaHandle = SFX->createSource(mDataBlock->sound[1], &getTransform(), &velocity);

        this->mVertBuff.set(GFX, 33, GFXBufferTypeStatic);
        this->mPrimBuff.set(GFX, 33, 2, GFXBufferTypeStatic);
        prepShadows();
    }

    addToScene();

    return true;
}

void Marble::processMoveTriggers(const Move * move)
{
    // TODO: Cleanup Decompile

    unsigned int v3; // ecx
    char v4; // al
    unsigned int v5; // eax

    if (move->trigger[0] && mPowerUpId || mPowerUpTimer)
    {
        mPowerUpTimer += 32;
        if (mPowerUpTimer > 0x200)
            mPowerUpTimer = 512;
        v3 = mDataBlock->powerUps->activateTime[mPowerUpId];
        if (v3 > 0x200)
            v3 = 512;
        if (mPowerUpTimer >= v3)
        {
            doPowerUp(mPowerUpId);
            mPowerUpId = 0;
#ifdef MB_ULTRA_PREVIEWS
            if (isServerObject() && !gSPMode)
#else
            if (isServerObject())
#endif
                Con::executef(this, "onPowerUpUsed");
            mPowerUpTimer = 0;
        }
        setMaskBits(0x4000000u);
    }
    if (move->trigger[1] && getBlastPercent() > 0.25f || mBlastTimer)
    {
        if (!mBlastTimer)
            doPowerUpBoost(0);
        mBlastTimer += 32;
        if (mBlastTimer > 0x200)
            mBlastTimer = 512;
        v5 = mDataBlock->powerUps->activateTime[0];
        if (v5 > 0x200)
            v5 = 512;
        if (mBlastTimer >= v5)
        {
            doPowerUpPower(0);
#ifdef MB_ULTRA_PREVIEWS
            if (isServerObject() && !gSPMode)
#else
            if (isServerObject())
#endif
                Con::executef(this, "onBlastUsed");
            mBlastEnergy = 0;
            mBlastTimer = 0;
        }
        setMaskBits(0x4000000u);
    }
    if (move->trigger[3])
        startCenterCamera();
}

void Marble::processItemsAndTriggers(const Point3F& startPos, const Point3F& endPos)
{
    // TODO: Cleanup decompile

    double v5; // st7
    double v6; // st7
    double v7; // st7
    double v8; // st7
    double v9; // st7
    double v10; // st7
    double v11; // st7
    SceneContainer* v12; // ecx
    unsigned int i; // ebx
    SceneObject* v14; // ecx
    unsigned int v15; // eax
    Box3F box; // [esp+10h] [ebp-5Ch] BYREF
    Point3F in_rMin; // [esp+28h] [ebp-44h] BYREF
    Point3F in_rMax; // [esp+34h] [ebp-38h] BYREF
    SimpleQueryList sql; // [esp+40h] [ebp-2Ch] BYREF
    float expansion; // [esp+4Ch] [ebp-20h]
    float v21; // [esp+50h] [ebp-1Ch]
    float v22; // [esp+54h] [ebp-18h]
    float v23; // [esp+58h] [ebp-14h]
    float v24; // [esp+5Ch] [ebp-10h]
    int v25; // [esp+68h] [ebp-4h]
    float startPosa; // [esp+74h] [ebp+8h]
    float startPosb; // [esp+74h] [ebp+8h]
    float startPosc; // [esp+74h] [ebp+8h]
    float endPosa; // [esp+78h] [ebp+Ch]
    float endPosb; // [esp+78h] [ebp+Ch]
    float endPosc; // [esp+78h] [ebp+Ch]

    if (endPos.z <= (double)startPos.z)
        v5 = endPos.z;
    else
        v5 = startPos.z;
    v21 = v5;
    if (endPos.y <= (double)startPos.y)
        v6 = endPos.y;
    else
        v6 = startPos.y;
    v22 = v6;
    if (endPos.x <= (double)startPos.x)
        v7 = endPos.x;
    else
        v7 = startPos.x;
    v23 = v7;
    if (endPos.z >= (double)startPos.z)
        v8 = endPos.z;
    else
        v8 = startPos.z;
    v24 = v8;
    if (endPos.y >= (double)startPos.y)
        v9 = endPos.y;
    else
        v9 = startPos.y;
    endPosa = v9;
    if (endPos.x >= (double)startPos.x)
        v10 = endPos.x;
    else
        v10 = startPos.x;
    startPosa = v10;
    expansion = this->mRadius + 0.2000000029802322;
    v11 = expansion;
    startPosb = startPosa + expansion;
    endPosb = endPosa + expansion;
    expansion = v24 + expansion;
    in_rMax.x = startPosb;
    in_rMax.y = endPosb;
    in_rMax.z = expansion;
    startPosc = v23 - v11;
    endPosc = v22 - v11;
    expansion = v21 - v11;
    in_rMin.x = startPosc;
    in_rMin.y = endPosc;
    in_rMin.z = expansion;
    box = Box3F(in_rMin, in_rMax, 0);
    
    v25 = 0;
    mContainer->findObjects(box, sTriggerItemMask, SimpleQueryList::insertionCallback, &sql);
    for (i = 0; i < sql.mList.size(); ++i)
    {
        v14 = sql.mList[i];
        if ((v14->getTypeMask() & TriggerObjectType) != 0)
        {
#ifdef MB_ULTRA_PREVIEWS
            if (isServerObject() && !gSPMode)
#else
            if (isServerObject())
#endif
                ((Trigger*)v14)->potentialEnterObject(this);
        }
        else if ((v14->getTypeMask() & ItemObjectType) != 0 && this != ((Item*)v14)->getCollisionObject())
        {
            in_rMin.x = 0.0;
            in_rMin.y = 0.0;
            in_rMin.z = 0.0;
            queueCollision((Item*)v14, in_rMin, 0);
        }
    }
    v25 = -1;
}

void Marble::setPowerUpId(U32 id, bool reset)
{
    // TODO: Cleanup Decompile
    
    Marble::PowerUpState* v7; // eax
    int v8; // ecx
    
    if (mDataBlock && mDataBlock->powerUps != NULL && mDataBlock->powerUps->blastRecharge[id])
    {
        this->mBlastEnergy = mDataBlock->blastRechargeTime >> 5;
        doPowerUp(id);
    }
    else
    {
        this->mPowerUpId = id;
    }
    if (reset)
    {
        v7 = this->mPowerUpState;
        v8 = 10;
        do
        {
            v7->active = 0;
            ++v7;
            --v8;
        }     while (v8);
        updatePowerUpParams();
    }
    setMaskBits(PowerUpMask);
}

void Marble::processTick(const Move* move)
{
    Parent::processTick(move);

    clearMarbleAxis();
    if ((mMode & TimerMode) != 0)
    {
        U32 bonusTime = mMarbleBonusTime;
        if (bonusTime <= 32)
        {
            mMarbleBonusTime = 0;
            mMarbleTime += 32 - bonusTime;
            if ((mMode & (MoveMode | StoppingMode)) == 0)
                setMode(StartingMode);
        }
        else
            mMarbleBonusTime = bonusTime - 32;

        mFullMarbleTime += 32;
    }

    if (mModeTimer)
    {
        mModeTimer--;
        if (!mModeTimer)
        {
            if ((mMode & StartingMode) != 0)
                mMode = mMode & ~(RestrictXYZMode | CameraHoverMode) | (MoveMode | TimerMode);
            if ((mMode & StoppingMode) != 0)
                mMode &= ~MoveMode;
            if ((mMode & FinishMode) != 0)
                mMode |= CameraHoverMode;
            mMode &= ~(StartingMode | FinishMode);
        }
    }

    if (mBlastEnergy < mDataBlock->maxNaturalBlastRecharge >> 5)
        mBlastEnergy++;

    const Move* newMove;
    if (mControllable)
    {
        if (move)
        {
            dMemcpy(&delta.move, move, sizeof(delta.move));
            newMove = move;  
        }
        else
            newMove = &delta.move;
    }
    else
        newMove = &NullMove;

    processMoveTriggers(newMove);
    processCameraMove(newMove);

    Point3F startPos(mPosition.x, mPosition.y, mPosition.z);

    advancePhysics(newMove, TickMs);

    Point3F endPos(mPosition.x, mPosition.y, mPosition.z);

    processItemsAndTriggers(startPos, endPos);
    updatePowerups();

    if (mPadPtr)
    {
        bool oldOnPad = mOnPad;
        updatePadState();
#ifdef MB_ULTRA_PREVIEWS
        if (oldOnPad != mOnPad && !(isGhost() || gSPMode))
#else
        if (oldOnPad != mOnPad && !isGhost())
#endif
        {
            const char* funcName = "onLeavePad";
            if (!oldOnPad)
                funcName = "onEnterPad";
            Con::executef(mDataBlock, funcName, this);//scriptThis());
        }
    }

#ifdef MB_ULTRA_PREVIEWS
    if (isGhost() || gSPMode)
#else
    if (isGhost())
#endif
    {
        if (getControllingClient())
        {
            if (Marble::smEndPad.isNull() || Marble::smEndPad->getId() != Marble::smEndPadId)
            {
                if (Marble::smEndPadId && Marble::smEndPadId != -1)
                    Marble::smEndPad = dynamic_cast<StaticShape*>(Sim::findObject(Marble::smEndPadId));
                
                if (Marble::smEndPad.isNull())
                    Marble::smEndPadId = 0;
            }
        }
    }

    if (mOmega.len() < 0.000001)
        mOmega.set(0, 0, 0);

#ifdef MB_ULTRA_PREVIEWS
    if (!(isGhost() || gSPMode) && mOOB && newMove->trigger[2])
#else
    if (!isGhost() && mOOB && newMove->trigger[2])
#endif
        Con::executef(this, "onOOBClick");

    notifyCollision();

    mSinglePrecision.mPosition = mPosition;
    mSinglePrecision.mVelocity = mVelocity;
    mSinglePrecision.mOmega = mOmega;

    mPosition = mSinglePrecision.mPosition;
    mVelocity = mSinglePrecision.mVelocity;
    mOmega = mSinglePrecision.mOmega;
}

//----------------------------------------------------------------------------

DefineEngineMethod(Marble, setPosition, void, (TransformF transform, F32 mouseY),, "(transform, mouseY)")
{
    Point3F posf = transform.getPosition();
    AngAxisF angAxis = transform.getOrientation();

    // Without this, if an axis is not provided, these are NaN
    //angAxis.axis.set(0, 0, 0);
    //angAxis.angle = 0;

    //dSscanf(argv[2], "%f %f %f %f %f %f %f", &posf.x, &posf.y, &posf.z, &angAxis.axis.x, &angAxis.axis.y, &angAxis.axis.z, &angAxis.angle);

    object->setPosition(Point3D(posf.x, posf.y, posf.z), angAxis, mouseY);
}

DefineEngineMethod(Marble, setGravityDir, void, (const char* gravity, bool snap), (false), "(gravity, snap)")
{
    Point3F xvec;
    Point3F yvec;
    Point3F zvec;
    dSscanf(gravity, "%g %g %g %g %g %g %g %g %g", &xvec.x, &xvec.y, &xvec.z, &yvec.x, &yvec.y, &yvec.z, &zvec.x, &zvec.y, &zvec.z);

    MatrixF mat1(true);
    MatrixF mat2(true);
    object->getGravityFrame().setMatrix(&mat1);

    if (snap)
        mat1.identity();

    mat2.setColumn(0, xvec);
    mat2.setColumn(1, -yvec);
    mat2.setColumn(2, -zvec);

    Point3F sideDir(mat1[2], mat1[6], mat1[10]);
    Point3F upDir = -zvec;
    Point3F rot;
    mCross(sideDir, upDir, &rot);
    
    float len = rot.len();
    if (len <= 0.1000000014901161f)
    {
        sideDir.x = mat1[0];
        sideDir.y = mat1[4];
        sideDir.z = mat1[8];
        mat2[0] = mat1[0];
        mat2[4] = mat1[4];
        mat2[8] = mat1[8];
        upDir.x = mat2[2];
        upDir.y = mat2[6];
        upDir.z = mat2[10];

        Point3F res;
        mCross(upDir, sideDir, &res);
        mat2.setColumn(1, res);
    }
    else
    {
        if (len > 1.0f)
            len = 1.0f;
        if (len < -1.0f)
            len = -1.0f;
        m_point3F_normalize_f(rot, asinf(len));

        dMemcpy(&mat2, &mat1, sizeof(mat2));

        rotateMatrix(mat2, rot);
        mat2.setColumn(2, -zvec);
    }

    QuatF grav(mat2);
    grav.normalize();

    object->setGravityFrame(grav, snap);
}

DefineEngineMethod(Marble, setMode, void, (const char* mode),, "(mode)")
{
    U32 modeFlags[5];
    const char* modesStrings[5];
    S32 newMode = object->getMode() & 3;

    modesStrings[0] = "Normal";
    modeFlags[0] = Marble::StartingMode;

    modesStrings[1] = "Victory";
    modeFlags[1] = Marble::StoppingMode;

    modesStrings[2] = "Lost";
    modeFlags[2] = Marble::StoppingMode;

    modesStrings[3] = "Freeze";
    modeFlags[3] = Marble::TimerMode | Marble::StoppingMode;

    modesStrings[4] = "Start";
    modeFlags[4] = Marble::MoveMode | Marble::RestrictXYZMode;

    S32 i = 0;
    while (dStricmp(modesStrings[i], mode))
    {
        if (++i >= 5)
        {
            Con::errorf("Marble:: Unknown marble mode: %s", mode);
            return;
        }
    }

    object->setMode(newMode | modeFlags[i]);
}

DefineEngineMethod(Marble, setPad, void, (U32 padId),, "(pad)")
{
    if (!padId)
    {
        object->setPad(0);
        return;
    }

    SceneObject* pad;
    if (Sim::findObject(padId, pad))
        object->setPad(pad);
    else
        Con::errorf("Marble::setPad: Not a SceneObject");
}

DefineEngineMethod(Marble, setMarbleTime, void, (U32 time),, "(time)")
{
    object->setMarbleTime(time);
}

DefineEngineMethod(Marble, setMarbleBonusTime, void, (U32 time),, "(time)")
{
    object->setMarbleBonusTime(time);
}

DefineEngineMethod(Marble, setUseFullMarbleTime, void, (bool flag),, "(flag)")
{
    object->setUseFullMarbleTime(flag);
}

DefineEngineMethod(Marble, getMarbleTime, S32, (),, "()")
{
    return object->getMarbleTime();
}

DefineEngineMethod(Marble, getFullMarbleTime, S32, (),, "()")
{
    return object->getFullMarbleTime();
}

DefineEngineMethod(Marble, getMarbleBonusTime, S32, (),, "()")
{
    return object->getMarbleBonusTime();
}

DefineEngineMethod(Marble, getBlastEnergy, F32, (),, "()")
{
    return object->getBlastEnergy();
}

DefineEngineMethod(Marble, setBlastEnergy, void, (F32 energy),, "(energy)")
{
    object->setBlastEnergy(energy);
}

DefineEngineMethod(Marble, getPad, S32, (),, "()")
{
    SceneObject* pad = object->getPad();
    if (pad)
        return pad->getId();

    return 0;
}

DefineEngineMethod(Marble, isFrozen, bool, (),, "()")
{
    return (object->getMode() & Marble::MoveMode) == 0;
}

DefineEngineMethod(Marble, getPowerUpId, S32, (),, "()")
{
    return object->getPowerUpId();
}

DefineEngineMethod(Marble, getLastContactPosition, const char*, (),, "()")
{
    if (object->getLastContact().position.x < 1.0e10)
    {
        char* buf = Con::getReturnBuffer(100);
        dSprintf(buf, 100, "%f %f %f",
                 object->getLastContact().position.x,
                 object->getLastContact().position.y,
                 object->getLastContact().position.z);
        return buf;
    }

    return "";
}

DefineEngineMethod(Marble, clearLastContactPosition, void, (),, "()")
{
    object->getLastContact().position.set(1.0e10, 1.0e10, 1.0e10);
    object->getLastContact().normal.set(0.0, 0.0, 1.0);
    object->getLastContact().actualNormal.set(0.0, 0.0, 1.0);
}

DefineEngineMethod(Marble, startCameraCenter, void, (),, "()")
{
    object->startCenterCamera();
}

DefineEngineMethod(Marble, getGravityDir, const char*, (),, "()")
{
    MatrixF mat(true);
    object->getGravityFrame().setMatrix(&mat);

    Point3F grav(mat[2], mat[6], mat[10]);

    char* retBuf = Con::getReturnBuffer(256);
    dSprintf(retBuf, 256, "%g %g %g", -grav.x, -grav.y, -grav.z);

    return retBuf;
}

DefineEngineMethod(Marble, setOOB, void, (bool oob),, "(oob)")
{
    object->setOOB(oob);
}

DefineEngineMethod(Marble, getPosition, const char*, (),, "()")
{
    static char buffer[100];
    Point3F pos = object->getPosition();
    dSprintf(buffer, 100, "%f %f %f", pos.x, pos.y, pos.z);

    return buffer;
}

DefineEngineMethod(Marble, doPowerUp, void, (F32 powerup),, "(powerup)")
{
    Con::printf("Did powerup! - %g", powerup);
    object->doPowerUp(powerup);
}

DefineEngineMethod(Marble, setPowerUpId, void, (U32 id, bool reset), (false), "(id)")
{
    object->setPowerUpId(id, reset);
}

//----------------------------------------------------------------------------

IMPLEMENT_CO_DATABLOCK_V1(MarbleData);

MarbleData::MarbleData()
{
    minVelocityBounceSoft = 2.5f;
    minVelocityBounceHard = 12.0f;
    bounceMinGain = 0.2f;
    minVelocityMegaBounceSoft = 2.5f;
    maxJumpTicks = 3;
    bounceEmitter = NULL;
    minVelocityMegaBounceHard = 8.0f;
    trailEmitter = NULL;
    mudEmitter = NULL;
    bounceMegaMinGain = 0.5f;
    grassEmitter = NULL;
    maxRollVelocity = 25.0f;
    angularAcceleration = 18.0f;
    brakingAcceleration = 8.0f;
    gravity = 20.0f;
    size = 1.0f;
    megaSize = 2.0f;
    staticFriction = 1.0f;
    kineticFriction = 0.89999998f;
    bounceKineticFriction = 0.2f;
    maxDotSlide = 0.1f;
    bounceRestitution = 0.89999998f;
    airAcceleration = 5.0f;
    energyRechargeRate = 1.0f;
    jumpImpulse = 1.0f;
    maxForceRadius = 1.0f;
    cameraDistance = 2.5f;
    minBounceVel = 0.1f;
    minTrailSpeed = 10.0f;
    minBounceSpeed = 1.0f;
    memset(sound, 0, sizeof(sound));
    //genericShadowLevel = Player_GenericShadowLevel;
    //noShadowLevel = Player_NoShadowLevel;
    blastRechargeTime = 1;
    maxNaturalBlastRecharge = 1;
    powerUps = NULL;
    mDecalData = NULL;
    mDecalID = 0;
    startModeTime = 320;
    stopModeTime = 32;

    // Enable Shadows
    shadowEnable = true;
}

void MarbleData::initPersistFields()
{
    addField("maxRollVelocity", TypeF32, Offset(maxRollVelocity, MarbleData));
    addField("angularAcceleration", TypeF32, Offset(angularAcceleration, MarbleData));
    addField("brakingAcceleration", TypeF32, Offset(brakingAcceleration, MarbleData));
    addField("staticFriction", TypeF32, Offset(staticFriction, MarbleData));
    addField("kineticFriction", TypeF32, Offset(kineticFriction, MarbleData));
    addField("bounceKineticFriction", TypeF32, Offset(bounceKineticFriction, MarbleData));
    addField("gravity", TypeF32, Offset(gravity, MarbleData));
    addField("size", TypeF32, Offset(size, MarbleData));
    addField("megaSize", TypeF32, Offset(megaSize, MarbleData));
    addField("maxDotSlide", TypeF32, Offset(maxDotSlide, MarbleData));
    addField("bounceRestitution", TypeF32, Offset(bounceRestitution, MarbleData));
    addField("airAcceleration", TypeF32, Offset(airAcceleration, MarbleData));
    addField("energyRechargeRate", TypeF32, Offset(energyRechargeRate, MarbleData));
    addField("jumpImpulse", TypeF32, Offset(jumpImpulse, MarbleData));
    addField("maxForceRadius", TypeF32, Offset(maxForceRadius, MarbleData));
    addField("cameraDistance", TypeF32, Offset(cameraDistance, MarbleData));
    addField("minBounceVel", TypeF32, Offset(minBounceVel, MarbleData));
    addField("minTrailSpeed", TypeF32, Offset(minTrailSpeed, MarbleData));
    addField("minBounceSpeed", TypeF32, Offset(minBounceSpeed, MarbleData));
    addField("bounceEmitter", TYPEID<ParticleEmitterData>(), Offset(bounceEmitter, MarbleData));
    addField("trailEmitter", TYPEID<ParticleEmitterData>(), Offset(trailEmitter, MarbleData));
    addField("mudEmitter", TYPEID<ParticleEmitterData>(), Offset(mudEmitter, MarbleData));
    addField("grassEmitter", TYPEID<ParticleEmitterData>(), Offset(grassEmitter, MarbleData));
    addField("powerUps", TYPEID<GameBaseData>(), Offset(powerUps, MarbleData));
    addField("blastRechargeTime", TypeS32, Offset(blastRechargeTime, MarbleData));
    addField("maxNaturalBlastRecharge", TypeS32, Offset(maxNaturalBlastRecharge, MarbleData));
    addField("RollHardSound", TYPEID<SFXProfile>(), Offset(sound[0], MarbleData));
    addField("RollMegaSound", TYPEID<SFXProfile>(), Offset(sound[1], MarbleData));
    addField("RollIceSound", TYPEID<SFXProfile>(), Offset(sound[2], MarbleData));
    addField("SlipSound", TYPEID<SFXProfile>(), Offset(sound[3], MarbleData));
    addField("Bounce1", TYPEID<SFXProfile>(), Offset(sound[4], MarbleData));
    addField("Bounce2", TYPEID<SFXProfile>(), Offset(sound[5], MarbleData));
    addField("Bounce3", TYPEID<SFXProfile>(), Offset(sound[6], MarbleData));
    addField("Bounce4", TYPEID<SFXProfile>(), Offset(sound[7], MarbleData));
    addField("MegaBounce1", TYPEID<SFXProfile>(), Offset(sound[8], MarbleData));
    addField("MegaBounce2", TYPEID<SFXProfile>(), Offset(sound[9], MarbleData));
    addField("MegaBounce3", TYPEID<SFXProfile>(), Offset(sound[10], MarbleData));
    addField("MegaBounce4", TYPEID<SFXProfile>(), Offset(sound[11], MarbleData));
    addField("JumpSound", TYPEID<SFXProfile>(), Offset(sound[12], MarbleData));
    Con::addVariable("Game::endPad", TypeS32, &Marble::smEndPadId);

    Parent::initPersistFields();
}

//----------------------------------------------------------------------------

bool MarbleData::preload(bool server, String& errorStr)
{
    if (!Parent::preload(server, errorStr))
        return false;

    if (!server)
    {
        if (!mDecalData && mDecalID != 0)
            if (!Sim::findObject(mDecalID, mDecalData))
                Con::errorf(ConsoleLogEntry::General, "MarbleData::preload Invalid packet, bad datablockId(decalData): 0x%x", mDecalID);
    }

    return true;
}

void MarbleData::packData(BitStream* stream)
{
    stream->write(maxRollVelocity);
    stream->write(angularAcceleration);
    stream->write(brakingAcceleration);
    stream->write(gravity);
    stream->write(size);
    stream->write(staticFriction);
    stream->write(kineticFriction);
    stream->write(bounceKineticFriction);
    stream->write(maxDotSlide);
    stream->write(bounceRestitution);
    stream->write(airAcceleration);
    stream->write(energyRechargeRate);
    stream->write(jumpImpulse);
    stream->write(maxForceRadius);
    stream->write(cameraDistance);
    stream->write(minBounceVel);
    stream->write(minTrailSpeed);
    stream->write(minBounceSpeed);
    stream->write(blastRechargeTime);
    stream->write(maxNaturalBlastRecharge);

    if (stream->writeFlag(bounceEmitter != NULL))
        stream->writeRangedU32(bounceEmitter->getId(), DataBlockObjectIdFirst, DataBlockObjectIdLast);
    if (stream->writeFlag(trailEmitter != NULL))
        stream->writeRangedU32(trailEmitter->getId(), DataBlockObjectIdFirst, DataBlockObjectIdLast);
    if (stream->writeFlag(mudEmitter != NULL))
        stream->writeRangedU32(mudEmitter->getId(), DataBlockObjectIdFirst, DataBlockObjectIdLast);
    if (stream->writeFlag(grassEmitter != NULL))
        stream->writeRangedU32(grassEmitter->getId(), DataBlockObjectIdFirst, DataBlockObjectIdLast);
    if (stream->writeFlag(powerUps != NULL))
        stream->writeRangedU32(powerUps->getId(), DataBlockObjectIdFirst, DataBlockObjectIdLast);


    S32 i;
    for (i = 0; i < MaxSounds; i++)
        if (stream->writeFlag(sound[i]))
            stream->writeRangedU32(sound[i]->getId(), DataBlockObjectIdFirst, DataBlockObjectIdLast);

    if (stream->writeFlag(mDecalData != NULL))
        stream->writeRangedU32(mDecalData->getId(), DataBlockObjectIdFirst, DataBlockObjectIdLast);

    Parent::packData(stream);
}

void MarbleData::unpackData(BitStream* stream)
{
    stream->read(&maxRollVelocity);
    stream->read(&angularAcceleration);
    stream->read(&brakingAcceleration);
    stream->read(&gravity);
    stream->read(&size);
    stream->read(&staticFriction);
    stream->read(&kineticFriction);
    stream->read(&bounceKineticFriction);
    stream->read(&maxDotSlide);
    stream->read(&bounceRestitution);
    stream->read(&airAcceleration);
    stream->read(&energyRechargeRate);
    stream->read(&jumpImpulse);
    stream->read(&maxForceRadius);
    stream->read(&cameraDistance);
    stream->read(&minBounceVel);
    stream->read(&minTrailSpeed);
    stream->read(&minBounceSpeed);
    stream->read(&blastRechargeTime);
    stream->read(&maxNaturalBlastRecharge);

    if (stream->readFlag())
        Sim::findObject(stream->readRangedU32(DataBlockObjectIdFirst, DataBlockObjectIdLast), bounceEmitter);
    if (stream->readFlag())
        Sim::findObject(stream->readRangedU32(DataBlockObjectIdFirst, DataBlockObjectIdLast), trailEmitter);
    if (stream->readFlag())
        Sim::findObject(stream->readRangedU32(DataBlockObjectIdFirst, DataBlockObjectIdLast), mudEmitter);
    if (stream->readFlag())
        Sim::findObject(stream->readRangedU32(DataBlockObjectIdFirst, DataBlockObjectIdLast), grassEmitter);
    if (stream->readFlag())
        Sim::findObject(stream->readRangedU32(DataBlockObjectIdFirst, DataBlockObjectIdLast), powerUps);

    S32 i;
    for (i = 0; i < MaxSounds; i++) {
        sound[i] = NULL;
        if (stream->readFlag())
        {
            U32 id = stream->readRangedU32(DataBlockObjectIdFirst, DataBlockObjectIdLast);
            sound[i] = dynamic_cast<SFXProfile*>(Sim::findObject(id));
        }
    }

    if (stream->readFlag())
        mDecalID = stream->readRangedU32(DataBlockObjectIdFirst, DataBlockObjectIdLast);

    Parent::unpackData(stream);
}

//----------------------------------------------------------------------------
