#pragma once

#include "database_trx_integration.hpp"

namespace scorum {
namespace chain {

class dev_pool_service_i;
class account_service_i;
class withdraw_vesting_service_i;
class withdraw_vesting_route_service_i;
class dynamic_global_property_service_i;

class withdraw_vesting_check_fixture : public database_trx_integration_fixture
{
public:
    withdraw_vesting_check_fixture();

    account_service_i& account_service;
    dev_pool_service_i& pool_service;
    withdraw_vesting_service_i& withdraw_vesting_service;
    withdraw_vesting_route_service_i& withdraw_vesting_route_service;
    dynamic_global_property_service_i& dynamic_global_property_service;

protected:
    void create_dev_pool(const asset& balance_in = ASSET_NULL_SP, const asset& balance_out = ASSET_NULL_SCR);
};
}
}
