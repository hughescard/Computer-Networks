#include "cli.hpp"
#include <iostream>

using namespace std;

namespace linkchat
{
    static void print_help()
    {
        cout << "Link-Chat (MVP CLI)\n"
                     "  recv | send <MAC> <text> | sendfile <MAC> <path>\n"
                     "  scan | broadcast <msg> | help\n";
    }
    
    int run_cli(const vector<string> &args)
    {
        if (args.empty() || args[0] == "help")
        {
            print_help();
            return 0;
        }
        cout << "[stub] command: " << args[0] << "\n";
        return 0;
    }
}
