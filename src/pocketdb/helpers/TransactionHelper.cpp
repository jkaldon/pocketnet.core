// Copyright (c) 2018-2022 The Pocketnet developers
// Distributed under the Apache 2.0 software license, see the accompanying
// https://www.apache.org/licenses/LICENSE-2.0

#include "pocketdb/helpers/TransactionHelper.h"

namespace PocketHelpers
{
    txnouttype TransactionHelper::ScriptType(const CScript& scriptPubKey)
    {
        std::vector<std::vector<unsigned char>> vSolutions;
        return Solver(scriptPubKey, vSolutions);
    }

    std::string TransactionHelper::ExtractDestination(const CScript& scriptPubKey)
    {
        CTxDestination destAddress;
        if (::ExtractDestination(scriptPubKey, destAddress))
            return EncodeDestination(destAddress);

        return "";
    }

    tuple<bool, string> TransactionHelper::GetPocketAuthorAddress(const CTransactionRef& tx)
    {
        if (tx->vout.size() < 2)
            return make_tuple(false, "");

        string address = ExtractDestination(tx->vout[1].scriptPubKey);
        return make_tuple(!address.empty(), address);
    }

    TxType TransactionHelper::ConvertOpReturnToType(const string& op)
    {
        if (op == OR_POST || op == OR_POSTEDIT)
            return TxType::CONTENT_POST;
        else if (op == OR_VIDEO)
            return TxType::CONTENT_VIDEO;
        else if (op == OR_ARTICLE)
            return TxType::CONTENT_ARTICLE;
        else if (op == OR_CONTENT_BOOST)
            return TxType::BOOST_CONTENT;
        else if (op == OR_CONTENT_DELETE)
            return TxType::CONTENT_DELETE;
        else if (op == OR_SCORE)
            return TxType::ACTION_SCORE_CONTENT;
        else if (op == OR_COMPLAIN)
            return TxType::ACTION_COMPLAIN;
        else if (op == OR_SUBSCRIBE)
            return TxType::ACTION_SUBSCRIBE;
        else if (op == OR_SUBSCRIBEPRIVATE)
            return TxType::ACTION_SUBSCRIBE_PRIVATE;
        else if (op == OR_UNSUBSCRIBE)
            return TxType::ACTION_SUBSCRIBE_CANCEL;
        else if (op == OR_ACCOUNT_SETTING)
            return TxType::ACCOUNT_SETTING;
        else if (op == OR_USERINFO)
            return TxType::ACCOUNT_USER;
        else if (op == OR_VIDEO_SERVER)
            return TxType::ACCOUNT_VIDEO_SERVER;
        else if (op == OR_MESSAGE_SERVER)
            return TxType::ACCOUNT_MESSAGE_SERVER;
        else if (op == OR_BLOCKING)
            return TxType::ACTION_BLOCKING;
        else if (op == OR_UNBLOCKING)
            return TxType::ACTION_BLOCKING_CANCEL;
        else if (op == OR_COMMENT)
            return TxType::CONTENT_COMMENT;
        else if (op == OR_COMMENT_EDIT)
            return TxType::CONTENT_COMMENT_EDIT;
        else if (op == OR_COMMENT_DELETE)
            return TxType::CONTENT_COMMENT_DELETE;
        else if (op == OR_COMMENT_SCORE)
            return TxType::ACTION_SCORE_COMMENT;

        return TxType::TX_DEFAULT;
    }

    string TransactionHelper::ParseAsmType(const CTransactionRef& tx, vector<string>& vasm)
    {
        if (tx->vout.empty())
            return "";

        const CTxOut& txout = tx->vout[0];
        if (txout.scriptPubKey[0] == OP_RETURN)
        {
            auto asmStr = ScriptToAsmStr(txout.scriptPubKey);
            boost::split(vasm, asmStr, boost::is_any_of("\t "));
            if (vasm.size() >= 2)
                return vasm[1];
        }

        return "";
    }

    TxType TransactionHelper::ParseType(const CTransactionRef& tx, vector<string>& vasm)
    {
        if (tx->IsCoinBase())
        {
            int txOutSum = std::accumulate(begin(tx->vout), end(tx->vout), 0,
                [](int i, const CTxOut& o) { return o.nValue + i; });

            if (txOutSum <= 0)
                return TxType::NOT_SUPPORTED;
            else
                return TxType::TX_COINBASE;
        }

        if (tx->IsCoinStake())
            return TxType::TX_COINSTAKE;

        return ConvertOpReturnToType(ParseAsmType(tx, vasm));
    }

