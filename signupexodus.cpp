//
// Created by Hongbo Tang on 2018/7/5.
//

#include "signupexodus.hpp"

void signupexodus::transfer(account_name from, account_name to, asset quantity, string memo) {
    if (from == _self || to != _self) {
        return;
    }
    eosio_assert(quantity.symbol == CORE_SYMBOL, "signupexodus only accepts CORE for signup eos account");
    eosio_assert(quantity.is_valid(), "Invalid token transfer");
    eosio_assert(quantity.amount > 0, "Quantity must be positive");

    memo.erase(memo.begin(), find_if(memo.begin(), memo.end(), [](int ch) {
        return !isspace(ch);
    }));
    memo.erase(find_if(memo.rbegin(), memo.rend(), [](int ch) {
        return !isspace(ch);
    }).base(), memo.end());

    auto separator_pos = memo.find(' ');
    if (separator_pos == string::npos) {
        separator_pos = memo.find('-');
    }
    eosio_assert(separator_pos != string::npos, "Account name and public key must be separated by space or dash");

    string account_name_str = memo.substr(0, separator_pos);
    eosio_assert(account_name_str.length() == 12, "Length of account name should be 12");
    account_name new_account_name = string_to_name(account_name_str.c_str());

    string public_key_str = memo.substr(separator_pos + 1);
    eosio_assert(public_key_str.length() == 53, "Length of public key should be 53");

    string pubkey_prefix("EOS");
    auto result = mismatch(pubkey_prefix.begin(), pubkey_prefix.end(), public_key_str.begin());
    eosio_assert(result.first == pubkey_prefix.end(), "Public key should be prefixed with EOS");
    auto base58substr = public_key_str.substr(pubkey_prefix.length());

    vector<unsigned char> vch;
    eosio_assert(decode_base58(base58substr, vch), "Decode pubkey failed");
    eosio_assert(vch.size() == 37, "Invalid public key");

    array<unsigned char,33> pubkey_data;
    copy_n(vch.begin(), 33, pubkey_data.begin());

    checksum160 check_pubkey;
    ripemd160(reinterpret_cast<char *>(pubkey_data.data()), 33, &check_pubkey);
    eosio_assert(memcmp(&check_pubkey.hash, &vch.end()[-4], 4) == 0, "invalid public key");

    const int64_t net_stake = 200*2;  // Amount to stake for NET [1/10 mEOS]
    const int64_t cpu_stake = 9800*2; // Amount to stake for CPU [1/10 mEOS]
    const uint32_t bytes = 4096;      // Number of bytes of RAM to buy
    const double ram_fee = 0.005;     // Fee for buying RAM [percent]

    // https://github.com/Dappub/signupeoseos
    // https://eosio.stackexchange.com/questions/847/how-to-get-current-last-ram-price
    // https://eosio.stackexchange.com/questions/20/how-to-read-tables-from-other-smart-contracts
    auto _rammarket = rammarket(N(eosio), N(eosio));
    auto itr = _rammarket.find(S(4, RAMCORE));
    auto tmp = *itr;
    // Not sure how reliable this formula for calculating RAM price is. It reproduces the actual price
    // on testnet up to four decimal places. However it is not the actual algorhythm as implemented
    // in "contacts/eosio.system/exchange_state.cpp"
    double ramPrice = 1.0*tmp.quote.balance.amount/tmp.base.balance.amount;
    //print("Ram Price: ", ramPrice);

    // Buy RAM and take into account a 0.5% fee for buying RAM.
    asset buy_ram(ceil(ramPrice*bytes*(1.0 + ram_fee)), CORE_SYMBOL);
    asset stake_net(net_stake, CORE_SYMBOL);
    asset stake_cpu(cpu_stake, CORE_SYMBOL);
    asset liquid = quantity - stake_net - stake_cpu - buy_ram;
    eosio_assert(liquid.amount >= 0, "Not enough balance to buy ram");

    signup_public_key pubkey = {
        .type = 0,
        .data = pubkey_data,
    };
    key_weight pubkey_weight = {
        .key = pubkey,
        .weight = 1,
    };
    authority owner = authority{
        .threshold = 1,
        .keys = {pubkey_weight},
        .accounts = {},
        .waits = {}
    };
    authority active = authority{
        .threshold = 1,
        .keys = {pubkey_weight},
        .accounts = {},
        .waits = {}
    };
    newaccount new_account = newaccount{
        .creator = _self,
        .name = new_account_name,
        .owner = owner,
        .active = active
    };

    action(
        permission_level{ _self, N(active) },
        N(eosio),
        N(newaccount),
        new_account
    ).send();

    action(
        permission_level{ _self, N(active)},
        N(eosio),
        N(buyram),
        make_tuple(_self, new_account_name, buy_ram)
    ).send();

    action(
        permission_level{ _self, N(active)},
        N(eosio),
        N(delegatebw),
        make_tuple(_self, new_account_name, stake_net, stake_cpu, true)
    ).send();

    if(liquid.amount > 0) {
        action(
            permission_level{ _self, N(active) },
            N(eosio.token),
            N(transfer),
            std::make_tuple(_self, new_account_name, liquid, std::string(""))
        ).send();
    }
}