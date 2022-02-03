/*
 * Copyright (c) Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <numeric>
#include "cachelib/allocator/CacheAllocator.h"
#include "cachelib/allocator/tests/TestBase.h"

namespace facebook {
namespace cachelib {
namespace tests {


using LruAllocatorConfig = CacheAllocatorConfig<LruAllocator>;
using LruMemoryTierConfigs = LruAllocatorConfig::MemoryTierConfigs;
using Strings = std::vector<std::string>;
using SizePair = std::tuple<size_t, size_t>;
using SizePairs = std::vector<SizePair>;
using StringDataKeyValue = std::pair<std::string, std::string>;
using StringDataKeyValues = std::vector<StringDataKeyValue>;

const size_t defaultTotalCacheSize{1 * 1024 * 1024 * 1024};
const std::string defaultCacheDir{"/var/metadataDir"};
const std::string defaultPmemPath{"/dev/shm/p1"};
const std::string defaultDaxPath{"/dev/dax0.0"};

template <typename Allocator>
class MemoryTiersTest: public AllocatorTest<Allocator> {
  public:
    void basicCheck(
        LruAllocatorConfig& actualConfig,
        const Strings& expectedPaths = {defaultPmemPath},
        size_t expectedTotalCacheSize = defaultTotalCacheSize,
        const std::string& expectedCacheDir = defaultCacheDir) {
      EXPECT_EQ(actualConfig.getCacheSize(), expectedTotalCacheSize);
      EXPECT_EQ(actualConfig.getMemoryTierConfigs().size(), expectedPaths.size());
      EXPECT_EQ(actualConfig.getCacheDir(), expectedCacheDir);
      auto configs = actualConfig.getMemoryTierConfigs();

      size_t sum_sizes = std::accumulate(configs.begin(), configs.end(), 0,
          [](const size_t i, const MemoryTierCacheConfig& config) { return i + config.getSize();});
      size_t sum_ratios = std::accumulate(configs.begin(), configs.end(), 0,
          [](const size_t i, const MemoryTierCacheConfig& config) { return i + config.getRatio();});

      EXPECT_EQ(sum_sizes, expectedTotalCacheSize);
      size_t partition_size = 0, remaining_capacity = actualConfig.getCacheSize();
      if (sum_ratios) {
        partition_size = actualConfig.getCacheSize() / sum_ratios;
      }

      for(auto i = 0; i < configs.size(); ++i) {
        auto &opt = std::get<FileShmSegmentOpts>(configs[i].getShmTypeOpts());
        EXPECT_EQ(opt.path, expectedPaths[i]);
        EXPECT_GT(configs[i].getSize(), 0);
        if (configs[i].getRatio() && (i < configs.size() - 1)) {
          EXPECT_EQ(configs[i].getSize(), partition_size * configs[i].getRatio());
        }
        remaining_capacity -= configs[i].getSize();
      }

      EXPECT_EQ(remaining_capacity, 0);
    }

    LruAllocatorConfig createTestCacheConfig(
        const Strings& tierPaths = {defaultPmemPath},
        const SizePairs& sizePairs = {std::make_tuple(1 /* ratio */, 0 /* size */)},
        bool setPosixForShm = true,
        size_t cacheSize = defaultTotalCacheSize,
        const std::string& cacheDir = defaultCacheDir) {
      LruAllocatorConfig cfg;
      cfg.setCacheSize(cacheSize)
         .enableCachePersistence(cacheDir);

      if (setPosixForShm)
         cfg.usePosixForShm();

      LruMemoryTierConfigs tierConfigs;
      tierConfigs.reserve(tierPaths.size());
      for(auto i = 0; i < tierPaths.size(); ++i) {
        if (tierPaths[i].empty()) {
          tierConfigs.push_back(MemoryTierCacheConfig::fromShm()
                                .setRatio(std::get<0>(sizePairs[i]))
                                .setSize(std::get<1>(sizePairs[i])));
        } else {
          tierConfigs.push_back(MemoryTierCacheConfig::fromFile(tierPaths[i])
                                .setRatio(std::get<0>(sizePairs[i]))
                                .setSize(std::get<1>(sizePairs[i])));
        }
      }
      cfg.configureMemoryTiers(tierConfigs);
      return cfg;
    }
};

using LruMemoryTiersTest = MemoryTiersTest<LruAllocator>;

TEST_F(LruMemoryTiersTest, TestValid1TierPmemRatioConfig) {
  LruAllocatorConfig cfg = createTestCacheConfig({defaultPmemPath});
  basicCheck(cfg);
}

TEST_F(LruMemoryTiersTest, TestValid1TierDaxRatioConfig) {
  LruAllocatorConfig cfg = createTestCacheConfig({defaultDaxPath});
  basicCheck(cfg, {defaultDaxPath});
}

