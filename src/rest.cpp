// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <core_io.h>
#include <index/txindex.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <validation.h>
#include <pos.h>
#include <httpserver.h>
#include <rpc/blockchain.h>
#include <rpc/server.h>
#include <streams.h>
#include <sync.h>
#include <txmempool.h>
#include <utilstrencodings.h>
#include <version.h>
#include <boost/algorithm/string.hpp>
#include <univalue.h>

#include "pocketdb/services/ChainPostProcessing.h"
#include "pocketdb/consensus/Helper.h"
#include "pocketdb/services/Accessor.h"
#include "pocketdb/web/PocketFrontend.h"

static const size_t MAX_GETUTXOS_OUTPOINTS = 15; //allow a max of 15 outpoints to be queried at once

enum class RetFormat
{
    UNDEF,
    BINARY,
    HEX,
    JSON,
};

static const struct
{
    RetFormat rf;
    const char* name;
}rf_names[] = {
    {RetFormat::UNDEF,  ""},
    {RetFormat::BINARY, "bin"},
    {RetFormat::HEX,    "hex"},
    {RetFormat::JSON,   "json"},
};

struct CCoin {
    uint32_t nHeight;
    CTxOut out;

    CCoin() : nHeight(0) {}
    explicit CCoin(Coin&& in) : nHeight(in.nHeight), out(std::move(in.out)) {}

    SERIALIZE_METHODS(CCoin, obj)
    {
        uint32_t nTxVerDummy = 0;
        READWRITE(nTxVerDummy, obj.nHeight, obj.out);
    }
};

static bool RESTERR(HTTPRequest* req, enum HTTPStatusCode status, std::string message)
{
    req->WriteHeader("Content-Type", "text/plain");
    req->WriteReply(status, message + "\r\n");
    return false;
}

static RetFormat ParseDataFormat(std::string& param, const std::string& strReq)
{
    const std::string::size_type pos = strReq.rfind('.');
    if (pos == std::string::npos)
    {
        param = strReq;
        return rf_names[0].rf;
    }

    param = strReq.substr(0, pos);
    const std::string suff(strReq, pos + 1);

    for (unsigned int i = 0; i < ARRAYLEN(rf_names); i++)
        if (suff == rf_names[i].name)
            return rf_names[i].rf;

    /* If no suffix is found, return original string.  */
    param = strReq;
    return rf_names[0].rf;
}

static std::tuple <RetFormat, std::vector<std::string>> ParseParams(const std::string& strURIPart)
{
    std::string param;
    RetFormat rf = ParseDataFormat(param, strURIPart);

    std::vector <std::string> uriParts;
    if (param.length() > 1)
    {
        std::string strUriParams = param.substr(1);
        boost::split(uriParts, strUriParams, boost::is_any_of("/"));
    }

    return std::make_tuple(rf, uriParts);
};

static std::tuple<bool, int> TryGetParamInt(std::vector <std::string>& uriParts, int index)
{
    try
    {
        if ((int) uriParts.size() > index)
        {
            auto val = std::stoi(uriParts[index]);
            return std::make_tuple(true, val);
        }

        return std::make_tuple(false, 0);
    }
    catch (...)
    {
        return std::make_tuple(false, 0);
    }
}

static std::tuple<bool, std::string> TryGetParamStr(std::vector <std::string>& uriParts, int index)
{
    try
    {
        if ((int) uriParts.size() > index)
        {
            auto val = uriParts[index];
            return std::make_tuple(true, val);
        }

        return std::make_tuple(false, "");
    }
    catch (...)
    {
        return std::make_tuple(false, "");
    }
}

static std::string AvailableDataFormatsString()
{
    std::string formats;
    for (unsigned int i = 0; i < ARRAYLEN(rf_names); i++)
        if (strlen(rf_names[i].name) > 0)
        {
            formats.append(".");
            formats.append(rf_names[i].name);
            formats.append(", ");
        }

    if (formats.length() > 0)
        return formats.substr(0, formats.length() - 2);

    return formats;
}

