#include <atomic>

#include <boost/asio.hpp>

#include "dbglog/dbglog.hpp"

#include "./runninguntilsignalled.hpp"

namespace service {

namespace asio = boost::asio;

struct RunningUntilSignalled::Detail : boost::noncopyable {
    Detail()
        : signals(ios, SIGINT)
        , terminated(false)
    {
        startSignals();
    }

    void signal(const boost::system::error_code &e, int signo);

    void startSignals();

    bool process();

    asio::io_service ios;
    asio::signal_set signals;
    std::atomic_bool terminated;
};

void RunningUntilSignalled::Detail::signal
   (const boost::system::error_code &e, int signo)
{
    if (e) {
        if (boost::asio::error::operation_aborted == e) {
            return;
        }
        startSignals();
    }

    // ignore signal '0'
    if (!signo) {
        startSignals();
        return;
    }

    auto signame(::strsignal(signo));

    LOG(debug)
        << "RunningUntilSignalled received signal: <" << signo
        << ", " << signame << ">.";
    switch (signo) {
    case SIGINT:
        LOG(info2)
            << "Terminate signal: <" << signo << ", " << signame << ">.";
        terminated = true;
        break;
    }
    startSignals();
}

void RunningUntilSignalled::Detail::startSignals()
{
    signals.async_wait(std::bind(&Detail::signal, this
                                 , std::placeholders::_1
                                 , std::placeholders::_2));
}

bool RunningUntilSignalled::Detail::process()
{
    // run the event loop until there is nothing to process
    ios.poll();
    return !terminated;
}

RunningUntilSignalled::RunningUntilSignalled()
    : detail_(new Detail)
{
}

RunningUntilSignalled::~RunningUntilSignalled()
{
}

bool RunningUntilSignalled::isRunning()
{
    return detail().process();
}

void RunningUntilSignalled::stop()
{
    detail().terminated = true;
}

} // namespace service
