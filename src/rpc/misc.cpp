// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <config.h>
#include <httpserver.h>
#include <key_io.h>
#include <logging.h>
#include <node/context.h>
#include <outputtype.h>
#include <rpc/blockchain.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <scheduler.h>
#include <script/descriptor.h>
#include <util/check.h>
#include <util/strencodings.h>
#include <util/system.h>
#include <util/validation.h>

#include <univalue.h>

#include <cstdint>
#ifdef HAVE_MALLOC_INFO
#include <malloc.h>
#endif

static UniValue validateaddress(const Config &config,
                                const JSONRPCRequest &request) {
    RPCHelpMan{
        "validateaddress",
        "\nReturn information about the given bitcoin address.\n",
        {
            {"address", RPCArg::Type::STR, RPCArg::Optional::NO,
             "The bitcoin address to validate"},
        },
        RPCResult{
            "{\n"
            "  \"isvalid\" : true|false,       (boolean) If the address is "
            "valid or not. If not, this is the only property returned.\n"
            "  \"address\" : \"address\",        (string) The bitcoin address "
            "validated\n"
            "  \"scriptPubKey\" : \"hex\",       (string) The hex-encoded "
            "scriptPubKey generated by the address\n"
            "  \"isscript\" : true|false,      (boolean) If the key is a "
            "script\n"
            "}\n"},
        RPCExamples{HelpExampleCli("validateaddress",
                                   "\"1PSSGeFHDnKNxiEyFrD1wcEaHr9hrQDDWc\"") +
                    HelpExampleRpc("validateaddress",
                                   "\"1PSSGeFHDnKNxiEyFrD1wcEaHr9hrQDDWc\"")},
    }
        .Check(request);

    CTxDestination dest =
        DecodeDestination(request.params[0].get_str(), config.GetChainParams());
    bool isValid = IsValidDestination(dest);

    UniValue ret(UniValue::VOBJ);
    ret.pushKV("isvalid", isValid);

    if (isValid) {
        if (ret["address"].isNull()) {
            std::string currentAddress = EncodeDestination(dest, config);
            ret.pushKV("address", currentAddress);

            CScript scriptPubKey = GetScriptForDestination(dest);
            ret.pushKV("scriptPubKey",
                       HexStr(scriptPubKey.begin(), scriptPubKey.end()));

            UniValue detail = DescribeAddress(dest);
            ret.pushKVs(detail);
        }
    }
    return ret;
}

static UniValue createmultisig(const Config &config,
                               const JSONRPCRequest &request) {
    RPCHelpMan{
        "createmultisig",
        "\nCreates a multi-signature address with n signature of m keys "
        "required.\n"
        "It returns a json object with the address and redeemScript.\n",
        {
            {"nrequired", RPCArg::Type::NUM, RPCArg::Optional::NO,
             "The number of required signatures out of the n keys."},
            {"keys",
             RPCArg::Type::ARR,
             RPCArg::Optional::NO,
             "A json array of hex-encoded public keys.",
             {
                 {"key", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED,
                  "The hex-encoded public key"},
             }},
        },
        RPCResult{"{\n"
                  "  \"address\":\"multisigaddress\",  (string) The value of "
                  "the new multisig address.\n"
                  "  \"redeemScript\":\"script\"       (string) The string "
                  "value of the hex-encoded redemption script.\n"
                  "}\n"},
        RPCExamples{
            "\nCreate a multisig address from 2 public keys\n" +
            HelpExampleCli("createmultisig",
                           "2 "
                           "\"["
                           "\\\"03789ed0bb717d88f7d321a368d905e7430207ebbd82bd3"
                           "42cf11ae157a7ace5fd\\\","
                           "\\\"03dbc6764b8884a92e871274b87583e6d5c2a58819473e1"
                           "7e107ef3f6aa5a61626\\\"]\"") +
            "\nAs a JSON-RPC call\n" +
            HelpExampleRpc("createmultisig",
                           "2, "
                           "\"["
                           "\\\"03789ed0bb717d88f7d321a368d905e7430207ebbd82bd3"
                           "42cf11ae157a7ace5fd\\\","
                           "\\\"03dbc6764b8884a92e871274b87583e6d5c2a58819473e1"
                           "7e107ef3f6aa5a61626\\\"]\"")},
    }
        .Check(request);

    int required = request.params[0].get_int();

    // Get the public keys
    const UniValue &keys = request.params[1].get_array();
    std::vector<CPubKey> pubkeys;
    for (size_t i = 0; i < keys.size(); ++i) {
        if ((keys[i].get_str().length() ==
                 2 * CPubKey::COMPRESSED_PUBLIC_KEY_SIZE ||
             keys[i].get_str().length() == 2 * CPubKey::PUBLIC_KEY_SIZE) &&
            IsHex(keys[i].get_str())) {
            pubkeys.push_back(HexToPubKey(keys[i].get_str()));
        } else {
            throw JSONRPCError(
                RPC_INVALID_ADDRESS_OR_KEY,
                strprintf("Invalid public key: %s\n", keys[i].get_str()));
        }
    }

    // Get the output type
    OutputType output_type = OutputType::LEGACY;

    // Construct using pay-to-script-hash:
    FillableSigningProvider keystore;
    CScript inner;
    const CTxDestination dest = AddAndGetMultisigDestination(
        required, pubkeys, output_type, keystore, inner);

    UniValue result(UniValue::VOBJ);
    result.pushKV("address", EncodeDestination(dest, config));
    result.pushKV("redeemScript", HexStr(inner.begin(), inner.end()));

    return result;
}

