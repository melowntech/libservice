#ifndef shared_service_service_hpp_included_
#define shared_service_service_hpp_included_

#include <string>
#include <boost/program_options.hpp>
#include <boost/noncopyable.hpp>

#include <dbglog/dbglog.hpp>

namespace service {

namespace po = boost::program_options;

struct immediate_exit {
    immediate_exit(int code) : code(code) {}
    int code;
};

class service : boost::noncopyable {
public:
    service(const std::string &name, const std::string &version)
        : name(name), version(version)
        , log_module_(dbglog::make_module(name))
        , daemonize_(false)
    {}

    int operator()(int argc, char *argv[]);

    const std::string name;
    const std::string version;

protected:
    virtual void configuration(po::options_description &cmdline
                               , po::options_description &config) = 0;

    virtual void configure(const po::variables_map &vars) = 0;

    virtual void start() = 0;

    virtual int run() = 0;

    dbglog::module log_module_;

private:
    void configure(int argc, char *argv[]);

    bool daemonize_;
};

} // namespace service

#endif // shared_service_service_hpp_included_
