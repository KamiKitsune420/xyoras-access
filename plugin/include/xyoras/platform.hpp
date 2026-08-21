/*
 * XYORAS Access — process-level setup a plugin has to do for itself.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 */
#ifndef XYORAS_PLATFORM_HPP
#define XYORAS_PLATFORM_HPP

#include "xyoras/common.hpp"

namespace xyoras { namespace platform {

    /// Registers newlib's "sdmc:" devoptab so plain stdio works in this
    /// process. Must be called before anything that uses fopen -- most
    /// importantly eSpeak, which reads all its voice data that way.
    ///
    /// Idempotent. Returns false if the SD could not be mounted, in which case
    /// speech cannot start and the caller should degrade rather than continue.
    bool MountSdmc(void);

    bool IsSdmcMounted(void);

}}

#endif
