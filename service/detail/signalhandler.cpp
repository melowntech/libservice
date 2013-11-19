#include "signalhandler.hpp"

namespace service { namespace detail {

SignalHandler::SignalHandler(dbglog::module &log, Service &owner
                             , pid_t mainPid)
    : signals_(ios_, SIGTERM, SIGINT, SIGHUP)
    , mem_(4096)
    , terminator_(mem_, 32)
    , terminated_(* new (mem_.get<std::atomic_bool>())
                  std::atomic_bool(false))
    , thisTerminated_(false)
    , logRotateEvent_(* new (mem_.get<std::atomic<std::uint64_t> >())
                      std::atomic<std::uint64_t>(0))
    , lastLogRotateEvent_(0)
    , statEvent_(* new (mem_.get<std::atomic<std::uint64_t> >())
                 std::atomic<std::uint64_t>(0))
    , lastStatEvent_(0)
    , log_(log), owner_(owner), mainPid_(mainPid)
{
    signals_.add(SIGUSR1);
}

void SignalHandler::terminate()
{
    terminated_ = true;
}

/** Processes events and returns whether we should terminate.
 */
bool SignalHandler::process()
{
    ios_.poll();

    // TODO: last event should be (process-local) atomic to handle multiple
    // threads calling this function

    // check for logrotate request
    {
        auto value(logRotateEvent_.load());
        if (value != lastLogRotateEvent_) {
            LOG(info3, log_) << "Logrotate: <" << owner_.logFile() << ">.";
            dbglog::log_file(owner_.logFile().string());
            LOG(info4, log_)
                << "Service " << owner_.name << '-' << owner_.version
                << ": log rotated.";
            lastLogRotateEvent_ = value;
        }
    }

    // check for statistics request
    {
        auto value(statEvent_.load());
        if ((value != lastStatEvent_) && (::getpid() == mainPid_)) {
            // statistics are processed only in main process
            owner_.stat();
            lastStatEvent_ = value;
        }
    }

    return terminated_ || thisTerminated_;
}

void SignalHandler::globalTerminate(bool value, ::pid_t pid)
{
    if (value) {
        terminator_.add(pid);
    } else {
        terminator_.remove(pid);
    }
}

void SignalHandler::start()
{
    signals_.async_wait(boost::bind(&SignalHandler::signal, this
                                    , asio::placeholders::error
                                    , asio::placeholders::signal_number));
}

void SignalHandler::stop()
{
    signals_.cancel();
}

void SignalHandler::signal(const boost::system::error_code &e, int signo)
{
    if (e) {
        if (boost::asio::error::operation_aborted == e) {
            return;
        }
            start();
    }

    // ignore signal '0'
    if (!signo) {
        start();
        return;
    }

    auto signame(::strsignal(signo));

    LOG(debug, log_)
        << "SignalHandler received signal: <" << signo
        << ", " << signame << ">.";
    switch (signo) {
    case SIGTERM:
    case SIGINT:
        LOG(info2, log_)
            << "Terminate signal: <" << signo << ", " << signame << ">.";
        markTerminated();
        break;

    case SIGHUP:
        // mark logrotate
        ++logRotateEvent_;
        break;

    case SIGUSR1:
        // mark statistics request
        ++statEvent_;
        break;
    }
    start();
}

void SignalHandler::markTerminated()
{
    thisTerminated_ = true;

    // check for global termination
    if (terminator_.find()) {
        LOG(info1) << "Global terminate.";
        terminated_ = true;
    } else {
        LOG(info1) << "Local terminate.";
    }
}

} } // namespace service::detail
