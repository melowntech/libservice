#ifndef shared_service_program_hpp_included_
#define shared_service_program_hpp_included_

#include <string>
#include <ostream>

#include <boost/program_options.hpp>
#include <boost/noncopyable.hpp>

#include <dbglog/dbglog.hpp>

namespace service {

namespace po = boost::program_options;

struct immediate_exit {
    immediate_exit(int code) : code(code) {}
    int code;
};

constexpr int DISABLE_CONFIG_HELP = 0x01;

class program : boost::noncopyable {
public:
    virtual ~program();

    const std::string name;
    const std::string version;

    int flags() const { return flags_; }

protected:
    program(const std::string &name, const std::string &version, int flags);

    virtual void configuration(po::options_description &cmdline
                               , po::options_description &config) = 0;

    virtual void configure(const po::variables_map &vars) = 0;

    /** Produces help for *what*.
     *  Returns false if help for *what* is not supported.
     *
     *  You should register --help-what in cmdline configuration to inform
     *  user about such help availability.
     */
    virtual bool help(std::ostream &out, const std::string &what)
        const { (void) out; (void) what; return false; }

    dbglog::module log_;

    void configure(int argc, char *argv[]
                   , const po::options_description &genericConfig);

private:
    void configureImpl(int argc, char *argv[]
                       , po::options_description genericConfig);

    int flags_;
};

} // namespace service

#endif // shared_service_program_hpp_included_
