//-----------------------------------------------------------------------------
// Torque Game Engine
// Copyright (C) GarageGames.com, Inc.
//-----------------------------------------------------------------------------

#ifndef _PATHSHAPE_H_
#define _PATHSHAPE_H_

#ifndef _STATICSHAPE_H_
#include "T3D/staticShape.h"
#endif

#ifndef _CAMERASPLINE_H_
#include "T3D/cameraSpline.h"
#endif

#ifndef _SIMPATH_H_
#include "scene/simPath.h"
#endif
//----------------------------------------------------------------------------
struct PathShapeData: public StaticShapeData {
   typedef StaticShapeData Parent;


   PathShapeData();
   static void consoleInit();

   DECLARE_CONOBJECT(PathShapeData);
   bool preload(bool server, String &errorStr);
   static void initPersistFields();
   virtual void packData(BitStream* stream);
   virtual void unpackData(BitStream* stream);
};


//----------------------------------------------------------------------------
class PathShape: public StaticShape
{
public:
   enum State {
      Forward,
      Backward,
      Stop,
      StateBits = 3
   };

private:
   typedef StaticShape Parent;

     enum MaskBits {
      WindowMask     = Parent::NextFreeMask,
      PositionMask   = WindowMask << 1,
      TargetMask     = PositionMask << 1,
      StateMask      = TargetMask << 1,
      NextFreeMask   = StateMask << 1

	  };

   struct StateDelta {
      F32 time;
      F32 timeVec;
   };
   StateDelta delta;

   enum Constants {
      NodeWindow = 20    // Maximum number of active nodes
   };

   PathShapeData* mDataBlock;
   CameraSpline mSpline;
   S32 mNodeBase;
   S32 mNodeCount;
   F32 mPosition;
   S32 mState;
   F32 mTarget;
   bool mTargetSet;
   void interpolateMat(F32 pos,MatrixF* mat);
   void advancePosition(S32 ms);

public:
   DECLARE_CONOBJECT(PathShape);

   PathShape();
   ~PathShape();

   StringTableEntry mControl[4];

   static void initPersistFields();
   static void consoleInit();
   bool onAdd();
   void onRemove();
   bool onNewDataBlock(GameBaseData* dptr, bool reload);
   void onNode(S32 node);

   void processTick(const Move*);
   void interpolateTick(F32 dt);

   U32  packUpdate(NetConnection *, U32 mask, BitStream *stream);
   void unpackUpdate(NetConnection *, BitStream *stream);

   void reset(F32 speed = 1);
   void pushFront(CameraSpline::Knot *knot);
   void pushBack(CameraSpline::Knot *knot);
   void popFront();

   void setPosition(F32 pos);
   void setTarget(F32 pos);
   void setState(State s);
   S32 getState();
   SimObjectRef< SimPath::Path >  mSimPath;
};


#endif