    TxType TransactionHelper::ParseType(const CTransactionRef& tx)
    {
        vector<string> vasm;
        return ParseType(tx, vasm);
    }

    string TransactionHelper::ConvertToReindexerTable(const Transaction& transaction)
    {
        // TODO (brangr) (v0.21.0): need remove for next generation serialization
        switch (*transaction.GetType())
        {
            case TxType::ACCOUNT_SETTING:
                return "AccountSettings";
            case TxType::ACCOUNT_USER:
                return "Users";
            case TxType::CONTENT_POST:
            case TxType::CONTENT_VIDEO:
            case TxType::CONTENT_ARTICLE:
            case TxType::CONTENT_DELETE:
                return "Posts";
            case TxType::CONTENT_COMMENT:
            case TxType::CONTENT_COMMENT_EDIT:
            case TxType::CONTENT_COMMENT_DELETE:
                return "Comment";
            case TxType::ACTION_SCORE_CONTENT:
                return "Scores";
            case TxType::ACTION_SCORE_COMMENT:
                return "CommentScores";
            case TxType::ACTION_COMPLAIN:
                return "Complains";
            case TxType::ACTION_BLOCKING_CANCEL:
            case TxType::ACTION_BLOCKING:
                return "Blocking";
            case TxType::ACTION_SUBSCRIBE_CANCEL:
            case TxType::ACTION_SUBSCRIBE_PRIVATE:
            case TxType::ACTION_SUBSCRIBE:
                return "Subscribes";
            case TxType::BOOST_CONTENT:
                return "Boosts";
            default:
                return "";
        }
    }

    string TransactionHelper::ExtractOpReturnHash(const CTransactionRef& tx)
    {
        vector<string> vasm;
        ParseAsmType(tx, vasm);
        
        if (vasm.size() < 3)
            return "";

        return vasm[2];
    }

    tuple<bool, string> TransactionHelper::ExtractOpReturnPayload(const CTransactionRef& tx)
    {
        vector<string> vasm;
        ParseAsmType(tx, vasm);

        if (vasm.size() < 4)
            return { false, "" };

        return { !vasm[3].empty(), vasm[3] };
    }

    bool TransactionHelper::IsPocketSupportedTransaction(const CTransactionRef& tx, TxType& txType)
    {
        txType = ParseType(tx);
        return txType != NOT_SUPPORTED;
    }

    bool TransactionHelper::IsPocketSupportedTransaction(const CTransactionRef& tx)
    {
        TxType txType = NOT_SUPPORTED;
        return IsPocketSupportedTransaction(tx, txType);
    }

    bool TransactionHelper::IsPocketSupportedTransaction(const CTransaction& tx)
    {
        auto txRef = MakeTransactionRef(tx);
        return IsPocketSupportedTransaction(txRef);
    }

    bool TransactionHelper::IsPocketTransaction(TxType& txType)
    {
        return txType != NOT_SUPPORTED &&
               txType != TX_COINBASE &&
               txType != TX_COINSTAKE &&
               txType != TX_DEFAULT;
    }

    bool TransactionHelper::IsPocketTransaction(const CTransactionRef& tx, TxType& txType)
    {
        txType = ParseType(tx);
        return IsPocketTransaction(txType);
    }

    bool TransactionHelper::IsPocketTransaction(const CTransactionRef& tx)
    {
        TxType txType = NOT_SUPPORTED;
        return IsPocketTransaction(tx, txType);
    }

    bool TransactionHelper::IsPocketTransaction(const CTransaction& tx)
    {
        auto txRef = MakeTransactionRef(tx);
        return IsPocketTransaction(txRef);
    }

    tuple<bool, shared_ptr<ScoreDataDto>> TransactionHelper::ParseScore(const CTransactionRef& tx)
    {
        shared_ptr<ScoreDataDto> scoreData = make_shared<ScoreDataDto>();

        vector<string> vasm;
        scoreData->ScoreType = ParseType(tx, vasm);

        if (scoreData->ScoreType != TxType::ACTION_SCORE_CONTENT &&
            scoreData->ScoreType != TxType::ACTION_SCORE_COMMENT)
            return make_tuple(false, scoreData);

        if (vasm.size() >= 4)
        {
            vector<unsigned char> _data_hex = ParseHex(vasm[3]);
            string _data_str(_data_hex.begin(), _data_hex.end());
            vector<string> _data;
            boost::split(_data, _data_str, boost::is_any_of("\t "));

            if (_data.size() >= 2)
            {
                if (auto[ok, addr] = GetPocketAuthorAddress(tx); ok)
                    scoreData->ScoreAddressHash = addr;

                scoreData->ContentAddressHash = _data[0];
                scoreData->ScoreValue = std::stoi(_data[1]);
            }
        }

        bool finalCheck = !scoreData->ScoreAddressHash.empty() &&
                          !scoreData->ContentAddressHash.empty();

        scoreData->ScoreTxHash = tx->GetHash().GetHex();
        return make_tuple(finalCheck, scoreData);
    }

