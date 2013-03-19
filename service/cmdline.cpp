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

} // namespace cmdline
