#include "mac.hpp"
#include <array>
#include <cctype>
#include <cstdio>
#include <set>
using namespace std;
namespace linkchat
{
    bool operator==(const Mac &a, const Mac &b) noexcept
    {
        for (size_t i = 0; i < kMacSize; i++)
        {
            if (a.bytes[i] != b.bytes[i])
                return false;
        }
        return true;
    }

    bool operator!=(const Mac &a, const Mac &b) noexcept
    {
        return !operator==(a, b);
    }

    bool is_hexVal(char c) noexcept
    {
        set<char> hexVal;
        hexVal.insert('0');
        hexVal.insert('1');
        hexVal.insert('2');
        hexVal.insert('3');
        hexVal.insert('4');
        hexVal.insert('5');
        hexVal.insert('6');
        hexVal.insert('7');
        hexVal.insert('8');
        hexVal.insert('9');
        hexVal.insert('A');
        hexVal.insert('B');
        hexVal.insert('C');
        hexVal.insert('D');
        hexVal.insert('E');
        hexVal.insert('F');
        hexVal.insert('a');
        hexVal.insert('b');
        hexVal.insert('c');
        hexVal.insert('d');
        hexVal.insert('e');
        hexVal.insert('f');
        return hexVal.find(c) != hexVal.end();
    }

    static int from_hex(char c) noexcept
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        c |= 0x20; 
        if (c >= 'a' && c <= 'f')
            return 10 + (c - 'a');
        return -1;
    }

    string bytes_to_hex(const uint8_t byte) noexcept
    {
        char buf[3];
        // lowercase, two hex digits
        static const char *hex = "0123456789abcdef";
        buf[0] = hex[(byte >> 4) & 0xF];
        buf[1] = hex[byte & 0xF];
        buf[2] = '\0';
        return string(buf);
    }
    bool is_broadcast(const Mac &mac) noexcept
    {
        for (auto i = 0; i < kMacSize; i++)
        {
            if (mac.bytes[i] != 0xFF)
                return false;
        }
        return true;
    }

    bool is_zero(const Mac &mac) noexcept
    {
        for (auto i = 0; i < kMacSize; i++)
        {
            if (mac.bytes[i] != 0x00)
                return false;
        }
        return true;
    }

    bool parse_mac(const string &text, Mac &out) noexcept
    {
        if (text.size() != 17)
            return false;
        // check colon positions: 2,5,8,11,14
        for (size_t i = 2; i < text.size(); i += 3)
        {
            if (text[i] != ':')
                return false;
        }

        for (int byte = 0; byte < static_cast<int>(kMacSize); ++byte)
        {
            int hi = from_hex(text[byte * 3 + 0]);
            int lo = from_hex(text[byte * 3 + 1]);
            if (hi < 0 || lo < 0)
                return false;
            out.bytes[byte] = static_cast<uint8_t>((hi << 4) | lo);
        }
        return true;
    }

    string mac_to_string(const Mac &mac) noexcept
    {
        string result;
        for (int i = 0; i < static_cast<int>(kMacSize); ++i)
        {
            if (i != 0)
                result += ':';
            result += bytes_to_hex(mac.bytes[i]);
        }
        return result;
    }

}