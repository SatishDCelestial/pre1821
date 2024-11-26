#pragma once

//////////////////////////////////////////////////////////////////////////
// Wrapper to log all exceptions and address and optional stack to Startup.log
// To enable, set reg key HKCU/.../Visual Assist X/EnableVASeExceptions=Yes
// keep old name to match ID_RK_APP_KEY
// Note: you can get debug symbols by setting StackWalkerFlag=0x3f
//
// To log the stack use
//   try{}
//   catch(...){ VALogExceptionCallStack(); }
//////////////////////////////////////////////////////////////////////////

void VASetSeTranslator(); // Needs to be called for each thread.
void VALogExceptionCallStack();
void VaSetCrtErrorHandlers();