UniValue deriveaddresses(const Config &config, const JSONRPCRequest &request) {
    RPCHelpMan{
        "deriveaddresses",
        {"\nDerives one or more addresses corresponding to an output "
         "descriptor.\n"
         "Examples of output descriptors are:\n"
         "    pkh(<pubkey>)                        P2PKH outputs for the given "
         "pubkey\n"
         "    sh(multi(<n>,<pubkey>,<pubkey>,...)) P2SH-multisig outputs for "
         "the given threshold and pubkeys\n"
         "    raw(<hex script>)                    Outputs whose scriptPubKey "
         "equals the specified hex scripts\n"
         "\nIn the above, <pubkey> either refers to a fixed public key in "
         "hexadecimal notation, or to an xpub/xprv optionally followed by one\n"
         "or more path elements separated by \"/\", where \"h\" represents a "
         "hardened child key.\n"
         "For more information on output descriptors, see the documentation in "
         "the doc/descriptors.md file.\n"},
        {
            {"descriptor", RPCArg::Type::STR, RPCArg::Optional::NO,
             "The descriptor."},
            {"begin", RPCArg::Type::NUM, RPCArg::Optional::OMITTED_NAMED_ARG,
             "If a ranged descriptor is used, this specifies the beginning of "
             "the range to import."},
            {"end", RPCArg::Type::NUM, RPCArg::Optional::OMITTED_NAMED_ARG,
             "If a ranged descriptor is used, this specifies the end of the "
             "range to import."},
        },
        RPCResult{"[ address ] (array) the derived addresses\n"},
        RPCExamples{"First three native segwit receive addresses\n" +
                    HelpExampleCli("deriveaddresses",
                                   "\"pkh([d34db33f/84h/0h/0h]"
                                   "xpub6DJ2dNUysrn5Vt36jH2KLBT2i1auw1tTSSomg8P"
                                   "hqNiUtx8QX2SvC9nrHu81fT41fvDUnhMjEzQgXnQjKE"
                                   "u3oaqMSzhSrHMxyyoEAmUHQbY/0/*)\" 0 2")}}
        .Check(request);

    RPCTypeCheck(request.params,
                 {UniValue::VSTR, UniValue::VNUM, UniValue::VNUM});
    const std::string desc_str = request.params[0].get_str();

    int range_begin = 0;
    int range_end = 0;

    if (request.params.size() >= 2) {
        if (request.params.size() == 2) {
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               "Missing range end parameter");
        }
        range_begin = request.params[1].get_int();
        range_end = request.params[2].get_int();
        if (range_begin < 0) {
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               "Range should be greater or equal than 0");
        }
        if (range_begin > range_end) {
            throw JSONRPCError(
                RPC_INVALID_PARAMETER,
                "Range end should be equal to or greater than begin");
        }
    }

    FlatSigningProvider provider;
    auto desc = Parse(desc_str, provider);
    if (!desc) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                           strprintf("Invalid descriptor"));
    }

    if (!desc->IsRange() && request.params.size() > 1) {
        throw JSONRPCError(
            RPC_INVALID_PARAMETER,
            "Range should not be specified for an un-ranged descriptor");
    }

    if (desc->IsRange() && request.params.size() == 1) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Range must be specified for a ranged descriptor");
    }

    UniValue addresses(UniValue::VARR);

    for (int i = range_begin; i <= range_end; ++i) {
        std::vector<CScript> scripts;
        if (!desc->Expand(i, provider, scripts, provider)) {
            throw JSONRPCError(
                RPC_INVALID_ADDRESS_OR_KEY,
                strprintf("Cannot derive script without private keys"));
        }

        for (const CScript &script : scripts) {
            CTxDestination dest;
            if (!ExtractDestination(script, dest)) {
                throw JSONRPCError(
                    RPC_INVALID_ADDRESS_OR_KEY,
                    strprintf(
                        "Descriptor does not have a corresponding address"));
            }

            addresses.push_back(EncodeDestination(dest, config));
        }
    }

    // This should not be possible, but an assert seems overkill:
    if (addresses.empty()) {
        throw JSONRPCError(RPC_MISC_ERROR, "Unexpected empty result");
    }

    return addresses;
}

