//---------------------------------------------------------------------------
// Copyright (c) 2009 Embarcadero Technologies, Inc.  All Rights Reserved.
//---------------------------------------------------------------------------

#include "stdafx.h"

#if defined(SANCTUARY)

#define _VIS_STD
#define _VIS_COMPAT

#include "Sanctuary/include/sanctuary.h"
#include "SanctLicenseRenewalPrefs.h"

#if (0)

#include "Sanctuary/include/renew/LicenseRenewalPrefs.h"

SANCT_USING_NAMESPACE

// SanctLicenseRenewalPrefs

SanctLicenseRenewalPrefs::SanctLicenseRenewalPrefs() {
    renewalPrefs = new LicenseRenewalPrefs();
}

SanctLicenseRenewalPrefs::~SanctLicenseRenewalPrefs() {
    if (renewalPrefs != NULL) {
	delete renewalPrefs;
    }
}

/*virtual*/ SanctLong SanctLicenseRenewalPrefs::getRenewalDuration(const Vector& v, SanctLong now) {
    return renewalPrefs->getRenewalDuration(v, now);
}

/*virtual*/ SanctLong SanctLicenseRenewalPrefs::getRenewalExpiration(const Vector& v, SanctLong now) {
    return renewalPrefs->getRenewalExpiration(v, now);
}

/*virtual*/ SanctLong SanctLicenseRenewalPrefs::getRenewalInterval(const Vector& v, SanctLong now) {
    return renewalPrefs->getRenewalInterval(v, now);
}

/*virtual*/ void SanctLicenseRenewalPrefs::filterRenewables(const Vector& v, Vector& renewables) {
    renewalPrefs->filterRenewables(v, renewables);
}
#else

#include "Sanctuary/include/OrderedProductInfo.h"

SANCT_USING_NAMESPACE

// SanctLicenseRenewalPrefs

/*virtual*/ bool SanctLicenseRenewalPrefs::isRenewable(const OrderedProductInfo* pi) {
    return (pi->getSkuId() == skuId);
}

SanctLicenseRenewalPrefs::SanctLicenseRenewalPrefs(int sku) :
    skuId(sku) {
}
#endif

#endif
