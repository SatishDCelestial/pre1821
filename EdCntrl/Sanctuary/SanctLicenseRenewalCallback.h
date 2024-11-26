//---------------------------------------------------------------------------
// Copyright (c) 2009 Embarcadero Technologies, Inc.  All Rights Reserved.
//---------------------------------------------------------------------------

#pragma once

#if defined(SANCTUARY)

#include "Sanctuary/include/renew/ILicenseRenewalCallback.h"

SANCT_USING_NAMESPACE

// TestLicenseRenewalCallback

class SanctLicenseRenewalCallback : public ILicenseRenewalCallback {

 private:

 protected:

 public:
    virtual void beginLicenseRenewal(const Vector& v);
    virtual void endLicenseRenewal(const Vector& v, bool done);
    virtual void notifyExpired(const Vector& v);
    virtual void warnExpiration(const Vector& v);
};

#endif