static bool CheckWarmup(HTTPRequest* req)
{
    std::string statusmessage;
    if (RPCIsInWarmup(&statusmessage))
        return RESTERR(req, HTTP_SERVICE_UNAVAILABLE, "Service temporarily unavailable: " + statusmessage);
    return true;
}

static bool rest_headers(HTTPRequest* req,
    const std::string& strURIPart)
{
    if (!CheckWarmup(req))
        return false;
    std::string param;
    const RetFormat rf = ParseDataFormat(param, strURIPart);
    std::vector <std::string> path;
    boost::split(path, param, boost::is_any_of("/"));

    if (path.size() != 2)
        return RESTERR(req, HTTP_BAD_REQUEST, "No header count specified. Use /rest/headers/<count>/<hash>.<ext>.");

    long count = strtol(path[0].c_str(), nullptr, 10);
    if (count < 1 || count > 2000)
        return RESTERR(req, HTTP_BAD_REQUEST, "Header count out of range: " + path[0]);

    std::string hashStr = path[1];
    uint256 hash;
    if (!ParseHashStr(hashStr, hash))
        return RESTERR(req, HTTP_BAD_REQUEST, "Invalid hash: " + hashStr);

    std::vector<const CBlockIndex*> headers;
    headers.reserve(count);
    {
        LOCK(cs_main);
        const CBlockIndex* pindex = LookupBlockIndex(hash);
        while (pindex != nullptr && chainActive.Contains(pindex))
        {
            headers.push_back(pindex);
            if (headers.size() == (unsigned long) count)
                break;
            pindex = chainActive.Next(pindex);
        }
    }

    CDataStream ssHeader(SER_NETWORK, PROTOCOL_VERSION);
    for (const CBlockIndex* pindex: headers)
    {
        ssHeader << pindex->GetBlockHeader();
    }

    switch (rf)
    {
        case RetFormat::BINARY:
        {
            std::string binaryHeader = ssHeader.str();
            req->WriteHeader("Content-Type", "application/octet-stream");
            req->WriteReply(HTTP_OK, binaryHeader);
            return true;
        }

        case RetFormat::HEX:
        {
            std::string strHex = HexStr(ssHeader.begin(), ssHeader.end()) + "\n";
            req->WriteHeader("Content-Type", "text/plain");
            req->WriteReply(HTTP_OK, strHex);
            return true;
        }
        case RetFormat::JSON:
        {
            UniValue jsonHeaders(UniValue::VARR);
            {
                LOCK(cs_main);
                for (const CBlockIndex* pindex: headers)
                {
                    jsonHeaders.push_back(blockheaderToJSON(pindex));
                }
            }
            std::string strJSON = jsonHeaders.write() + "\n";
            req->WriteHeader("Content-Type", "application/json");
            req->WriteReply(HTTP_OK, strJSON);
            return true;
        }
        default:
        {
            return RESTERR(req, HTTP_NOT_FOUND, "output format not found (available: .bin, .hex)");
        }
    }
}