static UniValue verifymessage(const Config &config,
                              const JSONRPCRequest &request) {
    RPCHelpMan{
        "verifymessage",
        "\nVerify a signed message\n",
        {
            {"address", RPCArg::Type::STR, RPCArg::Optional::NO,
             "The bitcoin address to use for the signature."},
            {"signature", RPCArg::Type::STR, RPCArg::Optional::NO,
             "The signature provided by the signer in base 64 encoding (see "
             "signmessage)."},
            {"message", RPCArg::Type::STR, RPCArg::Optional::NO,
             "The message that was signed."},
        },
        RPCResult{"true|false   (boolean) If the signature is verified or "
                  "not.\n"},
        RPCExamples{
            "\nUnlock the wallet for 30 seconds\n" +
            HelpExampleCli("walletpassphrase", "\"mypassphrase\" 30") +
            "\nCreate the signature\n" +
            HelpExampleCli(
                "signmessage",
                "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XX\" \"my message\"") +
            "\nVerify the signature\n" +
            HelpExampleCli("verifymessage", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4"
                                            "XX\" \"signature\" \"my "
                                            "message\"") +
            "\nAs a JSON-RPC call\n" +
            HelpExampleRpc("verifymessage", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4"
                                            "XX\", \"signature\", \"my "
                                            "message\"")},
    }
        .Check(request);

    LOCK(cs_main);

    std::string strAddress = request.params[0].get_str();
    std::string strSign = request.params[1].get_str();
    std::string strMessage = request.params[2].get_str();

    CTxDestination destination =
        DecodeDestination(strAddress, config.GetChainParams());
    if (!IsValidDestination(destination)) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid address");
    }

    const PKHash *pkhash = boost::get<PKHash>(&destination);
    if (!pkhash) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to key");
    }

    bool fInvalid = false;
    std::vector<uint8_t> vchSig = DecodeBase64(strSign.c_str(), &fInvalid);

    if (fInvalid) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                           "Malformed base64 encoding");
    }

    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << strMessage;

    CPubKey pubkey;
    if (!pubkey.RecoverCompact(ss.GetHash(), vchSig)) {
        return false;
    }

    return (pubkey.GetID() == *pkhash);
}

