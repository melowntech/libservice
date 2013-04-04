#include <boost/asio.hpp>

#include "service.hpp"

namespace asio = boost::asio;

namespace service {

struct service::SignalHandler : boost::noncopyable {
public:
    SignalHandler()
        : signals_(ios_, SIGTERM, SIGINT)
        , terminated_(false)
    {}

    struct ScopedHandler {
        ScopedHandler(SignalHandler &h) : h(h) { h.start(); }
        ~ScopedHandler() { h.stop(); }

        SignalHandler &h;
    };

    bool terminated() {
        ios_.poll();
        return terminated_;
    }

private:
    void start() {
        signals_.async_wait(boost::bind(&SignalHandler::signal, this
                                        , asio::placeholders::error
                                        , asio::placeholders::signal_number));
    }

    void stop() {
        signals_.cancel();
    }

    void signal(const boost::system::error_code &e, int signo) {
        if (e) {
            if (boost::asio::error::operation_aborted == e) {
                return;
            }
            start();
        }

        LOG(debug) << "SignalHandler received signal: <" << signo << ">.";
        switch (signo) {
        case SIGTERM:
        case SIGINT:
            LOG(info2) << "Terminate signal: <" << signo << ">.";
            terminated_ = true;
            break;
        }
        start();
    }

    asio::io_service ios_;
    asio::signal_set signals_;
    bool terminated_;
};

service::service(const std::string &name, const std::string &version
                 , int flags)
    : program(name, version, flags)
    , daemonize_(false)
    , signalHandler_(new SignalHandler)
{}

service::~service()
{}

int service::operator()(int argc, char *argv[])
{
    dbglog::thread_id("main");

    try {
        po::options_description genericConfig
            ("configuration file options (all options can be overridden "
             "on command line)");

        genericConfig.add_options()
            ("daemonize", po::value<>(&daemonize_)
             ->default_value(daemonize_)->implicit_value(true)
             , "run in daemon mode")
        ;

        program::configure(argc, argv, genericConfig);
    } catch (const immediate_exit &e) {
        return e.code;
    }

    LOG(info4, log_) << "Service " << name << '-' << version << " starting.";

    // daemonize if asked to do so
    if (daemonize_) {
        if (-1 == daemon(false, false)) {
            LOG(fatal) << "Failed to daemonize: " << errno;
            return EXIT_FAILURE;
        }
        dbglog::log_console(false);
        LOG(info4, log_) << "Running in background.";
    }

    int code = EXIT_SUCCESS;
    {
        SignalHandler::ScopedHandler signals(*signalHandler_);

        start();
        LOG(info4, log_) << "Started.";

        code = run();
    }

    if (code) {
        LOG(err4, log_) << "Terminated with error " << code << '.';
    } else {
        LOG(info4, log_) << "Terminated normally.";
    }

    return code;
}

bool service::isRunning() {
    return !signalHandler_->terminated();
}

void service::configure(const std::vector<std::string> &)
{
    throw po::error
        ("Program asked to collect unrecognized options "
         "although it is not processing them. Go fix your program.");
}

inline bool service::help(std::ostream &, const std::string &)
{
    return false;
}

} // namespace service
