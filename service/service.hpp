#ifndef shared_service_service_hpp_included_
#define shared_service_service_hpp_included_

#include <memory>

#include "program.hpp"
#include "runable.hpp"

namespace service {

class service : protected program, public runable {
public:
    service(const std::string &name, const std::string &version
            , int flags = 0x0);

    ~service();

    int operator()(int argc, char *argv[]);

    bool isRunning();

    void stop();

protected:
    typedef std::shared_ptr<void> Cleanup;

    virtual void configuration(po::options_description &cmdline
                               , po::options_description &config
                               , po::positional_options_description &pd) = 0;

    virtual void configure(const po::variables_map &vars) = 0;

    virtual void configure(const std::vector<std::string> &unrecognized);

    /** Returned Cleanup will be destroyed when going down. Just return pointer
     *  to your cleanup code
     */
    virtual Cleanup start() = 0;

    virtual int run() = 0;

    virtual bool help(std::ostream &out, const std::string &what);

    virtual void preConfigHook(const po::variables_map &vars);

    /** Code that will be run under original persona before persona is switched.
     */
    virtual void prePersonaSwitch() {}

    /** Code that will be run under new persona after persona is switched.
     */
    virtual void postPersonaSwitch() {}

private:
    bool daemonize_;

    struct SignalHandler;
    std::unique_ptr<SignalHandler> signalHandler_;
};

} // namespace service

#endif // shared_service_service_hpp_included_