static UniValue signmessagewithprivkey(const Config &config,
                                       const JSONRPCRequest &request) {
    RPCHelpMan{
        "signmessagewithprivkey",
        "\nSign a message with the private key of an address\n",
        {
            {"privkey", RPCArg::Type::STR, RPCArg::Optional::NO,
             "The private key to sign the message with."},
            {"message", RPCArg::Type::STR, RPCArg::Optional::NO,
             "The message to create a signature of."},
        },
        RPCResult{"\"signature\"          (string) The signature of the "
                  "message encoded in base 64\n"},
        RPCExamples{"\nCreate the signature\n" +
                    HelpExampleCli("signmessagewithprivkey",
                                   "\"privkey\" \"my message\"") +
                    "\nVerify the signature\n" +
                    HelpExampleCli("verifymessage",
                                   "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XX\" "
                                   "\"signature\" \"my message\"") +
                    "\nAs a JSON-RPC call\n" +
                    HelpExampleRpc("signmessagewithprivkey",
                                   "\"privkey\", \"my message\"")},
    }
        .Check(request);

    std::string strPrivkey = request.params[0].get_str();
    std::string strMessage = request.params[1].get_str();

    CKey key = DecodeSecret(strPrivkey);
    if (!key.IsValid()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key");
    }

    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << strMessage;

    std::vector<uint8_t> vchSig;
    if (!key.SignCompact(ss.GetHash(), vchSig)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Sign failed");
    }

    return EncodeBase64(vchSig.data(), vchSig.size());
}

static UniValue setmocktime(const Config &config,
                            const JSONRPCRequest &request) {
    RPCHelpMan{
        "setmocktime",
        "\nSet the local time to given timestamp (-regtest only)\n",
        {
            {"timestamp", RPCArg::Type::NUM, RPCArg::Optional::NO,
             "Unix seconds-since-epoch timestamp\n"
             "   Pass 0 to go back to using the system time."},
        },
        RPCResults{},
        RPCExamples{""},
    }
        .Check(request);

    if (!config.GetChainParams().MineBlocksOnDemand()) {
        throw std::runtime_error(
            "setmocktime for regression testing (-regtest mode) only");
    }

    // For now, don't change mocktime if we're in the middle of validation, as
    // this could have an effect on mempool time-based eviction, as well as
    // IsInitialBlockDownload().
    // TODO: figure out the right way to synchronize around mocktime, and
    // ensure all call sites of GetTime() are accessing this safely.
    LOCK(cs_main);

    RPCTypeCheck(request.params, {UniValue::VNUM});
    int64_t mockTime = request.params[0].get_int64();
    if (mockTime < 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Timestamp must be 0 or greater");
    }
    SetMockTime(mockTime);

    return NullUniValue;
}

static UniValue mockscheduler(const Config &config,
                              const JSONRPCRequest &request) {
    RPCHelpMan{
        "mockscheduler",
        "\nBump the scheduler into the future (-regtest only)\n",
        {
            {"delta_time", RPCArg::Type::NUM, RPCArg::Optional::NO,
             "Number of seconds to forward the scheduler into the future."},
        },
        RPCResults{},
        RPCExamples{""},
    }
        .Check(request);

    if (!Params().IsMockableChain()) {
        throw std::runtime_error(
            "mockscheduler is for regression testing (-regtest mode) only");
    }

    // check params are valid values
    RPCTypeCheck(request.params, {UniValue::VNUM});
    int64_t delta_seconds = request.params[0].get_int64();
    if ((delta_seconds <= 0) || (delta_seconds > 3600)) {
        throw std::runtime_error(
            "delta_time must be between 1 and 3600 seconds (1 hr)");
    }

    // protect against null pointer dereference
    CHECK_NONFATAL(g_rpc_node);
    CHECK_NONFATAL(g_rpc_node->scheduler);
    g_rpc_node->scheduler->MockForward(std::chrono::seconds(delta_seconds));

    return NullUniValue;
}

static UniValue RPCLockedMemoryInfo() {
    LockedPool::Stats stats = LockedPoolManager::Instance().stats();
    UniValue obj(UniValue::VOBJ);
    obj.pushKV("used", uint64_t(stats.used));
    obj.pushKV("free", uint64_t(stats.free));
    obj.pushKV("total", uint64_t(stats.total));
    obj.pushKV("locked", uint64_t(stats.locked));
    obj.pushKV("chunks_used", uint64_t(stats.chunks_used));
    obj.pushKV("chunks_free", uint64_t(stats.chunks_free));
    return obj;
}

#ifdef HAVE_MALLOC_INFO
static std::string RPCMallocInfo() {
    char *ptr = nullptr;
    size_t size = 0;
    FILE *f = open_memstream(&ptr, &size);
    if (f) {
        malloc_info(0, f);
        fclose(f);
        if (ptr) {
            std::string rv(ptr, size);
            free(ptr);
            return rv;
        }
    }
    return "";
}
#endif

