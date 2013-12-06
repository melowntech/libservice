#include "cmdline.hpp"

namespace service {

Cmdline::Cmdline(const std::string &name, const std::string &version
                 , int flags)
    : Program(name, version, flags)
{}

Cmdline::~Cmdline()
{}

int Cmdline::operator()(int argc, char *argv[])
{
    dbglog::thread_id("main");

    try {
        Program::configure
            (argc, argv, po::options_description
             ("configuration file options (all options can be overridden "
              "on command line)"));
    } catch (const immediate_exit &e) {
        return e.code;
    }

    int code = run();

    if (code && !noExcessiveLogging()) {
        LOG(err4, log_) << "Terminated with error " << code << '.';
    }

    return code;
}

void Cmdline::configure(const std::vector<std::string> &)
{
    throw po::error
        ("Program asked to collect unrecognized options "
         "although it is not processing them. Go fix your program.");
}

inline bool Cmdline::help(std::ostream &, const std::string &) const
{
    return false;
}

} // namespace cmdline