TEST_F(LruMemoryTiersTest, TestValid1TierDaxSizeConfig) {
  LruAllocatorConfig cfg = createTestCacheConfig({defaultDaxPath},
                                                 {std::make_tuple(0, defaultTotalCacheSize)},
                                                 /* setPosixShm */ true,
                                                 /* cacheSize */ 0);
  basicCheck(cfg, {defaultDaxPath});

  // Setting size after conifguringMemoryTiers with sizes is not allowed.
  EXPECT_THROW(cfg.setCacheSize(defaultTotalCacheSize + 1), std::invalid_argument);
}

TEST_F(LruMemoryTiersTest, TestValid2TierDaxPmemConfig) {
  LruAllocatorConfig cfg = createTestCacheConfig({defaultDaxPath, defaultPmemPath},
                                                 {std::make_tuple(1, 0), std::make_tuple(1, 0)});
  basicCheck(cfg, {defaultDaxPath, defaultPmemPath});
}

TEST_F(LruMemoryTiersTest, TestValid2TierDaxPmemRatioConfig) {
  LruAllocatorConfig cfg = createTestCacheConfig({defaultDaxPath, defaultPmemPath},
                                                 {std::make_tuple(5, 0), std::make_tuple(2, 0)});
  basicCheck(cfg, {defaultDaxPath, defaultPmemPath});
}

TEST_F(LruMemoryTiersTest, TestValid2TierDaxPmemSizeConfig) {
  size_t size_1 = 4321, size_2 = 1234;
  LruAllocatorConfig cfg = createTestCacheConfig({defaultDaxPath, defaultPmemPath},
                                                 {std::make_tuple(0, size_1), std::make_tuple(0, size_2)},
                                                 true, 0);
  basicCheck(cfg, {defaultDaxPath, defaultPmemPath}, size_1 + size_2);

  // Setting size after conifguringMemoryTiers with sizes is not allowed.
  EXPECT_THROW(cfg.setCacheSize(size_1 + size_2 + 1), std::invalid_argument);
}

TEST_F(LruMemoryTiersTest, TestInvalid2TierConfigPosixShmNotSet) {
  LruAllocatorConfig cfg = createTestCacheConfig({defaultDaxPath, defaultPmemPath},
                                                 {std::make_tuple(1, 0), std::make_tuple(1, 0)},
                                                  /* setPosixShm */ false);
}

TEST_F(LruMemoryTiersTest, TestInvalid2TierConfigNumberOfPartitionsTooLarge) {
  EXPECT_THROW(createTestCacheConfig({defaultDaxPath, defaultPmemPath},
                                     {std::make_tuple(defaultTotalCacheSize, 0), std::make_tuple(1, 0)}).validate(),
               std::invalid_argument);
}

TEST_F(LruMemoryTiersTest, TestInvalid2TierConfigSizesAndRatiosMixed) {
  EXPECT_THROW(createTestCacheConfig({defaultDaxPath, defaultPmemPath},
                                     {std::make_tuple(1, 0), std::make_tuple(1, 1)}),
               std::invalid_argument);
  EXPECT_THROW(createTestCacheConfig({defaultDaxPath, defaultPmemPath},
                                     {std::make_tuple(1, 1), std::make_tuple(0, 1)}),
               std::invalid_argument);
}

TEST_F(LruMemoryTiersTest, TestInvalid2TierConfigSizesAndRatioNotSet) {
  EXPECT_THROW(createTestCacheConfig({defaultDaxPath, defaultPmemPath},
                                     {std::make_tuple(1, 0), std::make_tuple(0, 0)}),
               std::invalid_argument);
}

TEST_F(LruMemoryTiersTest, TestInvalid2TierConfigRatiosCacheSizeNotSet) {
  EXPECT_THROW(createTestCacheConfig({defaultDaxPath, defaultPmemPath},
                                     {std::make_tuple(1, 0), std::make_tuple(1, 0)},
                                     /* setPosixShm */ true, /* cacheSize */ 0).validate(),
               std::invalid_argument);
}

TEST_F(LruMemoryTiersTest, TestInvalid2TierConfigSizesNeCacheSize) {
  EXPECT_THROW(createTestCacheConfig({defaultDaxPath, defaultPmemPath},
                                     {std::make_tuple(0, 1), std::make_tuple(0, 1)}),
               std::invalid_argument);
}