static UniValue getmemoryinfo(const Config &config,
                              const JSONRPCRequest &request) {
    /* Please, avoid using the word "pool" here in the RPC interface or help,
     * as users will undoubtedly confuse it with the other "memory pool"
     */
    RPCHelpMan{
        "getmemoryinfo",
        "Returns an object containing information about memory usage.\n",
        {
            {"mode", RPCArg::Type::STR, /* default */ "\"stats\"",
             "determines what kind of information is returned.\n"
             "  - \"stats\" returns general statistics about memory usage in "
             "the daemon.\n"
             "  - \"mallocinfo\" returns an XML string describing low-level "
             "heap state (only available if compiled with glibc 2.10+)."},
        },
        {
            RPCResult{
                "mode \"stats\"",
                "{\n"
                "  \"locked\": {               (json object) Information about "
                "locked memory manager\n"
                "    \"used\": xxxxx,          (numeric) Number of bytes used\n"
                "    \"free\": xxxxx,          (numeric) Number of bytes "
                "available in current arenas\n"
                "    \"total\": xxxxxxx,       (numeric) Total number of bytes "
                "managed\n"
                "    \"locked\": xxxxxx,       (numeric) Amount of bytes that "
                "succeeded locking. If this number is smaller than total, "
                "locking pages failed at some point and key data could be "
                "swapped to disk.\n"
                "    \"chunks_used\": xxxxx,   (numeric) Number allocated "
                "chunks\n"
                "    \"chunks_free\": xxxxx,   (numeric) Number unused chunks\n"
                "  }\n"
                "}\n"},
            RPCResult{"mode \"mallocinfo\"", "\"<malloc version=\"1\">...\"\n"},
        },
        RPCExamples{HelpExampleCli("getmemoryinfo", "") +
                    HelpExampleRpc("getmemoryinfo", "")},
    }
        .Check(request);

    std::string mode =
        request.params[0].isNull() ? "stats" : request.params[0].get_str();
    if (mode == "stats") {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("locked", RPCLockedMemoryInfo());
        return obj;
    } else if (mode == "mallocinfo") {
#ifdef HAVE_MALLOC_INFO
        return RPCMallocInfo();
#else
        throw JSONRPCError(
            RPC_INVALID_PARAMETER,
            "mallocinfo is only available when compiled with glibc 2.10+");
#endif
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "unknown mode " + mode);
    }
}

static void EnableOrDisableLogCategories(UniValue cats, bool enable) {
    cats = cats.get_array();
    for (size_t i = 0; i < cats.size(); ++i) {
        std::string cat = cats[i].get_str();

        bool success;
        if (enable) {
            success = LogInstance().EnableCategory(cat);
        } else {
            success = LogInstance().DisableCategory(cat);
        }

        if (!success) {
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               "unknown logging category " + cat);
        }
    }
}