static bool rest_block(HTTPRequest* req,
    const std::string& strURIPart,
    bool showTxDetails)
{
    if (!CheckWarmup(req))
        return false;
    std::string hashStr;
    const RetFormat rf = ParseDataFormat(hashStr, strURIPart);

    uint256 hash;
    if (!ParseHashStr(hashStr, hash))
        return RESTERR(req, HTTP_BAD_REQUEST, "Invalid hash: " + hashStr);

    CBlock block;
    CBlockIndex* pblockindex = nullptr;
    {
        LOCK(cs_main);
        pblockindex = LookupBlockIndex(hash);
        if (!pblockindex)
        {
            return RESTERR(req, HTTP_NOT_FOUND, hashStr + " not found");
        }

        if (IsBlockPruned(pblockindex))
            return RESTERR(req, HTTP_NOT_FOUND, hashStr + " not available (pruned data)");

        if (!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus()))
            return RESTERR(req, HTTP_NOT_FOUND, hashStr + " not found");
    }

    CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION | RPCSerializationFlags());
    ssBlock << block;

    switch (rf)
    {
        case RetFormat::BINARY:
        {
            std::string binaryBlock = ssBlock.str();
            req->WriteHeader("Content-Type", "application/octet-stream");
            req->WriteReply(HTTP_OK, binaryBlock);
            return true;
        }

        case RetFormat::HEX:
        {
            std::string strHex = HexStr(ssBlock.begin(), ssBlock.end()) + "\n";
            req->WriteHeader("Content-Type", "text/plain");
            req->WriteReply(HTTP_OK, strHex);
            return true;
        }

        case RetFormat::JSON:
        {
            UniValue objBlock;
            {
                LOCK(cs_main);
                objBlock = blockToJSON(block, pblockindex, showTxDetails);
            }
            std::string strJSON = objBlock.write() + "\n";
            req->WriteHeader("Content-Type", "application/json");
            req->WriteReply(HTTP_OK, strJSON);
            return true;
        }

        default:
        {
            return RESTERR(req, HTTP_NOT_FOUND,
                "output format not found (available: " + AvailableDataFormatsString() + ")");
        }
    }
}

static bool rest_blockhash(HTTPRequest* req, const std::string& strURIPart)
{
    if (!CheckWarmup(req))
        return false;

    auto[rf, uriParts] = ParseParams(strURIPart);

    int height = chainActive.Height();

    try
    {
        if (!uriParts.empty())
            height = std::stoi(uriParts[0]);
    }
    catch (...) {}

    if (height < 0 || height > chainActive.Height())
        return RESTERR(req, HTTP_BAD_REQUEST, "Block height out of range");

    CBlockIndex* pblockindex = chainActive[height];
    std::string blockHash = pblockindex->GetBlockHash().GetHex();

    switch (rf)
    {
        case RetFormat::JSON:
        {
            UniValue result(UniValue::VOBJ);
            result.pushKV("height", height);
            result.pushKV("blockhash", blockHash);

            req->WriteHeader("Content-Type", "application/json");
            req->WriteReply(HTTP_OK, result.write() + "\n");
            return true;
        }
        default:
        {
            req->WriteHeader("Content-Type", "text/plain");
            req->WriteReply(HTTP_OK, blockHash + "\n");
            return true;
        }
    }
}

static bool rest_block_extended(HTTPRequest* req, const std::string& strURIPart)
{
    return rest_block(req, strURIPart, true);
}

static bool rest_block_notxdetails(HTTPRequest* req, const std::string& strURIPart)
{
    return rest_block(req, strURIPart, false);
}

// A bit of a hack - dependency on a function defined in rpc/blockchain.cpp
UniValue getblockchaininfo(const JSONRPCRequest& request);

static bool rest_chaininfo(HTTPRequest* req, const std::string& strURIPart)
{
    if (!CheckWarmup(req))
        return false;
    std::string param;
    const RetFormat rf = ParseDataFormat(param, strURIPart);

    switch (rf)
    {
        case RetFormat::JSON:
        {
            JSONRPCRequest jsonRequest;
            jsonRequest.params = UniValue(UniValue::VARR);
            UniValue chainInfoObject = getblockchaininfo(jsonRequest);
            std::string strJSON = chainInfoObject.write() + "\n";
            req->WriteHeader("Content-Type", "application/json");
            req->WriteReply(HTTP_OK, strJSON);
            return true;
        }
        default:
        {
            return RESTERR(req, HTTP_NOT_FOUND, "output format not found (available: json)");
        }
    }
}

