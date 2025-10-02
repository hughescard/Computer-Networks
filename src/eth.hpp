#pragma once
#include <optional>
#include <string>


namespace linkchat
{
    struct NetIf
    {
        string name;
    };
    std:: optional<NetIf> pick_default_iface();
}
