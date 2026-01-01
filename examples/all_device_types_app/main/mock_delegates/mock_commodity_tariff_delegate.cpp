/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "esp_log.h"
#include "mock_commodity_tariff_delegate.h"

namespace chip {
namespace app {
namespace Clusters {
namespace CommodityTariff {

MockCommodityTariffDelegate::MockCommodityTariffDelegate()
{
    // Initialize with example tariff data by directly modifying the value reference
    using namespace chip::app::Clusters::Globals;
    using namespace chip::app::DataModel;
    
    ESP_LOGI(LOG_TAG, "Initializing CommodityTariff delegate with example data");
    
    // Initialize TariffUnit (e.g., kKVAh)
    auto& tariffUnit = GetTariffUnit();
    tariffUnit.SetNonNull(TariffUnitEnum::kKVAh);
    
    // Initialize StartDate (Matter epoch timestamp, e.g., January 1, 2025)
    // Matter epoch = 2000-01-01 00:00:00 UTC
    auto& startDate = GetStartDate();
    startDate.SetNonNull(788908800);  // 2025-01-01 00:00:00 UTC (25 years * 365.25 days)
    
    // Initialize TariffInfo
    auto& tariffInfo = GetTariffInfo();
    tariffInfo.SetNonNull();
    tariffInfo.Value().tariffLabel = MakeNullable(chip::CharSpan::fromCharString("Test Tariff"));
    tariffInfo.Value().providerName = MakeNullable(chip::CharSpan::fromCharString("Test Provider"));
    tariffInfo.Value().currency = chip::MakeOptional(
        MakeNullable<Globals::Structs::CurrencyStruct::Type>(
            { .currency = 978, .decimalPoints = 2 }  // EUR with 2 decimals
        )
    );
    tariffInfo.Value().blockMode = MakeNullable(static_cast<BlockModeEnum>(0));
    
    // Initialize DayEntries with example time periods
    auto& dayEntries = GetDayEntries();
    // Create static array with 2 day entries
    static Structs::DayEntryStruct::Type entries[2];
    
    // Entry 1: Morning period (6:00 AM - 12:00 PM)
    entries[0].dayEntryID = 1;
    entries[0].startTime = 360;  // 6:00 AM in minutes from midnight
    entries[0].duration = chip::MakeOptional(360u);  // 6 hours duration
    entries[0].randomizationOffset = chip::MakeOptional(static_cast<int16_t>(0));
    entries[0].randomizationType = chip::MakeOptional(DayEntryRandomizationTypeEnum::kNone);
    
    // Entry 2: Evening period (6:00 PM - 10:00 PM)
    entries[1].dayEntryID = 2;
    entries[1].startTime = 1080;  // 6:00 PM in minutes from midnight
    entries[1].duration = chip::MakeOptional(240u);  // 4 hours duration
    entries[1].randomizationOffset = chip::MakeOptional(static_cast<int16_t>(0));
    entries[1].randomizationType = chip::MakeOptional(DayEntryRandomizationTypeEnum::kNone);
    
    dayEntries.SetNonNull(List<Structs::DayEntryStruct::Type>(entries, 2));
    
    // Initialize TariffComponents with example pricing
    auto& tariffComponents = GetTariffComponents();
    // Create static array with 2 tariff components
    static Structs::TariffComponentStruct::Type components[2];
    
    // Component 1: Peak pricing
    components[0].tariffComponentID = 1;
    components[0].price.Emplace();  // Create Optional
    components[0].price.Value().SetNonNull();  // Set Nullable to non-null
    components[0].price.Value().Value().priceType = TariffPriceTypeEnum::kStandard;
    components[0].price.Value().Value().price.SetValue(2500);  // 0.25 EUR per kWh
    components[0].price.Value().Value().priceLevel.SetValue(1);
    
    // Component 2: Off-peak pricing
    components[1].tariffComponentID = 2;
    components[1].price.Emplace();  // Create Optional
    components[1].price.Value().SetNonNull();  // Set Nullable to non-null
    components[1].price.Value().Value().priceType = TariffPriceTypeEnum::kStandard;
    components[1].price.Value().Value().price.SetValue(1200);  // 0.12 EUR per kWh
    components[1].price.Value().Value().priceLevel.SetValue(0);
    
    tariffComponents.SetNonNull(List<Structs::TariffComponentStruct::Type>(components, 2));
    
    ESP_LOGI(LOG_TAG, "CommodityTariff delegate initialized successfully");
}

} // namespace CommodityTariff
} // namespace Clusters
} // namespace app
} // namespace chip
