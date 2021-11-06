#include <extractor.hpp>

#include <math.h>


/**
* Initializes the config table. Only needs to be called once when first deploying the contract
* 
* @required_auth The contract itself
*/
ACTION extractor::init() {
    require_auth(get_self());
    config.get_or_create(get_self(), config_s{});

    if (marketplaces.find(name("").value) == marketplaces.end()) {
        marketplaces.emplace(get_self(), [&](auto &_marketplace) {
            _marketplace.marketplace_name = name("");
            _marketplace.creator = DEFAULT_MARKETPLACE_CREATOR;
        });
    }
}


/**
* Converts the now deprecated stake and auction counters in the config singleton
* into using the counters table
* 
* Calling this only is necessary when upgrading the contract from a lower version to 1.2.0
* When deploying a fresh contract, this action can be ignored completely
* 
* @required_auth The contract itself
*/
ACTION extractor::convcounters() {
    require_auth(get_self());

    config_s current_config = config.get();

    check(current_config.stake_counter != 0 && current_config.auction_counter != 0,
        "The stake counters have already been converted");

    counters.emplace(get_self(), [&](auto &_counter) {
        _counter.counter_name = name("stake");
        _counter.counter_value = current_config.stake_counter;
    });
    current_config.stake_counter = 0;

    counters.emplace(get_self(), [&](auto &_counter) {
        _counter.counter_name = name("auction");
        _counter.counter_value = current_config.auction_counter;
    });
    current_config.auction_counter = 0;

    config.set(current_config, get_self());
}


/**
* Sets the version for the config table
* 
* @required_auth The contract itself
*/
ACTION extractor::setversion(string new_version) {
    require_auth(get_self());

    config_s current_config = config.get();
    current_config.version = new_version;

    config.set(current_config, get_self());
}


/**
* Set address of apoc token
* 
* @required_auth The contract itself
*/
ACTION extractor::setapocaddr(name token_contract) {
    require_auth(get_self());

    config_s current_config = config.get();
    current_config.apoc_token.token_contract = token_contract;
    config.set(current_config, get_self());
}




/**
* Claim apoc token to user.
The specified asset is then transferred to the user.
* @required_auth owner
*/
ACTION extractor::claim(
    name owner,
    asset token_to_withdraw
) {
    require_auth(owner);

    check(token_to_withdraw.is_valid(), "Invalid type token_to_withdraw");

    internal_withdraw_tokens(owner, token_to_withdraw, "extractor Withdrawal");
}

/**
* get collection name and check validity of assets list
* does the staker own assets?
* are the items unique? aren't there any duplicated items?
* what is collection name for assets?
* are they in the valid collection ?
* are they transferable?
* is list size valid?
* @required_auth owner
*/
name extractor::get_collection_and_check_assets(
    name owner,
    vector <uint64_t> asset_ids
) {
    check(asset_ids.size() != 0, "asset_ids needs to contain at least one id");

    vector <uint64_t> asset_ids_copy = asset_ids;
    std::sort(asset_ids_copy.begin(), asset_ids_copy.end());
    check(std::adjacent_find(asset_ids_copy.begin(), asset_ids_copy.end()) == asset_ids_copy.end(),
        "The asset_ids must not contain duplicates");


    atomicassets::assets_t owner_assets = atomicassets::get_assets(owner);

    name assets_collection_name = name("");
    for (uint64_t asset_id : asset_ids) {
        auto asset_itr = owner_assets.require_find(asset_id,
            ("The specified account does not own at least one of the assets - "
            + to_string(asset_id)).c_str());

        if (asset_itr->template_id != -1) {
            atomicassets::templates_t asset_template = atomicassets::get_templates(asset_itr->collection_name);
            auto template_itr = asset_template.find(asset_itr->template_id);
            check(template_itr->transferable,
                ("At least one of the assets is not transferable - " + to_string(asset_id)).c_str());
        }

        if (assets_collection_name == name("")) {
            assets_collection_name = asset_itr->collection_name;
        } else {
            check(assets_collection_name == asset_itr->collection_name,
                "The specified asset ids must all belong to the same collection");
        }
    }

    return assets_collection_name;
}