static UniValue logging(const Config &config, const JSONRPCRequest &request) {
    RPCHelpMan{
        "logging",
        "Gets and sets the logging configuration.\n"
        "When called without an argument, returns the list of categories with "
        "status that are currently being debug logged or not.\n"
        "When called with arguments, adds or removes categories from debug "
        "logging and return the lists above.\n"
        "The arguments are evaluated in order \"include\", \"exclude\".\n"
        "If an item is both included and excluded, it will thus end up being "
        "excluded.\n"
        "The valid logging categories are: " +
            ListLogCategories() +
            "\n"
            "In addition, the following are available as category names with "
            "special meanings:\n"
            "  - \"all\",  \"1\" : represent all logging categories.\n"
            "  - \"none\", \"0\" : even if other logging categories are "
            "specified, ignore all of them.\n",
        {
            {"include",
             RPCArg::Type::ARR,
             RPCArg::Optional::OMITTED_NAMED_ARG,
             "A json array of categories to add debug logging",
             {
                 {"include_category", RPCArg::Type::STR,
                  RPCArg::Optional::OMITTED, "the valid logging category"},
             }},
            {"exclude",
             RPCArg::Type::ARR,
             RPCArg::Optional::OMITTED_NAMED_ARG,
             "A json array of categories to remove debug logging",
             {
                 {"exclude_category", RPCArg::Type::STR,
                  RPCArg::Optional::OMITTED, "the valid logging category"},
             }},
        },
        RPCResult{"{                   (json object where keys are the logging "
                  "categories, and values indicates its status\n"
                  "  \"category\": 0|1,  (numeric) if being debug logged "
                  "or not. 0:inactive, 1:active\n"
                  "  ...\n"
                  "}\n"},
        RPCExamples{
            HelpExampleCli("logging", "\"[\\\"all\\\"]\" \"[\\\"http\\\"]\"") +
            HelpExampleRpc("logging", "[\"all\"], \"[libevent]\"")},
    }
        .Check(request);

    uint32_t original_log_categories = LogInstance().GetCategoryMask();
    if (request.params[0].isArray()) {
        EnableOrDisableLogCategories(request.params[0], true);
    }

    if (request.params[1].isArray()) {
        EnableOrDisableLogCategories(request.params[1], false);
    }

    uint32_t updated_log_categories = LogInstance().GetCategoryMask();
    uint32_t changed_log_categories =
        original_log_categories ^ updated_log_categories;

    /**
     * Update libevent logging if BCLog::LIBEVENT has changed.
     * If the library version doesn't allow it, UpdateHTTPServerLogging()
     * returns false, in which case we should clear the BCLog::LIBEVENT flag.
     * Throw an error if the user has explicitly asked to change only the
     * libevent flag and it failed.
     */
    if (changed_log_categories & BCLog::LIBEVENT) {
        if (!UpdateHTTPServerLogging(
                LogInstance().WillLogCategory(BCLog::LIBEVENT))) {
            LogInstance().DisableCategory(BCLog::LIBEVENT);
            if (changed_log_categories == BCLog::LIBEVENT) {
                throw JSONRPCError(RPC_INVALID_PARAMETER,
                                   "libevent logging cannot be updated when "
                                   "using libevent before v2.1.1.");
            }
        }
    }

    UniValue result(UniValue::VOBJ);
    std::vector<CLogCategoryActive> vLogCatActive = ListActiveLogCategories();
    for (const auto &logCatActive : vLogCatActive) {
        result.pushKV(logCatActive.category, logCatActive.active);
    }

    return result;
}

static UniValue echo(const Config &config, const JSONRPCRequest &request) {
    if (request.fHelp) {
        throw std::runtime_error(RPCHelpMan{
            "echo|echojson ...",
            "\nSimply echo back the input arguments. This command is for "
            "testing.\n"
            "\nThe difference between echo and echojson is that echojson has "
            "argument conversion enabled in the client-side table in "
            "bitcoin-cli and the GUI. There is no server-side difference.",
            {},
            RPCResults{},
            RPCExamples{""},
        }
                                     .ToString());
    }

    CHECK_NONFATAL(request.params.size() != 100);

    return request.params;
}

// clang-format off
static const CRPCCommand commands[] = {
    //  category            name                      actor (function)        argNames
    //  ------------------- ------------------------  ----------------------  ----------
    { "control",            "getmemoryinfo",          getmemoryinfo,          {"mode"} },
    { "control",            "logging",                logging,                {"include", "exclude"} },
    { "util",               "validateaddress",        validateaddress,        {"address"} },
    { "util",               "createmultisig",         createmultisig,         {"nrequired","keys"} },
    { "util",               "deriveaddresses",        deriveaddresses,        {"descriptor", "begin", "end"} },
    { "util",               "verifymessage",          verifymessage,          {"address","signature","message"} },
    { "util",               "signmessagewithprivkey", signmessagewithprivkey, {"privkey","message"} },
    /* Not shown in help */
    { "hidden",             "setmocktime",            setmocktime,            {"timestamp"}},
    { "hidden",             "mockscheduler",          mockscheduler,          {"delta_time"}},
    { "hidden",             "echo",                   echo,                   {"arg0","arg1","arg2","arg3","arg4","arg5","arg6","arg7","arg8","arg9"}},
    { "hidden",             "echojson",               echo,                   {"arg0","arg1","arg2","arg3","arg4","arg5","arg6","arg7","arg8","arg9"}},
};
// clang-format on

void RegisterMiscRPCCommands(CRPCTable &t) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++) {
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
    }
}
