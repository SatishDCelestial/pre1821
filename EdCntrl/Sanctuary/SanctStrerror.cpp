//---------------------------------------------------------------------------
// Copyright (c) 2004 Borland Software Corporation.  All Rights Reserved.
//---------------------------------------------------------------------------

#include "stdafx.h"

#if defined(SANCTUARY)

#define _VIS_STD
#define _VIS_COMPAT

#include "Sanctuary/include/sanctuary.h"
#include "Sanctuary/include/String.h"
#include "SanctStrerror.h"

SANCT_USING_NAMESPACE

struct string_item {
  int code;
  //ACHAR text[ 256 ];
  const ACHAR* text;
};

extern const string_item ls_strings[];
extern const string_item ex_strings[];
extern const string_item flexlm_strings[];
// Improve: Create hashmaps for each of the string_item arrays

namespace SanctuaryExt {

const ACHAR* GetErrorMessage( const string_item strings[], int code ) {
  const ACHAR* msg = NULL;
  for ( int i = 0; ; ++i ) {
    const ACHAR* text = strings[ i ].text;
    if ( text[ 0 ] == '\0' ) {
      break;
    }
    if ( strings[ i ].code == code ) {
      msg = text;
      break;
    }
  }
  return msg;
}

const ACHAR* GetErrorMessage( const sanct_error* se ) {
  if ( se == NULL ) {
    return NULL;
  }
  const ACHAR* msg = NULL;
  switch ( se->context ) {
    case ERROR_CONTEXT_EXCEPTION:
      msg = GetErrorMessage( ex_strings, se->code );
      break;
    case ERROR_CONTEXT_LSERVER:
      msg = GetErrorMessage( ls_strings, se->code );
      break;
    case ERROR_CONTEXT_FLEXLM: // includes ERROR_CONTEXT_LSERVER
      msg = GetErrorMessage( flexlm_strings, se->major );
      if ( msg == NULL ) {
        msg = GetErrorMessage( ls_strings, se->code );
      }
      break;
  }
  return msg;
}

void GetErrorMessage( String* message, const sanct_error* se, const char* v ) {
  if ( message == NULL ) {
    throw RuntimeException( "Null pointer" );
  }
  message->setLength( 0 );
  const ACHAR* em = GetErrorMessage( se );
  if ( em != NULL ) {
    message->append( em );
  }
  if ( v != NULL ) {
    String tmpl( *message );
    String value( v );
    message->textFormat( tmpl, value );
  }
  if ( message->length() > 0 ) {
    message->append( " " );
  }
  message->append( "(" );
  message->append( se->context );
  message->append( ":" );
  message->append( se->code );
  message->append( "," );
  message->append( se->major );
  message->append( "," );
  message->append( se->minor );
  message->append( "," );
  message->append( se->system );
  message->append( ")" );
}

void GetErrorMessage( String* message, const sanct_error* se ) {
  GetErrorMessage( message, se, NULL );
}

void GetErrorMessage( String* message, const SlipException& ex ) {
  const char* value = ex.getFormatValue(); // Always ASCII
  GetErrorMessage( message, ex.getError(), value );
}

void GetErrorMessage( String* message, int context, int code) {
	if ( message == NULL ) {
		throw RuntimeException( "Null pointer" );
	}
	sanct_error se = {context, code, 0, 0, 0};
	//se->code = code;
	//se->context = context;

	const sanct_error* cse = &se;

	message->setLength( 0 );
	const ACHAR* msg = GetErrorMessage(cse);
	message->append(msg);
}

// utility method
void GetErrorMessage( String* message, const sanct_error* se,
					  const String* user,
					  const String* machine,
					  const String* serverAddress) {

	// default message
	GetErrorMessage(message, se, NULL);

	// append details
	if( user != NULL && machine != NULL && serverAddress != NULL )
	{
		bool ampAdd = false;
		message->append(" ("); 
		
		// append user
		if ( user != NULL )    { ampAdd = true; message->append(user); }
		
		// machine
		if ( machine != NULL ) { 
			if(ampAdd) { message->append("@"); }
			ampAdd = true; 
			message->append(machine); 
		}

		// server with port
		if ( serverAddress != NULL) {
			if(ampAdd) { message->append(", "); }
			ampAdd = true;
			message->append(serverAddress);
		}

		message->append(")");
	}
}

} // TestLib