void generateAndInsert(LruAllocatorConfig& cfg,
                       const std::pair<size_t, size_t>& keyRange,
                       const size_t itemSize) {
  using Item = typename LruAllocator::Item;
  using RemoveCbData = typename LruAllocator::RemoveCbData;
  size_t itemsInserted = 0, itemsFound = 0, itemsEvicted = 0, itemsEvictedAndFound = 0;
  std::set<std::string> movedKeys;
  std::vector<RemoveCbData> removedData;
  std::unique_ptr<LruAllocator> alloc;

  auto moveCb = [&](const Item& oldItem, Item& newItem, Item*) {
    std::memcpy(newItem.getWritableMemory(), oldItem.getMemory(),
                oldItem.getSize());
    movedKeys.insert(oldItem.getKey().str());
  };

  auto removeCb = [&](const RemoveCbData& data) {
    removedData.push_back(data);
  };

  cfg.setRemoveCallback(removeCb);
  cfg.enableMovingOnSlabRelease(moveCb);

  if (cfg.getMemoryTierConfigs().size() > 1) {
    alloc = std::unique_ptr<LruAllocator>(new LruAllocator(LruAllocator::SharedMemNew, cfg));
  } else {
    alloc = std::unique_ptr<LruAllocator>(new LruAllocator(cfg));
  }

  const size_t numBytes = alloc->getCacheMemoryStats().cacheSize;
  auto poolId = alloc->addPool("default", numBytes);

  for(auto i = keyRange.first; i < keyRange.second; ++i) {
    auto key = std::to_string(i);
    std::string value = "";
    for (size_t j = 0; j < itemSize / key.length(); ++j) {
      value += key;
    }
    value += key.substr(0, itemSize % key.length());
    auto h = alloc->allocate(poolId, key, value.length());
    EXPECT_TRUE(h);

    std::memcpy(h->getWritableMemory(), value.c_str(), value.length());

    if (alloc->insert(h)) {
      ++itemsInserted;
    }
  }

  for(auto i = keyRange.first; i < keyRange.second; ++i) {
    auto key = std::to_string(i);
    auto x = alloc->find(key);
    if (x) {
      ++itemsFound;
    }
  }

  for (auto it = removedData.begin(); it != removedData.end(); ++it) {
    auto x = alloc->find((*it).item.getKey().str());
    if ((*it).context == facebook::cachelib::RemoveContext::kEviction) {
      if (x) {
        ++itemsEvictedAndFound;
      } else {
        ++itemsEvicted;
      }
    }
  }

  std::cout << "alloc.getCacheMemoryStats().cacheSize: " << numBytes << " (" << "cfg.getCacheSize(): " << cfg.getCacheSize() << ")";
  std::cout << "; Tiers: " << ((cfg.getMemoryTierConfigs().size() > 1)? "true" : "false");
  std::cout << "; Items inserted: " << itemsInserted;
  std::cout << "; Items found: " << itemsFound;
  std::cout << "; Items evicted: " << itemsEvicted;
  std::cout << " (total items removed: " << removedData.size();
  std::cout << ", items evicted and found: " << itemsEvictedAndFound << ")"<< std::endl;
}

LruAllocatorConfig create2TierCacheConfig(size_t totalCacheSize) {
  LruAllocatorConfig tieredCacheConfig{};
  tieredCacheConfig.setCacheSize(totalCacheSize)
    .enableCachePersistence(folly::sformat("/tmp/multi-tier-test/{}", ::getpid()))
    .usePosixForShm()
    .configureMemoryTiers(
      {MemoryTierCacheConfig::fromFile(folly::sformat("/tmp/tier1{}", ::getpid())).setRatio(1),
       MemoryTierCacheConfig::fromFile(folly::sformat("/tmp/tier2{}", ::getpid())).setRatio(1)});
  return tieredCacheConfig;
}

LruAllocatorConfig createDramCacheConfig(size_t totalCacheSize) {
  LruAllocatorConfig dramConfig{};
  dramConfig.setCacheSize(totalCacheSize);
  return dramConfig;
}

TEST_F(LruMemoryTiersTest, TestStressInserts) {
  std::vector<std::tuple<size_t, size_t, size_t, std::string>> stress_params = 
  {
    //params: total cache size, number of items to insert, size of data item, description
    std::make_tuple(50 * 1024 * 1024, 1000, 256, "Cache is undersaturated, just a few data items"),
    std::make_tuple(50 * 1024 * 1024, 108298, 256, "Data fills the entire 50Mb of DRAM-only cache"), 
    std::make_tuple(50 * 1024 * 1024, 180000, 256, "Too much data for the cache size")
  };

  for(auto params: stress_params) {
    LruAllocatorConfig tieredCacheConfig = create2TierCacheConfig(std::get<0>(params));
    LruAllocatorConfig dramCacheConfig = createDramCacheConfig(std::get<0>(params));

    std::cout << std::get<3>(params) << ":\n";
    generateAndInsert(tieredCacheConfig, std::make_pair(0, std::get<1>(params)), std::get<2>(params));
    generateAndInsert(dramCacheConfig, std::make_pair(0, std::get<1>(params)), std::get<2>(params));
    std::cout << "Done\n";
  }
}
} // namespace tests
} // namespace cachelib
} // namespace facebook
