#pragma once
#include <optional>
#include <string>


namespace linkchat
{
    struct NetIf
    {
        std::string name;
    };
    std:: optional<NetIf> pick_default_iface();
}