/**
* Gets the current value of a counter and increments the counter by 1
* If no counter with the specified name exists yet, it is treated as if the counter was 1
*/
uint64_t extractor::consume_counter(name counter_name) {
    uint64_t value;
    auto counter_itr = counters.find(counter_name.value);
    if (counter_itr == counters.end()) {
        value = 1; // Starting with 1 instead of 0 because these ids can be front facing
        counters.emplace(get_self(), [&](auto &_counter) {
            _counter.counter_name = counter_name;
            _counter.counter_value = 2;
        });
    } else {
        value = counter_itr->counter_value;
        counters.modify(counter_itr, get_self(), [&](auto &_counter) {
            _counter.counter_value++;
        });
    }
    return value;
}


/**
* Create a stake listing
* For the stake to become active, the seller needs to create an atomicassets offer from them to the extractor
* account, offering (only) the assets to be sold with the memo "stake"
* 
* @required_auth owner
*/
ACTION extractor::stake(
    name owner,
    vector <uint64_t> asset_ids,
) {
    require_auth(owner);

    name assets_collection_name = get_collection_and_check_assets(owner, asset_ids);

    checksum256 asset_ids_hash = hash_asset_ids(asset_ids);

    auto stakes_by_hash = pool.get_index <name("assetidshash")>();
    auto stake_itr = stakes_by_hash.find(asset_ids_hash);

    while (stake_itr != stakes_by_hash.end()) {
        if (asset_ids_hash != stake_itr->asset_ids_hash()) {
            break;
        }

        check(stake_itr->owner != owner,
            "You have already staked these assets. You can cancel the stake using the cancelstake action.");

        stake_itr++;
    }
    uint64_t stake_id = consume_counter(name("stake"));
    pool.emplace(owner, [&](auto &_stake) {
        _stake.stake_id = stake_id;
        _stake.owner = owner;
        _stake.asset_ids = asset_ids;
        _stake.collection_name = assets_collection_name;
    });


    action(
        permission_level{get_self(), name("active")},
        get_self(),
        name("lognewstake"),
        make_tuple(
            stake_id,
            owner,
            asset_ids,
            assets_collection_name,
        )
    ).send();
}


/**
* Cancels a stake. 
* 
* If the stake is invalid (the stake still owns at least one
* of the assets on stake items, error
* 
* @required_auth The stake's owner
*/
ACTION extractor::unstake(
    uint64_t stake_id
) {
    auto stake_itr = pool.require_find(stake_id,
        "No stake with this stake_id exists");

    bool is_stake_invalid = false;

    atomicassets::assets_t staker_assets = atomicassets::get_assets(stake_itr->owner);
    for (uint64_t asset_id : stake_itr->asset_ids) {
        if (staker_assets.find(asset_id) != staker_assets.end()) {
            is_stake_invalid = true;
            break;
        }
    }
    check(is_stake_invalid ,
        "The stake is not invalid, therefore the authorization of the staker is needed to cancel it");
    pool.erase(stake_itr);
}



/**
* This function is called when a transfer receipt from any token contract is sent to the extractor contract
* It handels deposits and adds the transferred tokens to the sender's balance table row
*/
void extractor::receive_token_transfer(name from, name to, asset quantity, string memo) {
    if (to != get_self()) {
        return;
    }

    check(is_token_supported(get_first_receiver(), quantity.symbol), "The transferred token is not supported");

    if (memo == "claim") {
        internal_add_balance(from, quantity);
    } else {
        check(false, "invalid memo");
    }
}


ACTION extractor::lognewstake(
    uint64_t stake_id,
    name owner,
    vector <uint64_t> asset_ids,
    name collection_name,
) {
    require_auth(get_self());

    require_recipient(seller);
}

ACTION extractor::lognewclaim(
    name owner,
    vector <uint64_t> asset_ids,
    double amount
) {
    require_auth(get_self());

    require_recipient(owner);
}


/**
* Gets the author of a collection in the atomicassets contract
*/
name extractor::get_collection_author(name collection_name) {
    auto collection_itr = atomicassets::collections.find(collection_name.value);
    return collection_itr->author;
}



/**
* Gets the token_contract corresponding to the token_symbol from the config
* Throws if there is no supported token with the specified token_symbol
*/
name extractor::require_get_supported_token_contract(
    symbol token_symbol
) {
    config_s current_config = config.get();

    for (TOKEN supported_token : current_config.supported_tokens) {
        if (supported_token.token_symbol == token_symbol) {
            return supported_token.token_contract;
        }
    }

    check(false, "The specified token symbol is not supported");
    return name(""); //To silence the compiler warning
}