const string_item ls_strings[] = {
  { LS_UNKNOWN, "Unknown Error" },
  { LS_OK, "No Error" },
  { LS_ERROR_INVALID_LICENSE_FILE, "Invalid license file" },
  { LS_ERROR_NO_LICENSE_ON_SERVER, "License server does not have license for this product" },
  { LS_ERROR_LIC_UNAVAILABLE, "Maximum number of users already reached" },
  { LS_ERROR_NO_SERVER_CONNECTION, "Cannot connect to license server" },
  { LS_ERROR_EXPIRED_LICENSE, "License has expired" },
  { LS_ERROR_INVALID_REQUEST, "Invalid request to license server" },
  { LS_ERROR_INVALID_RESPONSE, "Invalid response from license server" },
  { LS_ERROR_ADDRESS_OUT_OF_RANGE, "License server does not accept requests from this IP address" },
  { LS_ERROR_USER_UNKNOWN, "User name does not have permission" },
  { LS_ERROR_INTERNAL, "Internal Error.  Connection to License Server failed." },
  { LS_ERROR_UNKNOWN_HOST, "Could not resolve license server's host name" },
  { LS_ERROR_UNKNOWN, "Unknown Error occurred while connecting to License Server." },
  { LS_ERROR_UNSUPPORTED_REQUEST, "License server doesn't support request" },
  { LS_ERROR_UNSUPPORTED_PROTOCOL, "License server doesn't support protocol" },
  { LS_ERROR_BAD_BORROW_TYPE, "Invalid request to license server" },
  { LS_ERROR_BAD_IMPRINT, "Invalid request to license server" },
  { LS_ERROR_UNKNOWN_BACKUP_HOST, "Could not resolve backup license server's host name" },
  { LS_ERROR_NO_BACKUP_CONNECTION, "Cannot connect to either main or backup license server" },
  { LS_ERROR_CONNECTION_CLOSED, "License server closed the connection" },
  { LS_ERROR_BORROW_FROM_BACKUP, "Backup server does not allow offline usage of licenses" },
  { 0, "" } };

const string_item ex_strings[] = {
  { EX_MISSING_LICENSE_FILE, "Activation file is invalid.\nPlease obtain a valid activation file." },
  { EX_CORRUPTED_LICENSE, "Activation information is invalid.\nPlease register again." },
  { EX_CANTVERIFY_LICENSE, "Activation information is either corrupted or does not match this product.\nPlease register again." },
  { EX_CORRUPTED_LICENSE_FILE, "License storage {0} is corrupted, licensing data cannot be recovered.\nPlease register your Embarcadero product(s) again." },
  { EX_CORRUPTED_LIC_KEY_FILE, "Activation file {0} is invalid or corrupted.\nPlease obtain a valid activation file." },
  { EX_FOREIGN_FILE, "License storage {0} does not apply to current user.\nPlease register your Embarcadero product(s)." },
  { EX_CANT_SAVE, "License storage {0} is marked read-only. Licensing information can not be saved.\nPlease change file access rights." },
  { EX_CANT_WRITE, "Licensing storage {0} cannot be written." },
  { EX_NONIMPORTABLE, "Activation file {0} cannot be loaded.\nPlease obtain a valid activation file." },
  { EX_NO_SILENT_IMPORT, "Activation file {0} could not be loaded.\nPlease obtain a valid activation file." },
  { EX_NONIMPORTABLE_TEXT, "Activation file {0} cannot be manually loaded.\nPlease obtain a valid activation file." },
  { EX_NO_SILENT_IMPORT_TEXT, "Activation file {0} could not be loaded.\nPlease obtain a valid activation file." },
  { EX_WRONG_LICENSE, "Activation information does not match this product and has been ignored." },
  { EX_WRONG_PLATFORM, "Activation information is for a different platform or Operating System.\nPlease register again with an appropriate serial number." },
  { EX_WRONG_SESSION, "Activation information does not apply to current user/host machine or is no longer valid for this product.\nPlease register again." },
  { EX_CANT_STORE_BORROWER, "Licensing data cannot be stored to {0}." },
  { EX_WRONG_OEM_SLIP_1, "Activation information does not match this product." },
  { EX_WRONG_OEM_SLIP_2, "Activation information does not match this product." },
  { EX_INVALID_CUSTOM_NODELOCK, "An invalid custom node lock has been provided. \nCustom node lock must not be an empty string." },
  { EX_CANT_READ, "Licensing storage {0} cannot be read." },
  { EX_CANT_LOCK, "License storage {0} cannot be locked.\nPlease try again." },
  { EX_FOREIGN_NODE, "Licenses at {0} are not valid for use by products running on this host." },
  { EX_SLIP_IMPORT_FAILED, "Activation file {0} cannot be loaded." },
  { EX_NOT_NETWORKED_1, "Activation file {0} is not configured for networked licensing." },
  { EX_NOT_NETWORKED_2, "Activation file {0} is not configured for networked licensing." },
  { EX_NO_LICENSE, "Activation file {0} is not configured for this product and/or product version." },
  { EX_MISSING_SLIP, "The activation file for identifier {0} is missing." },
  { EX_CANT_WRITE_SLIP, "Activation file cannot be stored to {0}." },
  { EX_NO_SLIP_FOR_FOUND_LICENSE, "Missing activation file. Please register again." },
  { EX_INFODIR_NOT_WRITABLE, "Directory of license storage {0} is not writable." },
  { EX_WRONG_ADDON_SESSION, "Add-on license ({0}) could not be locked to the the current license file."},
  { EX_ADDON_DUPLICATE_SN, "An incremental license with an identical serial number has already been loaded." },
  { EX_ADDON_LICENSE_EXP, "The incremental license that is being imported has expired.  Import failed." },
  { EX_INVALID_VENDOR_KEY, "Imported license is not a {0} product license.  Missing random key attribute" },
  { EX_SPOOFED_TIME_API, "Bad time system API." },

  { 
	  EX_SILENT_ACTIVATION_FAILURE, 
"Product activation failed!\n\
Possible causes:\n\
\t - no internet connection\n\
\t - connection to the activation server blocked by firewall\n\
\t - invalid license file\n\
\n\
In order to activate the product, make sure to:\n\
\t - have a valid internet connection\n\
\t - have a valid license file\n\
\n" 
  },

  { 0, "" } };

