#pragma once

// BuyTryDlg_Strings.h
// Strings used by the Buy/Try dialog

#define LINEBREAK "\n"
#define MULTI_LINEBREAK "\r\n"

// License seat count exceeded MessageBox
// ----------------------------------------------------------------------------
// Displayed when the license seat count is exceeded.
//
#define kMsgBoxTitle_UserCountExceeded "Maximum Use Reached"
#define kMsgBoxText_UserCountExceeded                                                                                  \
	"You are attempting to exceed the number of PCs permitted to use your " LINEBREAK "license for " IDS_APPNAME       \
	". " LINEBREAK "\"%s\"" LINEBREAK LINEBREAK "Your IDE will run but " IDS_APPNAME                                   \
	" will be disabled. You should " LINEBREAK                                                                         \
	"exit your IDE and purchase an additional license, uninstall " LINEBREAK IDS_APPNAME                               \
	" from this PC, or uninstall " IDS_APPNAME LINEBREAK                                                               \
	"from the PC that should not use your license." LINEBREAK LINEBREAK                                                \
	"You must exit your IDE to prevent this PC from being counted by " LINEBREAK "license checks on other PCs."

// Armadillo ClockBack ErrorBox
// ----------------------------------------------------------------------------
// Displayed when armadillo clockback check fails.
//
#define kErrBoxText_ClockBack                                                                                          \
	IDS_APPNAME " is unable to start your trial. You may need to reboot your system. If the problem persists, "        \
	            "contact us at http://www.wholetomato.com/contact for assistance." LINEBREAK LINEBREAK "Error: CBX-3"

// Armadillo ClockForward ErrorBox
// ----------------------------------------------------------------------------
// Displayed when armadillo clockforward check fails.
//
#define kErrBoxText_ClockForward                                                                                       \
	IDS_APPNAME " is unable to start your trial. You may need to reboot your system. If the problem persists, "        \
	            "contact us at http://www.wholetomato.com/contact for assistance." LINEBREAK LINEBREAK "Error: CFX-3"

// Armadillo EXPIRED ErrorBox
// ----------------------------------------------------------------------------
// Displayed when armadillo returns EXPIRED.
// Occurs when mixing license types (like installing 1301 over 1418).
//
#define kErrBoxText_Expired                                                                                            \
	"There is a problem with your license for " IDS_APPNAME                                                            \
	". Please contact us at http://www.wholetomato.com/contact for assistance." LINEBREAK "Error: CEX-3"

// Buy Now MessageBox
// ----------------------------------------------------------------------------
// Displayed when user presses the Buy Now button
//
#define kMsgBoxTitle_BuyNow IDS_APPNAME
#define kMsgBoxText_BuyNow                                                                                             \
	"Press OK to launch your web browser and purchase a copy " LINEBREAK "of " IDS_APPNAME                             \
	". You receive a serial number which you " LINEBREAK "enter using the Register button."

// Renew Now MessageBox
// ----------------------------------------------------------------------------
// Displayed when user presses the Renew Now button
//
#define kMsgBoxTitle_RenewNow IDS_APPNAME
#define kMsgBoxText_RenewNow                                                                                           \
	"Press OK to launch your web browser and renew maintenance " LINEBREAK "for " IDS_APPNAME                          \
	". If you last activated " IDS_APPNAME " with a " LINEBREAK                                                        \
	"legacy, two-line activation key, you will receive a serial number " LINEBREAK                                     \
	"that lets you register this build of " IDS_APPNAME "."

// Buy / Try cancelled message
// ----------------------------------------------------------------------------
// Displayed when user presses cancel or the BuyTry fails.
//
#define kMsgBoxTitle_BuyTryCancel IDS_APPNAME
#define kMsgBoxText_BuyTryCancel                                                                                       \
	IDS_APPNAME " is loaded but dormant. You should uninstall the " LINEBREAK                                          \
	            "software or purchase a license if your trial has expired."
#define kMsgBoxText_BuyTryCancel10                                                                                     \
	IDS_APPNAME " is loaded but dormant. You should uninstall the " LINEBREAK                                          \
	            "software using the Visual Studio Extension Manager or purchase " LINEBREAK                            \
	            "a license if your trial has expired."

// Buy / Try cancelled message for expired non-perpetual license
// ----------------------------------------------------------------------------
// Displayed when user presses cancel with expired non-perpetual license.
//
#define kMsgBoxTitle_BuyTryCancel_NonPerpetualKeyExpired "Good-thru Date Expired"
#define kMsgBoxText_BuyTryCancel_NonPerpetualKeyExpired                                                                \
	"You are attempting to use " IDS_APPNAME " beyond the good-thru date in your activation key:" LINEBREAK            \
	"\"%s\"" LINEBREAK LINEBREAK "Your IDE will run but " IDS_APPNAME " will be disabled." LINEBREAK                   \
	"You should purchase a new license or uninstall " IDS_APPNAME "."

// BuyTry Dialog Text
// ----------------------------------------------------------------------------
// Do not use LINEBREAK in these.  Use MULTI_LINEBREAK instead.

// Instructions, trial period continues
#define kBuyTry_Instructions                                                                                           \
	"Welcome to " IDS_APPNAME " from Whole Tomato Software. You have been granted "                                    \
	"a license to try this software for evaluation. Your license expires "                                             \
	"in %d %s." MULTI_LINEBREAK MULTI_LINEBREAK "Press Try to continue your trial. "                                   \
	"Press Register if you are ready to register or activate " IDS_APPNAME ". "                                        \
	"Press Buy to purchase a license. "                                                                                \
	"Press Cancel to continue loading your IDE without " IDS_APPNAME "."

