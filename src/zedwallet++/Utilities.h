// Copyright (c) 2018-2019, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include <walletbackend/WalletBackend.h>

namespace ZedUtilities
{
    void confirmPassword(const std::shared_ptr<WalletBackend> walletBackend, const std::string msg);

    uint64_t getScanHeight();

} // namespace ZedUtilities
