//---------------------------------------------------------------------------
// Copyright (c) 2009 Embarcadero Technologies, Inc.  All Rights Reserved.
//---------------------------------------------------------------------------
#include "stdafx.h"

#if defined(SANCTUARY)

#define _VIS_STD
#define _VIS_COMPAT

#include "Sanctuary/include/sanctuary.h"
#include "SanctLicenseRenewalCallback.h"

SANCT_USING_NAMESPACE

// SanctLicenseRenewalCallback

/*virtual*/ void SanctLicenseRenewalCallback::beginLicenseRenewal(const Vector& v) {
	(void)v;
}

/*virtual*/ void SanctLicenseRenewalCallback::endLicenseRenewal(const Vector& v, bool done) {
	(void)v; (void)done;
}

/*virtual*/ void SanctLicenseRenewalCallback::notifyExpired(const Vector& v) {
	(void)v;
}

/*virtual*/ void SanctLicenseRenewalCallback::warnExpiration(const Vector& v) {
	(void)v;
}

#endif