// Instructions, trial period continues, installed renewable license not eligible for current build
#define kBuyTry_Instructions_RenewableLicenseExpired                                                                   \
	"Welcome to " IDS_APPNAME " from Whole Tomato Software. The software maintenance associated with "                 \
	"your license for " IDS_APPNAME " has expired so you do not qualify to run this build. You have "                  \
	"been granted a license to run this build for a short trial. Continue using " IDS_APPNAME " while "                \
	"you renew software maintenance, or revert to a previous build. Your trial expires "                               \
	"in %d %s. " MULTI_LINEBREAK MULTI_LINEBREAK "Press Try to continue your trial. "                                  \
	"Press Register if you are ready to register or activate this build of " IDS_APPNAME ". "                          \
	"Press Renew Now to renew software maintenance. "                                                                  \
	"Press Revert to download a build supported by your license. "                                                     \
	"Press Cancel to continue loading your IDE without " IDS_APPNAME "."

// Instructions, trial period continues, installed non-renewable license not eligible for current build
#define kBuyTry_Instructions_NonrenewableLicenseExpired                                                                \
	"Welcome to " IDS_APPNAME " from Whole Tomato Software. The software maintenance associated with "                 \
	"your license for " IDS_APPNAME " has expired so you do not qualify to run this build. You have "                  \
	"been granted a license to run this build for a short trial. Continue using " IDS_APPNAME " while "                \
	"you purchase a new license, or revert to a previous build. Your trial expires "                                   \
	"in %d %s. " MULTI_LINEBREAK MULTI_LINEBREAK "Press Try to continue your trial. "                                  \
	"Press Register if you are ready to register or activate this build of " IDS_APPNAME ". "                          \
	"Press Buy to purchase a new license. "                                                                            \
	"Press Revert to download a build supported by your license. "                                                     \
	"Press Cancel to continue loading your IDE without " IDS_APPNAME "."

// Trial period is over OR invalid license
#define kBuyTry_TrialOver                                                                                              \
	"Welcome to " IDS_APPNAME " from Whole Tomato Software. You were granted "                                         \
	"a license to try this software for evaluation and your evaluation period "                                        \
	"has expired. " MULTI_LINEBREAK MULTI_LINEBREAK                                                                    \
	"Press Register if you are ready to register or activate " IDS_APPNAME ". "                                        \
	"Press Buy to purchase a license. "                                                                                \
	"Press Cancel to continue loading your IDE without " IDS_APPNAME "."

// Valid renewable license is not eligible for current build and trial is over
#define kBuyTry_TrialOver_RenewableLicenseExpired                                                                      \
	"Welcome to " IDS_APPNAME " from Whole Tomato Software. The software maintenance associated with "                 \
	"your license for " IDS_APPNAME                                                                                    \
	" has expired and you do not qualify to run this build. " MULTI_LINEBREAK MULTI_LINEBREAK                          \
	"Press Register if you are ready to register or activate " IDS_APPNAME ". "                                        \
	"Press Renew Now to renew software maintenance. "                                                                  \
	"Press Revert to download a build supported by your license. "                                                     \
	"Press Cancel to continue loading your IDE without " IDS_APPNAME ". "

// Valid non-renewable license is not eligible for current build and trial is over
#define kBuyTry_TrialOver_NonRenewableLicenseExpired                                                                   \
	"Welcome to " IDS_APPNAME " from Whole Tomato Software. The software maintenance associated with "                 \
	"your license for " IDS_APPNAME                                                                                    \
	" has expired and you do not qualify to run this build. " MULTI_LINEBREAK MULTI_LINEBREAK                          \
	"Press Register if you are ready to register or activate " IDS_APPNAME ". "                                        \
	"Press Buy to purchase a new license. "                                                                            \
	"Press Revert to download a build supported by your license. "                                                     \
	"Press Cancel to continue loading your IDE without " IDS_APPNAME ". "

// Valid non-perpetual license has expired
#define kBuyTry_NonPerpetualLicenseExpired                                                                             \
	"Welcome to " IDS_APPNAME " from Whole Tomato Software. The good-thru term associated with "                       \
	"your license for " IDS_APPNAME                                                                                    \
	" has expired and you no longer qualify to run the software. " MULTI_LINEBREAK MULTI_LINEBREAK                     \
	"Press Register if you are ready to register or activate " IDS_APPNAME ". "                                        \
	"Press Buy to purchase a new license. "                                                                            \
	"Press Cancel to continue loading your IDE without " IDS_APPNAME ". "

// Valid floating license has no unused seats
#define kBuyTry_NoFloatingSeatsAvailable                                                                               \
	"Welcome to " IDS_APPNAME " from Whole Tomato Software. The maximum number of users of your "                      \
	"concurrent license has been reached and you do not currently qualify to run the software. " MULTI_LINEBREAK       \
	    MULTI_LINEBREAK "Press Register if you are ready to register or activate " IDS_APPNAME                         \
	" using a different license. "                                                                                     \
	"Press Buy to purchase a new license. "                                                                            \
	"Press Cancel to continue loading your IDE without " IDS_APPNAME ". "

// Some sort of Sanctuary error
#define kBuyTry_SanctuaryError                                                                                         \
	"%s has reported an error and the " IDS_APPNAME " license can not be validated. "                                  \
	"The error text is: %s" MULTI_LINEBREAK MULTI_LINEBREAK                                                            \
	"Press Register if you are ready to register or activate " IDS_APPNAME " using a different license. "              \
	"Press Buy to purchase a new license. "                                                                            \
	"Press Cancel to continue loading your IDE without " IDS_APPNAME ". "
