//---------------------------------------------------------------------------
// Copyright (c) 2004 Borland Software Corporation.  All Rights Reserved.
//---------------------------------------------------------------------------

#pragma once

#if defined(SANCTUARY)

/*
  These methods are given as examples of how to internationalize
  Sanctuary error messages.

  Important: String is not "wide character safe."  Replace GetErrorMessage's
  references to the String class if wide characters are needed, such as UTF-8.

  e.g.

  void GetErrorMessage( ACHAR* message, int max, const sanct_error* se );
  void GetErrorMessage( ACHAR* message, int max, const SlipException& ex );
*/

#define ACHAR char

struct sanct_error;

SANCT_BEGIN_NAMESPACE
class String;
class SlipException;
SANCT_END_NAMESPACE

namespace SanctuaryExt 
{

void GetErrorMessage( SANCT_NAMESPACE::String* message, const sanct_error* se );
void GetErrorMessage( SANCT_NAMESPACE::String* message, const SANCT_NAMESPACE::SlipException& ex );

void GetErrorMessage( SANCT_NAMESPACE::String* message, const sanct_error* se,
					  const SANCT_NAMESPACE::String* user,
					  const SANCT_NAMESPACE::String* machine,
					  const SANCT_NAMESPACE::String* serverAddress);

void GetErrorMessage(
					 SANCT_NAMESPACE::String* message, 
					 int context /*e.g. ERROR_CONTEXT_EXCEPTION*/, 
					 int code
					 );
}

#endif
