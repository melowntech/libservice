#ifndef shared_service_service_hpp_included_
#define shared_service_service_hpp_included_

#include <memory>

#include "utility/runnable.hpp"

#include "program.hpp"

namespace service {

namespace detail {
    struct SignalHandler;
} // namespace detail

class Service : protected Program, public utility::Runnable {
public:
    Service(const std::string &name, const std::string &version
            , int flags = 0x0);

    ~Service();

    int operator()(int argc, char *argv[]);

    bool isRunning();

    void stop();

    /** Show service statistics. Called on SIGUSR1.
     */
    virtual void stat() {}

    /** Adds/removes this process to list of processes that are mark global
     *  terminate flag on terminate signal. All other processes handle terminate
     *  signal locally.
     */
    void globalTerminate(bool value = true, long pid = 0);

    struct Config {
        std::string username;
        std::string groupname;

        Config() {}

        void configuration(po::options_description &cmdline
                           , po::options_description &config);

        void configure(const po::variables_map &vars);
    };

protected:
    friend class detail::SignalHandler;

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

    std::unique_ptr<detail::SignalHandler> signalHandler_;
};

} // namespace service

#endif // shared_service_service_hpp_included_