static bool rest_mempool_info(HTTPRequest* req, const std::string& strURIPart)
{
    if (!CheckWarmup(req))
        return false;
    std::string param;
    const RetFormat rf = ParseDataFormat(param, strURIPart);

    switch (rf)
    {
        case RetFormat::JSON:
        {
            UniValue mempoolInfoObject = mempoolInfoToJSON();

            std::string strJSON = mempoolInfoObject.write() + "\n";
            req->WriteHeader("Content-Type", "application/json");
            req->WriteReply(HTTP_OK, strJSON);
            return true;
        }
        default:
        {
            return RESTERR(req, HTTP_NOT_FOUND, "output format not found (available: json)");
        }
    }
}

static bool rest_mempool_contents(HTTPRequest* req, const std::string& strURIPart)
{
    if (!CheckWarmup(req))
        return false;
    std::string param;
    const RetFormat rf = ParseDataFormat(param, strURIPart);

    switch (rf)
    {
        case RetFormat::JSON:
        {
            UniValue mempoolObject = mempoolToJSON(true);

            std::string strJSON = mempoolObject.write() + "\n";
            req->WriteHeader("Content-Type", "application/json");
            req->WriteReply(HTTP_OK, strJSON);
            return true;
        }
        default:
        {
            return RESTERR(req, HTTP_NOT_FOUND, "output format not found (available: json)");
        }
    }
}

static bool rest_tx(HTTPRequest* req, const std::string& strURIPart)
{
    if (!CheckWarmup(req))
        return false;
    std::string hashStr;
    const RetFormat rf = ParseDataFormat(hashStr, strURIPart);

    uint256 hash;
    if (!ParseHashStr(hashStr, hash))
        return RESTERR(req, HTTP_BAD_REQUEST, "Invalid hash: " + hashStr);

    if (g_txindex)
    {
        g_txindex->BlockUntilSyncedToCurrentChain();
    }

    CTransactionRef tx;
    uint256 hashBlock = uint256();
    if (!GetTransaction(hash, tx, Params().GetConsensus(), hashBlock, true))
        return RESTERR(req, HTTP_NOT_FOUND, hashStr + " not found");

    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION | RPCSerializationFlags());
    ssTx << tx;

    switch (rf)
    {
        case RetFormat::BINARY:
        {
            std::string binaryTx = ssTx.str();
            req->WriteHeader("Content-Type", "application/octet-stream");
            req->WriteReply(HTTP_OK, binaryTx);
            return true;
        }

        case RetFormat::HEX:
        {
            std::string strHex = HexStr(ssTx.begin(), ssTx.end()) + "\n";
            req->WriteHeader("Content-Type", "text/plain");
            req->WriteReply(HTTP_OK, strHex);
            return true;
        }

        case RetFormat::JSON:
        {
            UniValue objTx(UniValue::VOBJ);
            TxToUniv(*tx, hashBlock, objTx);
            std::string strJSON = objTx.write() + "\n";
            req->WriteHeader("Content-Type", "application/json");
            req->WriteReply(HTTP_OK, strJSON);
            return true;
        }

        default:
        {
            return RESTERR(req, HTTP_NOT_FOUND,
                "output format not found (available: " + AvailableDataFormatsString() + ")");
        }
    }
}