    PTransactionRef TransactionHelper::CreateInstance(TxType txType, const CTransactionRef& tx)
    {
        PTransactionRef ptx = nullptr;
        switch (txType)
        {
            case TX_COINBASE:
                ptx = make_shared<Coinbase>(tx);
                break;
            case TX_COINSTAKE:
                ptx = make_shared<Coinstake>(tx);
                break;
            case TX_DEFAULT:
                ptx = make_shared<Default>(tx);
                break;
            case ACCOUNT_SETTING:
                ptx = make_shared<AccountSetting>(tx);
                break;
            case ACCOUNT_USER:
                ptx = make_shared<User>(tx);
                break;
            case CONTENT_POST:
                ptx = make_shared<Post>(tx);
                break;
            case CONTENT_VIDEO:
                ptx = make_shared<Video>(tx);
                break;
            case CONTENT_ARTICLE:
                ptx = make_shared<Article>(tx);
                break;
            case CONTENT_DELETE:
                ptx = make_shared<ContentDelete>(tx);
                break;
            case BOOST_CONTENT:
                ptx = make_shared<BoostContent>(tx);
                break;
            case CONTENT_COMMENT:
                ptx = make_shared<Comment>(tx);
                break;
            case CONTENT_COMMENT_EDIT:
                ptx = make_shared<CommentEdit>(tx);
                break;
            case CONTENT_COMMENT_DELETE:
                ptx = make_shared<CommentDelete>(tx);
                break;
            case ACTION_SCORE_CONTENT:
                ptx = make_shared<ScoreContent>(tx);
                break;
            case ACTION_SCORE_COMMENT:
                ptx = make_shared<ScoreComment>(tx);
                break;
            case ACTION_SUBSCRIBE:
                ptx = make_shared<Subscribe>(tx);
                break;
            case ACTION_SUBSCRIBE_PRIVATE:
                ptx = make_shared<SubscribePrivate>(tx);
                break;
            case ACTION_SUBSCRIBE_CANCEL:
                ptx = make_shared<SubscribeCancel>(tx);
                break;
            case ACTION_BLOCKING:
                ptx = make_shared<Blocking>(tx);
                break;
            case ACTION_BLOCKING_CANCEL:
                ptx = make_shared<BlockingCancel>(tx);
                break;
            case ACTION_COMPLAIN:
                ptx = make_shared<Complain>(tx);
                break;
            default:
                return nullptr;
        }

        return ptx;
    }

    PTransactionRef TransactionHelper::CreateInstance(TxType txType)
    {
        PTransactionRef ptx = nullptr;
        switch (txType)
        {
            case TX_COINBASE:
                ptx = make_shared<Coinbase>();
                break;
            case TX_COINSTAKE:
                ptx = make_shared<Coinstake>();
                break;
            case TX_DEFAULT:
                ptx = make_shared<Default>();
                break;
            case ACCOUNT_SETTING:
                ptx = make_shared<AccountSetting>();
                break;
            case ACCOUNT_USER:
                ptx = make_shared<User>();
                break;
            case CONTENT_POST:
                ptx = make_shared<Post>();
                break;
            case CONTENT_VIDEO:
                ptx = make_shared<Video>();
                break;
            case CONTENT_ARTICLE:
                ptx = make_shared<Article>();
                break;
            case CONTENT_DELETE:
                ptx = make_shared<ContentDelete>();
                break;
            case BOOST_CONTENT:
                ptx = make_shared<BoostContent>();
                break;
            case CONTENT_COMMENT:
                ptx = make_shared<Comment>();
                break;
            case CONTENT_COMMENT_EDIT:
                ptx = make_shared<CommentEdit>();
                break;
            case CONTENT_COMMENT_DELETE:
                ptx = make_shared<CommentDelete>();
                break;
            case ACTION_SCORE_CONTENT:
                ptx = make_shared<ScoreContent>();
                break;
            case ACTION_SCORE_COMMENT:
                ptx = make_shared<ScoreComment>();
                break;
            case ACTION_SUBSCRIBE:
                ptx = make_shared<Subscribe>();
                break;
            case ACTION_SUBSCRIBE_PRIVATE:
                ptx = make_shared<SubscribePrivate>();
                break;
            case ACTION_SUBSCRIBE_CANCEL:
                ptx = make_shared<SubscribeCancel>();
                break;
            case ACTION_BLOCKING:
                ptx = make_shared<Blocking>();
                break;
            case ACTION_BLOCKING_CANCEL:
                ptx = make_shared<BlockingCancel>();
                break;
            case ACTION_COMPLAIN:
                ptx = make_shared<Complain>();
                break;
            default:
                return nullptr;
        }

        return ptx;
    }

