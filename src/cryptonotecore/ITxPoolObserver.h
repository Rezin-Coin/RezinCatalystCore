// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

#pragma once

namespace CryptoNote
{
    class ITxPoolObserver
    {
      public:
        virtual ~ITxPoolObserver() {}

        virtual void txDeletedFromPool() = 0;
    };
} // namespace CryptoNote