/**
* Internal function to check whether an token is a supported token
*/
bool extractor::is_token_supported(
    name token_contract,
    symbol token_symbol
) {
    config_s current_config = config.get();

    for (TOKEN supported_token : current_config.supported_tokens) {
        if (supported_token.token_contract == token_contract && supported_token.token_symbol == token_symbol) {
            return true;
        }
    }
    return false;
}


/**
* Internal function to check whether a supported token with this symbol exists
*/
bool extractor::is_symbol_supported(
    symbol token_symbol
) {
    config_s current_config = config.get();

    for (TOKEN supported_token : current_config.supported_tokens) {
        if (supported_token.token_symbol == token_symbol) {
            return true;
        }
    }
    return false;
}


/**
* Decreases the withdrawers balance by the specified quantity and transfers the tokens to them
* Throws if the withdrawer does not have a sufficient balance
*/
void extractor::internal_withdraw_tokens(
    name withdrawer,
    asset quantity,
    string memo
) {
    check(quantity.amount > 0, "The quantity to withdraw must be positive");

    //This will throw if the user does not have sufficient balance
    internal_decrease_balance(withdrawer, quantity);

    name withdraw_token_contract = require_get_supported_token_contract(quantity.symbol);

    action(
        permission_level{get_self(), name("active")},
        withdraw_token_contract,
        name("transfer"),
        make_tuple(
            get_self(),
            withdrawer,
            quantity,
            memo
        )
    ).send();
}

/**
* Internal function used to add a quantity of a token to an account's balance
* It is not checked whether the added token is a supported token, this has to be checked before calling this function
*/
void extractor::internal_add_balance(
    name owner,
    asset quantity
) {
    if (quantity.amount == 0) {
        return;
    }
    check(quantity.amount > 0, "Can't add negative balances");

    auto balance_itr = balances.find(owner.value);

    vector <asset> quantities;
    if (balance_itr == balances.end()) {
        //No balance table row exists yet
        quantities = {quantity};
        balances.emplace(get_self(), [&](auto &_balance) {
            _balance.owner = owner;
            _balance.quantities = quantities;
        });

    } else {
        //A balance table row already exists for owner
        quantities = balance_itr->quantities;

        bool found_token = false;
        for (asset &token : quantities) {
            if (token.symbol == quantity.symbol) {
                //If the owner already has a balance for the token, this balance is increased
                found_token = true;
                token.amount += quantity.amount;
                break;
            }
        }

        if (!found_token) {
            //If the owner does not already have a balance for the token, it is added to the vector
            quantities.push_back(quantity);
        }

        balances.modify(balance_itr, get_self(), [&](auto &_balance) {
            _balance.quantities = quantities;
        });
    }
}


/**
* Internal function used to deduct a quantity of a token from an account's balance
* If the account does not has less than that quantity in his balance, this function will cause the
* transaction to fail
*/
void extractor::internal_decrease_balance(
    name owner,
    asset quantity
) {
    auto balance_itr = balances.require_find(owner.value,
        "The specified account does not have a balance table row");

    vector <asset> quantities = balance_itr->quantities;
    bool found_token = false;
    for (auto itr = quantities.begin(); itr != quantities.end(); itr++) {
        if (itr->symbol == quantity.symbol) {
            found_token = true;
            check(itr->amount >= quantity.amount,
                "The specified account's balance is lower than the specified quantity");
            itr->amount -= quantity.amount;
            if (itr->amount == 0) {
                quantities.erase(itr);
            }
            break;
        }
    }
    check(found_token,
        "The specified account does not have a balance for the symbol specified in the quantity");

    //Updating the balances table
    if (quantities.size() > 0) {
        balances.modify(balance_itr, same_payer, [&](auto &_balance) {
            _balance.quantities = quantities;
        });
    } else {
        balances.erase(balance_itr);
    }
}


void extractor::internal_transfer_assets(
    name to,
    vector <uint64_t> asset_ids,
    string memo
) {
    action(
        permission_level{get_self(), name("active")},
        atomicassets::ATOMICASSETS_ACCOUNT,
        name("transfer"),
        make_tuple(
            get_self(),
            to,
            asset_ids,
            memo
        )
    ).send();
}