/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/platforms/test_platforms/BcmTestYampPlatform.h"
#include "fboss/agent/platforms/common/PlatformProductInfo.h"
#include "fboss/agent/platforms/test_platforms/BcmTestYampPort.h"
#include "fboss/agent/platforms/wedge/yamp/YampPlatformMapping.h"

namespace facebook::fboss {

BcmTestYampPlatform::BcmTestYampPlatform(
    std::unique_ptr<PlatformProductInfo> productInfo)
    : BcmTestWedgeTomahawk3Platform(
          std::move(productInfo),
          std::make_unique<YampPlatformMapping>()) {}

std::unique_ptr<BcmTestPort> BcmTestYampPlatform::createTestPort(PortID id) {
  return std::make_unique<BcmTestYampPort>(id, this);
}

} // namespace facebook::fboss