    bool TransactionHelper::IsIn(TxType txType, const vector<TxType>& inTypes)
    {
        for (auto inType : inTypes)
            if (inType == txType)
                return true;

        return false;
    }

    string TransactionHelper::TxStringType(TxType type)
    {
        switch (type)
        {
            case PocketTx::CONTENT_DELETE:
                return "contentDelete";
            case PocketTx::CONTENT_POST:
                return "share";
            case PocketTx::CONTENT_VIDEO:
                return "video";
            case PocketTx::CONTENT_ARTICLE:
                return "article";
            case PocketTx::ACCOUNT_SETTING:
                return "accSet";
            case PocketTx::ACTION_SCORE_CONTENT:
                return "upvoteShare";
            case PocketTx::ACTION_SUBSCRIBE:
                return "subscribe";
            case PocketTx::ACTION_SUBSCRIBE_PRIVATE:
                return "subscribePrivate";
            case PocketTx::ACTION_SUBSCRIBE_CANCEL:
                return "unsubscribe";
            case PocketTx::ACCOUNT_USER:
                return "userInfo";
            case PocketTx::CONTENT_COMMENT:
                return "comment";
            case PocketTx::CONTENT_COMMENT_EDIT:
                return "commentEdit";
            case PocketTx::CONTENT_COMMENT_DELETE:
                return "commentDelete";
            case PocketTx::ACTION_SCORE_COMMENT:
                return "cScore";
            case PocketTx::BOOST_CONTENT:
                return "contentBoost";
            default:
                return "";
        }
    }

    TxType TransactionHelper::TxIntType(const string& type)
    {
        if (type == "contentDelete" || type == OR_CONTENT_DELETE) return TxType::CONTENT_DELETE;
        else if (type == "share" || type == "shareEdit" || type == OR_POST || type == OR_POSTEDIT) return TxType::CONTENT_POST;
        else if (type == "video" || type == OR_VIDEO) return TxType::CONTENT_VIDEO;
        else if (type == "article" || type == OR_ARTICLE) return TxType::CONTENT_ARTICLE;
        else if (type == "accSet" || type == OR_ACCOUNT_SETTING) return TxType::ACCOUNT_SETTING;
        else if (type == "upvoteShare" || type == OR_SCORE) return TxType::ACTION_SCORE_CONTENT;
        else if (type == "subscribe" || type == OR_SUBSCRIBE) return TxType::ACTION_SUBSCRIBE;
        else if (type == "subscribePrivate" || type == OR_SUBSCRIBEPRIVATE) return TxType::ACTION_SUBSCRIBE_PRIVATE;
        else if (type == "unsubscribe" || type == OR_UNSUBSCRIBE) return TxType::ACTION_SUBSCRIBE_CANCEL;
        else if (type == "userInfo" || type == OR_USERINFO) return TxType::ACCOUNT_USER;
        else if (type == "comment" || type == OR_COMMENT) return TxType::CONTENT_COMMENT;
        else if (type == "commentEdit" || type == OR_COMMENT_EDIT) return TxType::CONTENT_COMMENT_EDIT;
        else if (type == "commentDelete" || type == OR_COMMENT_DELETE) return TxType::CONTENT_COMMENT_DELETE;
        else if (type == "cScore" || type == OR_COMMENT_SCORE) return TxType::ACTION_SCORE_COMMENT;
        else if (type == "contentBoost" || type == OR_CONTENT_BOOST) return TxType::BOOST_CONTENT;
        else return TxType::NOT_SUPPORTED;
    }
}