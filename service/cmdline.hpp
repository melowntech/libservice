#ifndef shared_service_cmdline_hpp_included_
#define shared_service_cmdline_hpp_included_

#include "program.hpp"

namespace service {

class Cmdline : protected Program {
public:
    Cmdline(const std::string &name, const std::string &version
            , int flags = 0x0);

    ~Cmdline();

    int operator()(int argc, char *argv[]);

protected:
    virtual void configuration(po::options_description &cmdline
                               , po::options_description &config
                               , po::positional_options_description &pd) = 0;

    virtual void configure(const po::variables_map &vars) = 0;

    virtual void configure(const std::vector<std::string> &unrecognized);

    virtual int run() = 0;

    virtual bool help(std::ostream &out, const std::string &what) const;
};

} // namespace service

#endif // shared_service_cmdline_hpp_included_