static bool rest_getutxos(HTTPRequest* req, const std::string& strURIPart)
{
    if (!CheckWarmup(req))
        return false;
    std::string param;
    const RetFormat rf = ParseDataFormat(param, strURIPart);

    std::vector <std::string> uriParts;
    if (param.length() > 1)
    {
        std::string strUriParams = param.substr(1);
        boost::split(uriParts, strUriParams, boost::is_any_of("/"));
    }

    // throw exception in case of an empty request
    std::string strRequestMutable = req->ReadBody();
    if (strRequestMutable.length() == 0 && uriParts.size() == 0)
        return RESTERR(req, HTTP_BAD_REQUEST, "Error: empty request");

    bool fInputParsed = false;
    bool fCheckMemPool = false;
    std::vector <COutPoint> vOutPoints;

    // parse/deserialize input
    // input-format = output-format, rest/getutxos/bin requires binary input, gives binary output, ...

    if (uriParts.size() > 0)
    {
        //inputs is sent over URI scheme (/rest/getutxos/checkmempool/txid1-n/txid2-n/...)
        if (uriParts[0] == "checkmempool") fCheckMemPool = true;

        for (size_t i = (fCheckMemPool) ? 1 : 0; i < uriParts.size(); i++)
        {
            uint256 txid;
            int32_t nOutput;
            std::string strTxid = uriParts[i].substr(0, uriParts[i].find('-'));
            std::string strOutput = uriParts[i].substr(uriParts[i].find('-') + 1);

            if (!ParseInt32(strOutput, &nOutput) || !IsHex(strTxid))
                return RESTERR(req, HTTP_BAD_REQUEST, "Parse error");

            txid.SetHex(strTxid);
            vOutPoints.push_back(COutPoint(txid, (uint32_t) nOutput));
        }

        if (vOutPoints.size() > 0)
            fInputParsed = true;
        else
            return RESTERR(req, HTTP_BAD_REQUEST, "Error: empty request");
    }

    switch (rf)
    {
        case RetFormat::HEX:
        {
            // convert hex to bin, continue then with bin part
            std::vector<unsigned char> strRequestV = ParseHex(strRequestMutable);
            strRequestMutable.assign(strRequestV.begin(), strRequestV.end());
        }

        case RetFormat::BINARY:
        {
            try
            {
                //deserialize only if user sent a request
                if (strRequestMutable.size() > 0)
                {
                    if (fInputParsed) //don't allow sending input over URI and HTTP RAW DATA
                        return RESTERR(req, HTTP_BAD_REQUEST,
                            "Combination of URI scheme inputs and raw post data is not allowed");

                    CDataStream oss(SER_NETWORK, PROTOCOL_VERSION);
                    oss << strRequestMutable;
                    oss >> fCheckMemPool;
                    oss >> vOutPoints;
                }
            } catch (const std::ios_base::failure&)
            {
                // abort in case of unreadable binary data
                return RESTERR(req, HTTP_BAD_REQUEST, "Parse error");
            }
            break;
        }

        case RetFormat::JSON:
        {
            if (!fInputParsed)
                return RESTERR(req, HTTP_BAD_REQUEST, "Error: empty request");
            break;
        }
        default:
        {
            return RESTERR(req, HTTP_NOT_FOUND,
                "output format not found (available: " + AvailableDataFormatsString() + ")");
        }
    }

    // limit max outpoints
    if (vOutPoints.size() > MAX_GETUTXOS_OUTPOINTS)
        return RESTERR(req, HTTP_BAD_REQUEST,
            strprintf("Error: max outpoints exceeded (max: %d, tried: %d)", MAX_GETUTXOS_OUTPOINTS, vOutPoints.size()));

    // check spentness and form a bitmap (as well as a JSON capable human-readable string representation)
    std::vector<unsigned char> bitmap;
    std::vector <CCoin> outs;
    std::string bitmapStringRepresentation;
    std::vector<bool> hits;
    bitmap.resize((vOutPoints.size() + 7) / 8);
    {
        auto process_utxos = [&vOutPoints, &outs, &hits](const CCoinsView& view, const CTxMemPool& mempool)
        {
            for (const COutPoint& vOutPoint: vOutPoints)
            {
                Coin coin;
                bool hit = !mempool.isSpent(vOutPoint) && view.GetCoin(vOutPoint, coin);
                hits.push_back(hit);
                if (hit) outs.emplace_back(std::move(coin));
            }
        };

        if (fCheckMemPool)
        {
            // use db+mempool as cache backend in case user likes to query mempool
            LOCK2(cs_main, mempool.cs);
            CCoinsViewCache& viewChain = *pcoinsTip;
            CCoinsViewMemPool viewMempool(&viewChain, mempool);
            process_utxos(viewMempool, mempool);
        }
        else
        {
            LOCK(cs_main);  // no need to lock mempool!
            process_utxos(*pcoinsTip, CTxMemPool());
        }

        for (size_t i = 0; i < hits.size(); ++i)
        {
            const bool hit = hits[i];
            bitmapStringRepresentation.append(
                hit ? "1" : "0"); // form a binary string representation (human-readable for json output)
            bitmap[i / 8] |= ((uint8_t) hit) << (i % 8);
        }
    }

    switch (rf)
    {
        case RetFormat::BINARY:
        {
            // serialize data
            // use exact same output as mentioned in Bip64
            CDataStream ssGetUTXOResponse(SER_NETWORK, PROTOCOL_VERSION);
            ssGetUTXOResponse << chainActive.Height() << chainActive.Tip()->GetBlockHash() << bitmap << outs;
            std::string ssGetUTXOResponseString = ssGetUTXOResponse.str();

            req->WriteHeader("Content-Type", "application/octet-stream");
            req->WriteReply(HTTP_OK, ssGetUTXOResponseString);
            return true;
        }

        case RetFormat::HEX:
        {
            CDataStream ssGetUTXOResponse(SER_NETWORK, PROTOCOL_VERSION);
            ssGetUTXOResponse << chainActive.Height() << chainActive.Tip()->GetBlockHash() << bitmap << outs;
            std::string strHex = HexStr(ssGetUTXOResponse.begin(), ssGetUTXOResponse.end()) + "\n";

            req->WriteHeader("Content-Type", "text/plain");
            req->WriteReply(HTTP_OK, strHex);
            return true;
        }

        case RetFormat::JSON:
        {
            UniValue objGetUTXOResponse(UniValue::VOBJ);

            // pack in some essentials
            // use more or less the same output as mentioned in Bip64
            objGetUTXOResponse.pushKV("chainHeight", chainActive.Height());
            objGetUTXOResponse.pushKV("chaintipHash", chainActive.Tip()->GetBlockHash().GetHex());
            objGetUTXOResponse.pushKV("bitmap", bitmapStringRepresentation);

            UniValue utxos(UniValue::VARR);
            for (const CCoin& coin: outs)
            {
                UniValue utxo(UniValue::VOBJ);
                utxo.pushKV("height", (int32_t) coin.nHeight);
                utxo.pushKV("value", ValueFromAmount(coin.out.nValue));

                // include the script in a json output
                UniValue o(UniValue::VOBJ);
                ScriptPubKeyToUniv(coin.out.scriptPubKey, o, true);
                utxo.pushKV("scriptPubKey", o);
                utxos.push_back(utxo);
            }
            objGetUTXOResponse.pushKV("utxos", utxos);

            // return json string
            std::string strJSON = objGetUTXOResponse.write() + "\n";
            req->WriteHeader("Content-Type", "application/json");
            req->WriteReply(HTTP_OK, strJSON);
            return true;
        }
        default:
        {
            return RESTERR(req, HTTP_NOT_FOUND,
                "output format not found (available: " + AvailableDataFormatsString() + ")");
        }
    }
}

