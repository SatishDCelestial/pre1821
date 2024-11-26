//---------------------------------------------------------------------------
// Copyright (c) 2009 Embarcadero Technologies, Inc.  All Rights Reserved.
//---------------------------------------------------------------------------

#pragma once

#if defined(SANCTUARY)

#if (0)

#include "Sanctuary/include/renew/ILicenseRenewalPrefs.h"

SANCT_BEGIN_NAMESPACE

class LicenseRenewalPrefs;

SANCT_END_NAMESPACE

SANCT_USING_NAMESPACE

// SanctLicenseRenewalPrefs

class SanctLicenseRenewalPrefs : public ILicenseRenewalPrefs {

 private:
    LicenseRenewalPrefs* renewalPrefs;

 protected:

 public:
	 SanctLicenseRenewalPrefs();
    ~SanctLicenseRenewalPrefs();

    virtual SanctLong getRenewalDuration(const Vector& v, SanctLong now);
    virtual SanctLong getRenewalExpiration(const Vector& v, SanctLong now);
    virtual SanctLong getRenewalInterval(const Vector& v, SanctLong now);
    virtual void filterRenewables(const Vector& v, Vector& renewables);
};
#else

#include "Sanctuary/include/renew/LicenseRenewalPrefs.h"

SANCT_USING_NAMESPACE

// SanctLicenseRenewalPrefs

class SanctLicenseRenewalPrefs : public LicenseRenewalPrefs {

 private:
    int skuId;

 protected:
    virtual bool isRenewable(const OrderedProductInfo* pi);

 public:
	 SanctLicenseRenewalPrefs(int sku);
};
#endif

#endif
