/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <app-common/zap-generated/cluster-objects.h>
#include <app/clusters/commodity-tariff-server/commodity-tariff-server.h>

/*
 * Mock CommodityTariff Delegate Implementation
 * This file provides a mock implementation of the CommodityTariff::Delegate interface
 * with example tariff data initialization.
*/

namespace chip {
namespace app {
namespace Clusters {
namespace CommodityTariff {

class MockCommodityTariffDelegate : public Delegate
{
public:
    MockCommodityTariffDelegate();
    ~MockCommodityTariffDelegate() override = default;

private:
    const char *LOG_TAG = "commodity-tariff";
};

} // namespace CommodityTariff
} // namespace Clusters
} // namespace app
} // namespace chip