static bool rest_topaddresses(HTTPRequest* req, const std::string& strURIPart)
{
    if (!CheckWarmup(req))
        return false;

    auto[rf, uriParts] = ParseParams(strURIPart);

    int count = 30;
    if (!uriParts.empty())
    {
        count = std::stoi(uriParts[0]);
        if (count > 1000)
            count = 1000;
    }

    auto result = req->DbConnection()->WebRpcRepoInst->GetTopAddresses(count);
    req->WriteHeader("Content-Type", "application/json");
    req->WriteReply(HTTP_OK, result.write() + "\n");
    return true;
}

static bool rest_emission(HTTPRequest* req, const std::string& strURIPart)
{
    if (!CheckWarmup(req))
        return false;

    auto[rf, uriParts] = ParseParams(strURIPart);

    int height = chainActive.Height();
    if (auto[ok, result] = TryGetParamInt(uriParts, 0); ok)
        height = result;

    int first75 = 3750000;
    int halvblocks = 2'100'000;
    double emission = 0;
    int nratio = height / halvblocks;
    double mult;

    for (int i = 0; i <= nratio; ++i)
    {
        mult = 5. / pow(2., static_cast<double>(i));
        if (i < nratio || nratio == 0)
        {
            if (i == 0 && height < 75'000)
                emission += height * 50;
            else if (i == 0)
            {
                emission += first75 + (std::min(height, halvblocks) - 75000) * 5;
            }
            else
            {
                emission += 2'100'000 * mult;
            }
        }

        if (i == nratio && nratio != 0)
        {
            emission += (height % halvblocks) * mult;
        }
    }

    switch (rf)
    {
        case RetFormat::JSON:
        {
            UniValue result(UniValue::VOBJ);
            result.pushKV("height", height);
            result.pushKV("emission", emission);

            req->WriteHeader("Content-Type", "application/json");
            req->WriteReply(HTTP_OK, result.write() + "\n");
            return true;
        }
        default:
        {
            req->WriteHeader("Content-Type", "text/plain");
            req->WriteReply(HTTP_OK, std::to_string(emission) + "\n");
            return true;
        }
    }
}

static bool get_static_web(HTTPRequest* req, const std::string& strURIPart)
{
    if (!CheckWarmup(req))
        return false;

    if (strURIPart.find("clear") == 0)
    {
        PocketWeb::PocketFrontendInst.ClearCache();
        req->WriteReply(HTTP_OK, "Cache cleared!");
        return true;
    }

    if (auto[code, file] = PocketWeb::PocketFrontendInst.GetFile(strURIPart); code == HTTP_OK)
    {
        req->WriteHeader("Content-Type", file->ContentType);
        req->WriteReply(code, file->Content);
        return true;
    }
    else
    {
        return RESTERR(req, code, "");
    }

    return RESTERR(req, HTTP_NOT_FOUND, "");
}

static const struct
{
    const char* prefix;
    bool (* handler)(HTTPRequest* req, const std::string& strReq);
}uri_prefixes[] = {

    {"/rest/tx/",                rest_tx},
    {"/rest/block/notxdetails/", rest_block_notxdetails},
    {"/rest/block/",             rest_block_extended},
    {"/rest/chaininfo",          rest_chaininfo},
    {"/rest/mempool/info",       rest_mempool_info},
    {"/rest/mempool/contents",   rest_mempool_contents},
    {"/rest/headers/",           rest_headers},
    {"/rest/getutxos",           rest_getutxos},
    {"/rest/emission",           rest_emission},
    {"/rest/getemission",        rest_emission},
    {"/rest/topaddresses",       rest_topaddresses},
    {"/rest/gettopaddresses",    rest_topaddresses},
    {"/rest/blockhash",          rest_blockhash},
};

void StartREST()
{
    if (g_restSocket)
        for (auto uri_prefixe: uri_prefixes)
            g_restSocket->RegisterHTTPHandler(uri_prefixe.prefix, false, uri_prefixe.handler, g_restSocket->m_workQueue);

    // Register web content route
    if (g_staticSocket)
        g_staticSocket->RegisterHTTPHandler("/", false, get_static_web, g_staticSocket->m_workQueue);
}

void InterruptREST()
{
}

void StopREST()
{
    if (g_restSocket)
        for (auto uri_prefixe: uri_prefixes)
            g_restSocket->UnregisterHTTPHandler(uri_prefixe.prefix, false);

    if (g_staticSocket)
        g_staticSocket->UnregisterHTTPHandler("/", false);
}
