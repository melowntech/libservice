#include <cstdint>
#include <memory>
#include <atomic>

#include <boost/asio.hpp>
#include <boost/interprocess/anonymous_shared_memory.hpp>

#include "service.hpp"

namespace asio = boost::asio;
namespace bi = boost::interprocess;

namespace service {

runable::~runable()
{
}

namespace {
    inline void* getAddress(bi::mapped_region &mem, std::size_t offset = 0)
    {
        return static_cast<char*>(mem.get_address()) + offset;
    }
}

struct service::SignalHandler : boost::noncopyable {
public:
    SignalHandler(dbglog::module &log, const program &owner)
        : signals_(ios_, SIGTERM, SIGINT, SIGHUP)
        , mem_(bi::anonymous_shared_memory(1024))
        , terminated_(* new (getAddress(mem_)) std::atomic_bool(false))
          // TODO: what about alignment?
        , logRotateEvent_(* new (getAddress(mem_, sizeof(std::atomic_bool)))
                          std::atomic<std::uint64_t>(0))
        , lastLogRotateEvent_(0)
        , log_(log), owner_(owner)
    {
    }

    struct ScopedHandler {
        ScopedHandler(SignalHandler &h) : h(h) { h.start(); }
        ~ScopedHandler() { h.stop(); }

        SignalHandler &h;
    };

    void terminate() { terminated_ = true; }

    /** Processes events and returns whether we should terminate.
     */
    bool process() {
        ios_.poll();

        // TODO: last event should be (process-local) atomic to handle multiple
        // threads calling this function
        auto value(logRotateEvent_.load());
        if (value != lastLogRotateEvent_) {
            LOG(info3) << "Logrotate: <" << owner_.logFile() << ">.";
            dbglog::log_file(owner_.logFile());
            LOG(info4, log_)
                << "Service " << owner_.name << '-' << owner_.version
                << ": log rotated.";
            lastLogRotateEvent_ = value;
        }

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

        LOG(debug, log_)
            << "SignalHandler received signal: <" << signo << ">.";
        switch (signo) {
        case SIGTERM:
        case SIGINT:
            LOG(info2, log_) << "Terminate signal: <" << signo << ">.";
            terminated_ = true;
            break;

        case SIGHUP:
            // mark logrotate
            ++logRotateEvent_;
            break;
        }
        start();
    }

    asio::io_service ios_;
    asio::signal_set signals_;
    bi::mapped_region mem_;
    std::atomic_bool &terminated_;
    std::atomic<std::uint64_t> &logRotateEvent_;
    std::uint64_t lastLogRotateEvent_;
    dbglog::module &log_;
    const program &owner_;
};

service::service(const std::string &name, const std::string &version
                 , int flags)
    : program(name, version, flags)
    , daemonize_(false)
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

    signalHandler_.reset(new SignalHandler(log_, *this));

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

        try {
            start();
        } catch (const immediate_exit &e) {
            return e.code;
        }

        if (!isRunning()) {
            LOG(info4, log_) << "Terminated during startup.";
            return EXIT_FAILURE;
        }

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

void service::stop()
{
    signalHandler_->terminate();
}

bool service::isRunning() {
    return !signalHandler_->process();
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
