//--------------------------------------------------------------------------
// Copyright (C) 2014-2016 Cisco and/or its affiliates. All rights reserved.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License Version 2 as published
// by the Free Software Foundation.  You may not use, modify or distribute
// this program under any other version of the GNU General Public License.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//--------------------------------------------------------------------------

// udp_module.h author Russ Combs <rucombs@cisco.com>

#ifndef UDP_MODULE_H
#define UDP_MODULE_H

#include <string>
#include <vector>

#include "main/snort_types.h"
#include "main/thread.h"
#include "flow/session.h"
#include "framework/module.h"

struct SnortConfig;

struct UdpStats
{
    SESSION_STATS;
};

extern const PegInfo udp_pegs[];
extern THREAD_LOCAL struct UdpStats udpStats;
extern THREAD_LOCAL ProfileStats udp_perf_stats;

//-------------------------------------------------------------------------
// stream_udp module
//-------------------------------------------------------------------------

#define MOD_NAME "stream_udp"
#define MOD_HELP "stream inspector for UDP flow tracking"

struct StreamUdpConfig;

class StreamUdpModule : public Module
{
public:
    StreamUdpModule();
    bool set(const char*, Value&, SnortConfig*) override;
    bool begin(const char*, int, SnortConfig*) override;
    bool end(const char*, int, SnortConfig*) override;

    ProfileStats* get_profile(unsigned, const char*&, const char*&) const override;
    const PegInfo* get_pegs() const override;
    PegCount* get_counts() const override;
    StreamUdpConfig* get_data();

    void sum_stats() override;

private:
    StreamUdpConfig* config;
};

#endif

