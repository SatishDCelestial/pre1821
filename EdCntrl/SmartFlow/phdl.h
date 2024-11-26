#pragma once

namespace SmartFlowPhoneHome
{
void FireLicenseCountViolation();
void FireInvalidLicenseTerminationDate();
void FireRenamedVaxDll();
void FireArmadilloMissing();
void FireArmadilloClockBack();
void FireArmadilloClockForward();
void FireArmadilloExpired();
void FireCancelExpiredLicense();
void FireCancelTrial();
void FireMissingHost();
} // namespace SmartFlowPhoneHome
