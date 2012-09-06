#ifndef shared_service_service_hpp_included_
#define shared_service_service_hpp_included_

#include <string>

#include <boost/program_options.hpp>
#include <boost/noncopyable.hpp>
#include <boost/scoped_ptr.hpp>

#include <dbglog/dbglog.hpp>

namespace service {

namespace po = boost::program_options;

struct immediate_exit {
    immediate_exit(int code) : code(code) {}
    int code;
};

class service : boost::noncopyable {
public:
    service(const std::string &name, const std::string &version);

    ~service();

    int operator()(int argc, char *argv[]);

    const std::string name;
    const std::string version;

    bool isRunning();

protected:
    virtual void configuration(po::options_description &cmdline
                               , po::options_description &config) = 0;

    virtual void configure(const po::variables_map &vars) = 0;

    virtual void start() = 0;

    virtual int run() = 0;

    dbglog::module log_;

private:
    void configure(int argc, char *argv[]);

    bool daemonize_;

    struct SignalHandler;

    boost::scoped_ptr<SignalHandler> signalHandler_;
};

} // namespace service

#endif // shared_service_service_hpp_included_
