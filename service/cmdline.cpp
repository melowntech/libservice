#include "cmdline.hpp"

namespace service {

cmdline::cmdline(const std::string &name, const std::string &version
                 , int flags)
    : program(name, version, flags)
{}

cmdline::~cmdline()
{}

int cmdline::operator()(int argc, char *argv[])
{
    dbglog::thread_id("main");

    try {
        program::configure
            (argc, argv, po::options_description
             ("configuration file options (all options can be overridden "
              "on command line)"));
    } catch (const immediate_exit &e) {
        return e.code;
    }

    int code = run();

    if (code) {
        LOG(err4, log_) << "Terminated with error " << code << '.';
    }

    return code;
}

void cmdline::configure(const std::vector<std::string> &)
{
    throw po::error
        ("Program asked to collect unrecognized options "
         "although it is not processing them. Go fix your program.");
}

inline bool cmdline::help(std::ostream &, const std::string &)
{
    return false;
}

} // namespace cmdline
