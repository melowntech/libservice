#ifndef shared_service_program_hpp_included_
#define shared_service_program_hpp_included_

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

class program : boost::noncopyable {
public:
    ~program();

    const std::string name;
    const std::string version;

protected:
    program(const std::string &name, const std::string &version);

    virtual void configuration(po::options_description &cmdline
                               , po::options_description &config) = 0;

    virtual void configure(const po::variables_map &vars) = 0;

    dbglog::module log_;

    void configure(int argc, char *argv[]
                   , const po::options_description &genericConfig);

private:
    void configureImpl(int argc, char *argv[]
                       , po::options_description genericConfig);
};

} // namespace service

#endif // shared_service_program_hpp_included_
