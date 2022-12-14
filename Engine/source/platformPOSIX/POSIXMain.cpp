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

#ifndef __APPLE__

#include "platform/platform.h"
#include "platform/platformInput.h"
#include "console/console.h"

#include "platformPOSIX/platformPOSIX.h"
#include "platformPOSIX/POSIXStdConsole.h"
#include "platformPOSIX/POSIXState.h"

extern void InitWindowingSystem();

//------------------------------------------------------------------------------
void Platform::init()
{
   StdConsole::create();
   stdConsole->enable(true);

   // init process control stuff
   ProcessControlInit();

   Con::printf("Initializing platform...");

   // Set the platform variable for the scripts
   Con::setVariable( "$platform", "x86UNIX" );
#if defined(__linux__)
   Con::setVariable( "$platformUnixType", "Linux" );
#elif defined(__OpenBSD__)
   Con::setVariable( "$platformUnixType", "OpenBSD" );
#else
   Con::setVariable( "$platformUnixType", "Unknown" );
#endif

   Input::init();

   //installRedBookDevices();

#ifndef TORQUE_DEDICATED
   // if we're not dedicated do more initialization
   InitWindowingSystem();
#endif
}

//------------------------------------------------------------------------------
void Platform::shutdown()
{
   Cleanup();
}

//------------------------------------------------------------------------------


extern "C"
{
   bool torque_engineinit(int argc, const char **argv);
   int  torque_enginetick();
   S32  torque_getreturnstatus();
   bool torque_engineshutdown();

   int torque_unixmain(int argc, const char **argv)
   {
     if (!torque_engineinit(argc, argv))
         return 1;

     while(torque_enginetick())
     {

     }

     torque_engineshutdown();

     return torque_getreturnstatus();

   }
}

extern S32 TorqueMain(S32 argc, const char **argv);

#if !defined(TORQUE_SHARED)
int main(int argc, const char **argv)
{
   return TorqueMain(argc, argv);
}
#endif
#endif