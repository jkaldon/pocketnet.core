// Copyright (c) 2018-2022 The Pocketnet developers
// Distributed under the Apache 2.0 software license, see the accompanying
// https://www.apache.org/licenses/LICENSE-2.0

#ifndef POCKETDB_MODEL_WEB_CONTENT_H
#define POCKETDB_MODEL_WEB_CONTENT_H

#include <string>
#include "pocketdb/models/base/PocketTypes.h"

namespace PocketDbWeb
{
    using namespace std;
    using namespace PocketTx;

    struct WebContent
    {
        int64_t ContentId;
        ContentFieldType FieldType;
        string Value;

        WebContent(int64_t contentId, ContentFieldType fieldType, const string& value)
        {
            ContentId = contentId;
            FieldType = fieldType;
            Value = value;
        }
    };

} // PocketDbWeb

#endif //POCKETDB_MODEL_WEB_CONTENT_H
