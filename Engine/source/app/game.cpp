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

//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~~//
// Arcane-FX for MIT Licensed Open Source version of Torque 3D from GarageGames
// Copyright (C) 2015 Faust Logic, Inc.
//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~~//

#include "platform/platform.h"
#include "platform/platformInput.h"

#include "app/game.h"
#include "math/mMath.h"
#include "core/dnet.h"
#include "core/stream/fileStream.h"
#include "core/frameAllocator.h"
#include "core/iTickable.h"
#include "core/strings/findMatch.h"
#include "console/simBase.h"
#include "console/console.h"
#include "console/consoleTypes.h"
#include "console/engineAPI.h"
#include "gui/controls/guiMLTextCtrl.h"
#ifdef TORQUE_TGB_ONLY
#include "T2D/oldModel/networking/t2dGameConnection.h"
#include "T2D/oldModel/networking/t2dNetworkServerSceneProcess.h"
#include "T2D/oldModel/networking/t2dNetworkClientSceneProcess.h"
#else
#include "T3D/gameBase/gameConnection.h"
#include "T3D/gameFunctions.h"
#include "T3D/gameBase/gameProcess.h"
#endif
#include "platform/profiler.h"
#include "gfx/gfxCubemap.h"
#include "gfx/gfxTextureManager.h"
#include "sfx/sfxSystem.h"

#ifdef TORQUE_AFX_ENABLED
#include "afx/arcaneFX.h"
#endif

#if defined(TORQUE_PLAYER) || !defined(TORQUE_TOOLS)
// See matching #ifdef in editor/editor.cpp
bool gEditingMission = false;
#endif

//--------------------------------------------------------------------------

ConsoleFunctionGroupBegin( InputManagement, "Functions that let you deal with input from scripts" );

DefineEngineFunction( deactivateDirectInput, void, (), ,
         "()"
            "@brief Disables DirectInput.\n\n"
            "Also deactivates any connected joysticks.\n\n"
			"@ingroup Input" )
{
   if ( Input::isActive() )
      Input::deactivate();
}

DefineEngineFunction( activateDirectInput, void, (), ,
            "()"
            "@brief Activates DirectInput.\n\n"
            "Also activates any connected joysticks."
			"@ingroup Input")
{
   if ( !Input::isActive() )
      Input::activate();
}
ConsoleFunctionGroupEnd( InputManagement );

//--------------------------------------------------------------------------

static const U32 MaxPlayerNameLength = 16;
DefineEngineFunction( strToPlayerName, const char*, (const char* ptr ), , "strToPlayerName(string);" )
{

	// Strip leading spaces and underscores:
   while ( *ptr == ' ' || *ptr == '_' )
      ptr++;

   U32 len = dStrlen( ptr );
   if ( len )
   {
      char* ret = Con::getReturnBuffer( MaxPlayerNameLength + 1 );
      char* rptr = ret;
      ret[MaxPlayerNameLength - 1] = '\0';
      ret[MaxPlayerNameLength] = '\0';
      bool space = false;

      U8 ch;
      while ( *ptr && dStrlen( ret ) < MaxPlayerNameLength )
      {
         ch = (U8) *ptr;

         // Strip all illegal characters:
         if ( ch < 32 || ch == ',' || ch == '.' || ch == '\'' || ch == '`' )
         {
            ptr++;
            continue;
         }

         // Don't allow double spaces or space-underline combinations:
         if ( ch == ' ' || ch == '_' )
         {
            if ( space )
            {
               ptr++;
               continue;
            }
            else
               space = true;
         }
         else
            space = false;

         *rptr++ = *ptr;
         ptr++;
      }
      *rptr = '\0';

		//finally, strip out the ML text control chars...
		return GuiMLTextCtrl::stripControlChars(ret);
   }

	return( "" );
}

ConsoleFunctionGroupBegin( Platform , "General platform functions.");

DefineEngineFunction( lockMouse, void, (bool isLocked ), , "(bool isLocked)"
            "@brief Lock or unlock the mouse to the window.\n\n"
            "When true, prevents the mouse from leaving the bounds of the game window.\n\n"
            "@ingroup Input")
{
   Platform::setWindowLocked(isLocked);
}


