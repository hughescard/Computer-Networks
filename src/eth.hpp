#pragma once
#include <optional>
#include <string>

using namespace std;

namespace linkchat
{
    struct NetIf
    {
        string name;
    };
    optional<NetIf> pick_default_iface(); // D2
}
