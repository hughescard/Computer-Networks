#include "cli.hpp"
#include <vector>
#include <string>

using namespace std;

int main(int argc, char **argv)
{
    vector<string> args;
    args.reserve(argc > 1 ? argc - 1 : 0);
    for (int i = 1; i < argc; ++i)
        args.emplace_back(argv[i]);
    return linkchat::run_cli(args);
}