DefineEngineFunction( setNetPort, bool, (int port, bool bind), (true), "(int port, bool bind=true)" 
   "@brief Set the network port for the game to use.\n\n"

   "@param port The port to use.\n"
   "@param bind True if bind() should be called on the port.\n"

   "@returns True if the port was successfully opened.\n"

   "This will trigger a windows firewall prompt.  "
   "If you don't have firewall tunneling tech you can set this to false to avoid the prompt.\n\n"
   "@ingroup Networking")
{
   return Net::openPort((S32)port, bind);
}

DefineEngineFunction(isAddressTypeAvailable, bool, (int addressType), , "(protocol id)"
	"@brief Determines if a specified address type can be reached.\n\n"
	"@ingroup Networking")
{
	return Net::isAddressTypeAvailable((NetAddress::Type)addressType);
}

DefineEngineFunction( closeNetPort, void, (), , "()" 
   "@brief Closes the current network port\n\n"
   "@ingroup Networking")
{
   Net::closePort();
}

DefineEngineFunction( saveJournal, void, (const char * filename), , "(string filename)" 
                "Save the journal to the specified file.\n\n"
				"@ingroup Platform")
{
   Journal::Record(filename);
}

DefineEngineFunction( playJournal, void, (const char * filename), , "(string filename)" 
                "@brief Begin playback of a journal from a specified field.\n\n"
				"@param filename Name and path of file journal file\n"
				"@ingroup Platform")
{
   // CodeReview - BJG 4/24/2007 - The break flag needs to be wired back in.
   // bool jBreak = (argc > 2)? dAtob(argv[2]): false;
   Journal::Play(filename);
}

DefineEngineFunction( getSimTime, S32, (), , "()" 
				"Return the current sim time in milliseconds.\n\n"
                "@brief Sim time is time since the game started.\n\n"
				"@ingroup Platform")
{
   return Sim::getCurrentTime();
}

DefineEngineFunction( getRealTime, S32, (), , "()" 
				"@brief Return the current real time in milliseconds.\n\n"
                "Real time is platform defined; typically time since the computer booted.\n\n"
				"@ingroup Platform")
{
   return Platform::getRealMilliseconds();
}

DefineEngineFunction(getLocalTime, const char*, (),,
   "@brief Return the current local time as: weekday month day year hour min sec.\n\n"
   "Local time is platform defined."
   "@ingroup Platform")
{
   Platform::LocalTime lt;
   Platform::getLocalTime(lt);

   static const U32 bufSize = 128;
   char *retBuffer = Con::getReturnBuffer(bufSize);
   dSprintf(retBuffer, bufSize, "%d %d %d %d %02d %02d %02d",
      lt.weekday,
      lt.month + 1,
      lt.monthday,
      lt.year + 1900,
      lt.hour,
      lt.min,
      lt.sec);

   return retBuffer;
}

ConsoleFunctionGroupEnd(Platform);

//-----------------------------------------------------------------------------

bool clientProcess(U32 timeDelta)
{
#ifdef TORQUE_AFX_ENABLED
   // Required heartbeat call on the client side which must come
   // before the advanceTime() calls are made to the scene objects.
   arcaneFX::advanceTime(timeDelta);
#endif
   bool ret = true;

#ifndef TORQUE_TGB_ONLY
   ret = ClientProcessList::get()->advanceTime(timeDelta);
#else
	ret = gt2dNetworkClientProcess.advanceTime( timeDelta );
#endif

   ITickable::advanceTime(timeDelta);

#ifndef TORQUE_TGB_ONLY
   // Determine if we're lagging
   GameConnection* connection = GameConnection::getConnectionToServer();
   if(connection)
	{
      connection->detectLag();
	}
#else
   // Determine if we're lagging
   t2dGameConnection* connection = t2dGameConnection::getConnectionToServer();
   if(connection)
	{
      connection->detectLag();
	}
#endif

   // Let SFX process.
   SFX->_update();

   return ret;
}

bool serverProcess(U32 timeDelta)
{
   bool ret = true;
#ifndef TORQUE_TGB_ONLY
   ret =  ServerProcessList::get()->advanceTime(timeDelta);
#else
   ret =  gt2dNetworkServerProcess.advanceTime( timeDelta );
#endif
   return ret;
}

