/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/api/BufferApi.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"

#include <gtest/gtest.h>

using namespace facebook::fboss;

class BufferApiTest : public ::testing::Test {
 public:
  void SetUp() override {
    fs = FakeSai::getInstance();
    sai_api_initialize(0, nullptr);
    bufferApi = std::make_unique<BufferApi>();
  }
  BufferPoolSaiId createBufferPool(
      sai_buffer_pool_type_t _type,
      sai_uint64_t _size,
      sai_buffer_pool_threshold_mode_t _mode) {
    SaiBufferPoolTraits::Attributes::Type type{_type};
    SaiBufferPoolTraits::Attributes::Size size{_size};
    SaiBufferPoolTraits::Attributes::ThresholdMode mode{_mode};
    SaiBufferPoolTraits::CreateAttributes c{type, size, mode};
    return bufferApi->create<SaiBufferPoolTraits>(c, 0);
  }
  void checkBufferPool(BufferPoolSaiId id) const {
    SaiBufferPoolTraits::Attributes::Type type;
    auto gotType = bufferApi->getAttribute(id, type);
    EXPECT_EQ(fs->bufferPoolManager.get(id).poolType, gotType);
    SaiBufferPoolTraits::Attributes::Size size;
    auto gotSize = bufferApi->getAttribute(id, size);
    EXPECT_EQ(fs->bufferPoolManager.get(id).size, gotSize);
    SaiBufferPoolTraits::Attributes::ThresholdMode mode;
    auto gotMode = bufferApi->getAttribute(id, mode);
    EXPECT_EQ(fs->bufferPoolManager.get(id).threshMode, gotMode);
  }

  BufferProfileSaiId createBufferProfile(BufferPoolSaiId _pool) {
    SaiBufferProfileTraits::Attributes::PoolId pool{_pool};
    SaiBufferProfileTraits::Attributes::ReservedBytes reservedBytes{42};
    SaiBufferProfileTraits::Attributes::ThresholdMode mode{
        SAI_BUFFER_PROFILE_THRESHOLD_MODE_DYNAMIC};
    SaiBufferProfileTraits::Attributes::SharedDynamicThreshold dynamicThresh{
        24};
    SaiBufferProfileTraits::CreateAttributes c{
        pool, reservedBytes, mode, dynamicThresh};
    return bufferApi->create<SaiBufferProfileTraits>(c, 0);
  }
  void checkBufferProfile(BufferProfileSaiId id) const {
    SaiBufferProfileTraits::Attributes::PoolId pool{};
    auto gotPool = bufferApi->getAttribute(id, pool);
    EXPECT_EQ(fs->bufferProfileManager.get(id).poolId, gotPool);
    SaiBufferProfileTraits::Attributes::ReservedBytes reservedBytes{};
    auto gotReservedBytes = bufferApi->getAttribute(id, reservedBytes);
    EXPECT_EQ(fs->bufferProfileManager.get(id).reservedBytes, gotReservedBytes);
    SaiBufferProfileTraits::Attributes::ThresholdMode mode{};
    auto gotThresholdMode = bufferApi->getAttribute(id, mode);
    EXPECT_EQ(fs->bufferProfileManager.get(id).threshMode, gotThresholdMode);
    SaiBufferProfileTraits::Attributes::SharedDynamicThreshold dynamicThresh{};
    auto gotDynamic = bufferApi->getAttribute(id, dynamicThresh);
    EXPECT_EQ(fs->bufferProfileManager.get(id).dynamicThreshold, gotDynamic);
  }

  std::shared_ptr<FakeSai> fs;
  std::unique_ptr<BufferApi> bufferApi;
};

TEST_F(BufferApiTest, createBufferPool) {
  auto saiBufferId = createBufferPool(
      SAI_BUFFER_POOL_TYPE_EGRESS,
      1024,
      SAI_BUFFER_POOL_THRESHOLD_MODE_DYNAMIC);
  checkBufferPool(saiBufferId);
}

TEST_F(BufferApiTest, getBufferPoolAttributes) {
  auto id = createBufferPool(
      SAI_BUFFER_POOL_TYPE_INGRESS, 42, SAI_BUFFER_POOL_THRESHOLD_MODE_DYNAMIC);
  EXPECT_EQ(
      bufferApi->getAttribute(id, SaiBufferPoolTraits::Attributes::Type{}),
      SAI_BUFFER_POOL_TYPE_INGRESS);
  EXPECT_EQ(
      bufferApi->getAttribute(id, SaiBufferPoolTraits::Attributes::Size{}), 42);
  EXPECT_EQ(
      bufferApi->getAttribute(
          id, SaiBufferPoolTraits::Attributes::ThresholdMode{}),
      SAI_BUFFER_POOL_THRESHOLD_MODE_DYNAMIC);
}

TEST_F(BufferApiTest, createBufferProfile) {
  auto saiBufferId = createBufferPool(
      SAI_BUFFER_POOL_TYPE_EGRESS,
      1024,
      SAI_BUFFER_POOL_THRESHOLD_MODE_DYNAMIC);
  auto profileId = createBufferProfile(saiBufferId);
  checkBufferProfile(profileId);
}

TEST_F(BufferApiTest, getBufferProfilelAttributes) {
  auto poolId = createBufferPool(
      SAI_BUFFER_POOL_TYPE_INGRESS, 42, SAI_BUFFER_POOL_THRESHOLD_MODE_DYNAMIC);
  auto id = createBufferProfile(poolId);
  EXPECT_EQ(
      bufferApi->getAttribute(id, SaiBufferProfileTraits::Attributes::PoolId{}),
      poolId);
  EXPECT_EQ(
      bufferApi->getAttribute(
          id, SaiBufferProfileTraits::Attributes::ReservedBytes{}),
      42);
  EXPECT_EQ(
      bufferApi->getAttribute(
          id, SaiBufferProfileTraits::Attributes::ThresholdMode{}),
      SAI_BUFFER_PROFILE_THRESHOLD_MODE_DYNAMIC);
  EXPECT_EQ(
      bufferApi->getAttribute(
          id, SaiBufferProfileTraits::Attributes::SharedDynamicThreshold{}),
      24);
}

TEST_F(BufferApiTest, setBufferProfileAttributes) {
  auto saiBufferId = createBufferPool(
      SAI_BUFFER_POOL_TYPE_EGRESS,
      1024,
      SAI_BUFFER_POOL_THRESHOLD_MODE_DYNAMIC);
  auto profileId = createBufferProfile(saiBufferId);
  bufferApi->setAttribute(
      profileId, SaiBufferProfileTraits::Attributes::ReservedBytes{24});
  EXPECT_EQ(
      bufferApi->getAttribute(
          profileId, SaiBufferProfileTraits::Attributes::ReservedBytes{}),
      24);

  bufferApi->setAttribute(
      profileId,
      SaiBufferProfileTraits::Attributes::ThresholdMode{
          SAI_BUFFER_PROFILE_THRESHOLD_MODE_DYNAMIC});
  EXPECT_EQ(
      bufferApi->getAttribute(
          profileId, SaiBufferProfileTraits::Attributes::ThresholdMode{}),
      SAI_BUFFER_PROFILE_THRESHOLD_MODE_DYNAMIC);

  bufferApi->setAttribute(
      profileId,
      SaiBufferProfileTraits::Attributes::SharedDynamicThreshold{42});
  EXPECT_EQ(
      bufferApi->getAttribute(
          profileId,
          SaiBufferProfileTraits::Attributes::SharedDynamicThreshold{}),
      42);
}
