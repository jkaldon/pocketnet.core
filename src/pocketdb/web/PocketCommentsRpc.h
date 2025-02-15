// Copyright (c) 2018-2022 The Pocketnet developers
// Distributed under the Apache 2.0 software license, see the accompanying
// https://www.apache.org/licenses/LICENSE-2.0

#ifndef SRC_POCKETCOMMENTSRPC_H
#define SRC_POCKETCOMMENTSRPC_H

#include "rpc/server.h"
#include "logging.h"
#include "validation.h"

namespace PocketWeb::PocketWebRpc
{
    using namespace std;
    
    UniValue GetCommentsByPost(const JSONRPCRequest& request);
    UniValue GetLastComments(const JSONRPCRequest& request);
} // namespace PocketWeb


#endif //SRC_POCKETCOMMENTSRPC_H
