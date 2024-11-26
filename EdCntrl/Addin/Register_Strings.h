#pragma once

// Register_Strings.h
// Strings used by Register dialog

#define LINEBREAK "\n"

// Short Invalid Key MessageBox
// ----------------------------------------------------------------------------
// Displayed when user input a key that is less than 20 characters (possibly
// a VA4 key).
//
#define kMsgBoxTitle_ShortInvalidKey "Invalid Key"
#define kMsgBoxText_ShortInvalidKey                                                                                    \
	"You entered an invalid key for this product. Please be sure" LINEBREAK                                            \
	"you have a valid key for this product and that you have typed" LINEBREAK "or pasted it correctly."

// Valid but expired license input MessageBox
// ----------------------------------------------------------------------------
// Displayed when the license info input is an expired license (ineligible to
// unlock the current build).
//
#define kMsgBoxTitle_KeyExpired "Software Maintenance Expired"
#define kMsgBoxText_RenewableKeyExpired                                                                                \
	"You entered a key which does not qualify to run this build. " LINEBREAK                                           \
	"You must renew software maintenance to run this build, or revert to a previous build. " LINEBREAK                 \
	"Previous builds can be downloaded from  " LINEBREAK "http://www.wholetomato.com/support/history.asp."

#define kMsgBoxText_NonrenewableKeyExpired                                                                             \
	"You entered a key which does not qualify to run this build. " LINEBREAK                                           \
	"You must purchase a new license to run this build, or revert to a previous build. " LINEBREAK                     \
	"Previous builds can be downloaded from  " LINEBREAK "http://www.wholetomato.com/support/history.asp."

// Valid but expired license input MessageBox -- non-perpetual license
// ----------------------------------------------------------------------------
// Displayed when the license info input is an expired license (ineligible to
// run any build at all).
//
#define kMsgBoxTitle_NonPerpetualKeyExpired "Good-thru Date Expired"
#define kMsgBoxText_NonPerpetualKeyExpired                                                                             \
	"You are attempting to use " IDS_APPNAME " beyond the good-thru date " LINEBREAK                                   \
	"in your activation key." LINEBREAK LINEBREAK "You must obtain a new activation key."

// Invalid key input MessageBox - missing first line
// ----------------------------------------------------------------------------
// Displayed when the license info input appears to be missing the first line.
//
#define kMsgBoxTitle_MissingUser "Incomplete License"
#define kMsgBoxText_MissingUser                                                                                        \
	"You entered an incomplete key for this product." LINEBREAK                                                        \
	"The first line of the two-line key is missing." LINEBREAK "Press ? in the Enter Key dialog if you need help."

// Invalid key input MessageBox
// ----------------------------------------------------------------------------
// Displayed when the license info input is not valid at all.
//
#define kMsgBoxTitle_InvalidKey "Invalid Key"
#define kMsgBoxText_InvalidKey                                                                                         \
	"You entered an invalid key for this product. Please be sure you" LINEBREAK                                        \
	"have a valid key for this product and that you have typed or" LINEBREAK "pasted it correctly."

// Ineligible renewal MessageBox
// ----------------------------------------------------------------------------
// Displayed when the user inputs a valid renewal license for which no previous
// license could be located and even when prompted, the user failed to produce
// a previous valid (expired or not) license.
//
#define kMsgBoxTitle_IneligibleRenewal "Invalid Key"
#define kMsgBoxText_IneligibleRenewal                                                                                  \
	"You entered a previous key for which renewal is not valid." LINEBREAK                                             \
	"Please be sure you have a previous key for this product and" LINEBREAK                                            \
	"that you have typed or pasted it correctly."

// Successful license input MessageBox
// ----------------------------------------------------------------------------
// Displayed when the license info input is valid and accepted.
//
#define kMsgBoxTitle_KeyAccepted "Registration Succeeded"
#define kMsgBoxText_KeyAccepted "Thank you for registering."

// This is appended to kMsgBoxText_KeyAccepted when the Register dlg is called
// from the Options dlg
#define kMsgBoxText_KeyAccepted_AdditionalInstructions                                                                 \
	"  Close and reopen the options dialog to view updated information on the Sytem Info page."
