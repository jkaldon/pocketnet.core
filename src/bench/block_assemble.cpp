// Copyright (c) 2011-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bench/bench.h>
#include <chainparams.h>
#include <coins.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <miner.h>
#include <policy/policy.h>
#include <pow.h>
#include <scheduler.h>
#include <txdb.h>
#include <txmempool.h>
#include <utiltime.h>
#include <validation.h>
#include <validationinterface.h>

#include <boost/thread.hpp>

#include <list>
#include <vector>
#include "pocketdb/services/Serializer.h"


static std::shared_ptr<CBlock> StakeBlock(const CScript& coinbase_scriptPubKey)
{
    auto block = std::make_shared<CBlock>(
        BlockAssembler{Params()}
            .CreateNewBlock(coinbase_scriptPubKey, /* fMineWitnessTx */ true, /*fProofOfStake */ true)
            ->block);

    block->nTime = ::chainActive.Tip()->GetMedianTimePast() + 1;
    block->hashMerkleRoot = BlockMerkleRoot(*block);

    return block;
}


static std::shared_ptr<CBlock> PrepareBlock(const CScript& coinbase_scriptPubKey)
{
    auto block = std::make_shared<CBlock>(
        BlockAssembler{Params()}
            .CreateNewBlock(coinbase_scriptPubKey, /* fMineWitnessTx */ true, /*fProofOfStake */ false)
            ->block);

    block->nTime = ::chainActive.Tip()->GetMedianTimePast() + 1;
    block->hashMerkleRoot = BlockMerkleRoot(*block);

    return block;
}


static CTxIn MineBlock(const CScript& coinbase_scriptPubKey)
{
    auto block = PrepareBlock(coinbase_scriptPubKey);

    while (!CheckProofOfWork(block->GetHash(), block->nBits, Params().GetConsensus(), 0)) {
        ++block->nNonce;
        assert(block->nNonce);
    }

    CValidationState state;
    UniValue pocketData;
    bool ignored;
    auto[deserializeOk, pocketBlock] = PocketServices::Serializer::DeserializeBlock(*block);
    auto pocketBlockRef = std::make_shared<PocketBlock>(pocketBlock);
    bool processed{ProcessNewBlock(state, Params(), block, pocketBlockRef, true, true, &ignored)};
    assert(processed);

    return CTxIn{block->vtx[0]->GetHash(), 0};
}


static void AssembleBlock(benchmark::Bench& bench)
{
    const std::vector<unsigned char> op_true{OP_TRUE};
    CScriptWitness witness;
    witness.stack.push_back(op_true);

    uint256 witness_program;
    CSHA256().Write(&op_true[0], op_true.size()).Finalize(witness_program.begin());

    const CScript SCRIPT_PUB{CScript(OP_0) << std::vector<unsigned char>{witness_program.begin(), witness_program.end()}};

    InitScriptExecutionCache();

    boost::thread_group thread_group;
    CScheduler scheduler;
    {
        ::pblocktree.reset(new CBlockTreeDB(1 << 20, true));
        ::pcoinsdbview.reset(new CCoinsViewDB(1 << 23, true));
        ::pcoinsTip.reset(new CCoinsViewCache(pcoinsdbview.get()));

        const CChainParams& chainparams = Params();
        thread_group.create_thread(boost::bind(&CScheduler::serviceQueue, &scheduler));
        GetMainSignals().RegisterBackgroundSignalScheduler(scheduler);
        LoadGenesisBlock(chainparams);
        CValidationState state;
        ActivateBestChain(state, chainparams);
        assert(::chainActive.Tip() != nullptr);
        const bool witness_enabled{IsWitnessEnabled(::chainActive.Tip(), chainparams.GetConsensus())};
        assert(witness_enabled);
    }

    // Collect some loose transactions that spend the coinbases of our mined blocks
    constexpr size_t NUM_BLOCKS{200};
    std::array<CTransactionRef, NUM_BLOCKS - COINBASE_MATURITY + 1> txs;
    for (size_t b{0}; b < NUM_BLOCKS; ++b) {
        CMutableTransaction tx;
        tx.vin.push_back(MineBlock(SCRIPT_PUB));
        tx.vin.back().scriptWitness = witness;
        tx.vout.emplace_back(1337, SCRIPT_PUB);
        if (NUM_BLOCKS - b >= COINBASE_MATURITY)
            txs.at(b) = MakeTransactionRef(tx);
    }
    {
        LOCK(::cs_main); // Required for ::AcceptToMemoryPool.

        for (const auto& txr : txs) {
            CValidationState state;
            bool ret{::AcceptToMemoryPool(::mempool, state, txr, 
                nullptr /* pocketTx */,
                nullptr /* pfMissingInputs */,
                nullptr /* plTxnReplaced */,
                false /* bypass_limits */,
                0 /* nAbsurdFee */,
                false /* test_accept */)};
            assert(ret);
        }
    }

    bench.run([&] {
        StakeBlock(SCRIPT_PUB);
    });

    thread_group.interrupt_all();
    thread_group.join_all();
    GetMainSignals().FlushBackgroundCallbacks();
    GetMainSignals().UnregisterBackgroundSignalScheduler();
}

BENCHMARK(AssembleBlock);
