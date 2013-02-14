#ifndef shared_service_cmdline_hpp_included_
#define shared_service_cmdline_hpp_included_

#include "program.hpp"

namespace service {

class cmdline : protected program {
public:
    cmdline(const std::string &name, const std::string &version);

    ~cmdline();

    int operator()(int argc, char *argv[]);

protected:
    virtual void configuration(po::options_description &cmdline
                               , po::options_description &config) = 0;

    virtual void configure(const po::variables_map &vars) = 0;

    virtual int run() = 0;

    virtual bool help(std::ostream &out, const std::string &what)
        const { return program::help(out, what); }
};

} // namespace service

#endif // shared_service_cmdline_hpp_included_