const string_item flexlm_strings[] = {
  { FLEXLM_NOERROR, "There was no error." },
  { FLEXLM_NOCONFFILE, "Can't find license file" },
  { FLEXLM_BADFILE, "License file corrupted" },
  { FLEXLM_NOSERVER, "Cannot connect to a license server" },
  { FLEXLM_MAXUSERS, "Maximum number of users reached" },
  { FLEXLM_NOFEATURE, "No such feature exists" },
  { FLEXLM_NOSERVICE, "No TCP/IP service \"FLEXlm\"" },
  { FLEXLM_NOSOCKET, "No socket to talk to server on" },
  { FLEXLM_BADCODE, "Bad encryption code" },
  { FLEXLM_NOTTHISHOST, "Hostid doesn't match license" },
  { FLEXLM_LONGGONE, "Software Expired" },
  { FLEXLM_BADDATE, "Bad date in license file" },
  { FLEXLM_BADCOMM, "Bad return from server" },
  { FLEXLM_NO_SERVER_IN_FILE, "No servers specified in license file" },
  { FLEXLM_BADHOST, "Bad SERVER hostname in license file" },
  { FLEXLM_CANTCONNECT, "Cannot connect to server" },
  { FLEXLM_CANTREAD, "Cannot read from server" },
  { FLEXLM_CANTWRITE, "Cannot write to server" },
  { FLEXLM_NOSERVSUPP, "Server does not support this feature" },
  { FLEXLM_SELECTERR, "Error in select system call" },
  { FLEXLM_SERVBUSY, "Application server \"busy\" (connecting)" },
  { FLEXLM_OLDVER, "Config file doesn't support this version" },
  { FLEXLM_CHECKINBAD, "Feature checkin failed at daemon end" },
  { FLEXLM_BUSYNEWSERV, "Server busy/new server connecting" },
  { FLEXLM_USERSQUEUED, "Users already in queue for this feature" },
  { FLEXLM_SERVLONGGONE, "Version not supported at server end" },
  { FLEXLM_TOOMANY, "Request for more licenses than supported" },
  { FLEXLM_CANTREADKMEM, "Cannot read /dev/kmem" },
  { FLEXLM_CANTREADVMUNIX, "Cannot read /vmunix" },
  { FLEXLM_CANTFINDETHER, "Cannot find ethernet device" },
  { FLEXLM_NOREADLIC, "Cannot read license file" },
  { FLEXLM_TOOEARLY, "Start date for feature not reached" },
  { FLEXLM_NOSUCHATTR, "No such attr for lm_set_attr/ls_get_attr" },
  { FLEXLM_BADHANDSHAKE, "Bad encryption handshake with server" },
  { FLEXLM_CLOCKBAD, "Clock difference too large between client/server" },
  { FLEXLM_FEATQUEUE, "We are in the queue for this feature" },
  { FLEXLM_FEATCORRUPT, "Feature database corrupted in daemon" },
  { FLEXLM_BADFEATPARAM, "dup_select mismatch for this feature" },
  { FLEXLM_FEATEXCLUDE, "User/host on EXCLUDE list for feature" },
  { FLEXLM_FEATNOTINCLUDE, "User/host not in INCLUDE list for feature" },
  { FLEXLM_CANTMALLOC, "Cannot allocate dynamic memory" },
  { FLEXLM_NEVERCHECKOUT, "Feature never checked out (lm_status())" },
  { FLEXLM_BADPARAM, "Invalid parameter" },
  { FLEXLM_NOKEYDATA, "No FLEXlm key data" },
  { FLEXLM_BADKEYDATA, "Invalid FLEXlm key data" },
  { FLEXLM_FUNCNOTAVAIL, "FLEXlm function not available" },
  { FLEXLM_DEMOKIT, "FLEXlm software is demonstration version" },
  { FLEXLM_NOCLOCKCHECK, "Clock check not available in daemon" },
  { FLEXLM_BADPLATFORM, "FLEXlm platform not enabled" },
  { FLEXLM_DATE_TOOBIG, "Date too late for binary format" },
  { FLEXLM_EXPIREDKEYS, "FLEXlm key data has expired" },
  { FLEXLM_NOFLEXLMINIT, "FLEXlm not initialized" },
  { FLEXLM_NOSERVRESP, "Server did not respond to message" },
  { FLEXLM_CHECKOUTFILTERED, "Request rejected by vendor-defined filter" },
  { FLEXLM_NOFEATSET, "No FEATURESET line present in license file" },
  { FLEXLM_BADFEATSET, "Incorrect FEATURESET line in license file" },
  { FLEXLM_CANTCOMPUTEFEATSET, "Cannot compute FEATURESET line" },
  { FLEXLM_SOCKETFAIL, "socket() call failed" },
  { FLEXLM_SETSOCKFAIL, "setsockopt() failed" },
  { FLEXLM_BADCHECKSUM, "message checksum failure" },
  { FLEXLM_SERVBADCHECKSUM, "server message checksum failure" },
  { FLEXLM_SERVNOREADLIC, "Cannot read license file from server" },
  { FLEXLM_NONETWORK, "Network software (tcp/ip) not available" },
  { FLEXLM_NOTLICADMIN, "Not a license administrator" },
  { FLEXLM_REMOVETOOSOON, "lmremove request too soon" },
  { FLEXLM_BADVENDORDATA, "Bad VENDORCODE struct passed to lm_init()" },
  { FLEXLM_LIBRARYMISMATCH, "FLEXlm include file/library mismatch" },
  { FLEXLM_NONETOBORROW, "No licenses to borrow" },
  { FLEXLM_NOBORROWSUPP, "License BORROW support not enabled" },
  { FLEXLM_NOTONSERVER, "FLOAT_OK can't run standalone on SERVER" },
  { FLEXLM_BORROWLOCKED, "Meter already being updated for another counter" },
  { FLEXLM_BAD_TZ, "Invalid TZ environment variable" },
  { FLEXLM_OLDVENDORDATA, "\"Old-style\" vendor keys (3-word)" },
  { FLEXLM_LOCALFILTER, "Local checkout filter requested request" },
  { FLEXLM_ENDPATH, "Attempt to read beyond the end of LF path" },
  { FLEXLM_VMS_SETIMR_FAILED, "VMS SYS$SETIMR call failed" },
  { FLEXLM_INTERNAL_ERROR, "Internal FLEXlm error -- Please report" },
  { FLEXLM_BAD_VERSION, "Version number must be string of dec float" },
  { FLEXLM_NOADMINAPI, "FLEXadmin API functions not available" },
  { FLEXLM_NOFILEOPS, "FLEXlm internal error -79" },
  { FLEXLM_NODATAFILE, "FLEXlm internal error -80" },
  { FLEXLM_NOFILEVSEND, "FLEXlm internal error -81" },
  { FLEXLM_BADPKG, "Invalid PACKAGE line in license file" },
  { FLEXLM_SERVOLDVER, "Server FLEXlm version older than client's" },
  { FLEXLM_USER_BASED, "Incorrect number of USERS/HOSTS INCLUDED in options file -- see server log" },
  { FLEXLM_NOSERVCAP, "Server doesn't support this request" },
  { FLEXLM_OBJECTUSED, "This license object already in use (Java only)" },
  { FLEXLM_MAXLIMIT, "Checkout exceeds MAX specified in options file" },
  { FLEXLM_BADSYSDATE, "System clock has been set back" },
  { FLEXLM_PLATNOTLIC, "This platform not authorized by license" },
  { FLEXLM_FUTURE_FILE, "\"Future license file format or misspelling in license file\"" },
  { FLEXLM_DEFAULT_SEEDS, "ENCRYPTION_SEEDs are non-unique" },
  { FLEXLM_SERVER_REMOVED, "Server removed during reread, or server hostid mismatch with license" },
  { FLEXLM_POOL, "This feature is available in a different license pool" },
  { FLEXLM_LGEN_VER, "Attempt to generate license with incompatible attributes" },
  { FLEXLM_NOT_THIS_HOST, "Network connect to THIS_HOST failed" },
  { FLEXLM_HOSTDOWN, "Server node is down or not responding" },
  { FLEXLM_VENDOR_DOWN, "The desired vendor daemon is down" },
  { FLEXLM_CANT_DECIMAL, "The FEATURE line can't be converted to decimal format" },
  { FLEXLM_BADDECFILE, "The decimal format license is typed incorrectly" },
  { FLEXLM_REMOVE_LINGER, "Cannot remove a lingering license" },
  { FLEXLM_RESVFOROTHERS, "All licenses are reserved for others" },
  { FLEXLM_BORROW_ERROR, "A FLEXid borrow error occurred" },
  { FLEXLM_TSOK_ERR, "Terminal Server remote client not allowed" },
  { FLEXLM_BORROW_TOOLONG, "Cannot borrow that long" },
  { FLEXLM_UNBORROWED_ALREADY, "Feature already returned to server" },
  { FLEXLM_SERVER_MAXED_OUT, "License server out of network connections" },
  { FLEXLM_NOBORROWCOMP, "Can't borrow a PACKAGE component" },
  { FLEXLM_BORROW_METEREMPTY, "Licenses all borrowed or meter empty" },
  { FLEXLM_NOBORROWMETER, "No Borrow Meter Found" },
  { FLEXLM_NODONGLE, "Dongle not attached, or can't read dongle" },
  { FLEXLM_NORESLINK, "lmgr.res, Windows Resource file, not linked" },
  { FLEXLM_NODONGLEDRIVER, "Missing Dongle Driver" },
  { FLEXLM_FLEXLOCK2CKOUT, "2 FLEXlock checkouts attempted" },
  { FLEXLM_SIGN_REQ, "SIGN= attribute required, but missing from license" },
  { FLEXLM_PUBKEY_ERR, "Error in Public Key package" },
  { FLEXLM_NOCROSUPPORT, "CRO not supported for this platform" },
  { FLEXLM_BORROW_LINGER_ERR, "BORROW failed" },
  { FLEXLM_BORROW_EXPIRED, "BORROW period has expired" },
  { FLEXLM_MUST_BE_LOCAL, "lmdown and lmreread must be run on license server node" },
  { FLEXLM_BORROW_DOWN, "Cannot lmdown the server when licenses are borrowed" },
  { FLEXLM_FLOATOK_ONEHOSTID, "FLOAT_OK license must have exactly one dongle hostid" },
  { FLEXLM_BORROW_DELETE_ERR, "Unable to delete local borrow info" },
  { FLEXLM_BORROW_RETURN_EARLY_ERR, "Support for returning a borrowed license early is not enabled" },
  { FLEXLM_BORROW_RETURN_SERVER_ERR, "Error returning borrowed license on server" },
  { FLEXLM_CANT_CHECKOUT_JUST_PACKAGE, "Error when trying to just checkout a PACKAGE(BUNDLE)" },
  { FLEXLM_COMPOSITEID_INIT_ERR, "Composite Hostid not initialized" },
  { FLEXLM_COMPOSITEID_ITEM_ERR, "An item needed for Composite Hostid missing or invalid" },
  { FLEXLM_BORROW_MATCH_ERR, "Error, borrowed license doesn't match any known server license." },
  { FLEXLM_NULLPOINTER, "A null pointer was detected." },
  { FLEXLM_BADHANDLE, "A bad handle was used." },
  { FLEXLM_EMPTYSTRING, "An emptstring was detected." },
  { FLEXLM_BADMEMORYACCESS, "Tried to asscess memory that we shouldn't have." },
  { FLEXLM_NOTSUPPORTED, "Operation is not supported yet." },
  { FLEXLM_NULLJOBHANDLE, "The job handle was NULL." },
  { FLEXLM_EVENTLOG_INIT_ERR, "Error enabling event log" },
  { FLEXLM_EVENTLOG_DISABLED, "Event logging is disabled" },
  { FLEXLM_EVENTLOG_WRITE_ERR, "Error writing to event log" },
  { FLEXLM_LAST_ERRNO, "The last valid error number" },
  { 0, "" } };

#endif
