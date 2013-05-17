#ifndef shared_service_service_hpp_included_
#define shared_service_service_hpp_included_

#include <boost/scoped_ptr.hpp>

#include "program.hpp"

namespace service {

class service : protected program {
public:
    service(const std::string &name, const std::string &version
            , int flags = 0x0);

    ~service();

    int operator()(int argc, char *argv[]);

    bool isRunning();

    void stop();

protected:
    virtual void configuration(po::options_description &cmdline
                               , po::options_description &config
                               , po::positional_options_description &pd) = 0;

    virtual void configure(const po::variables_map &vars) = 0;

    virtual void configure(const std::vector<std::string> &unrecognized);

    virtual void start() = 0;

    virtual int run() = 0;

    virtual bool help(std::ostream &out, const std::string &what);

private:
    bool daemonize_;

    struct SignalHandler;

    boost::scoped_ptr<SignalHandler> signalHandler_;
};

} // namespace service

#endif // shared_service_service_hpp_included_
