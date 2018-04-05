/**
 * Copyright (c) 2017 Melown Technologies SE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * *  Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <atomic>

#include <boost/asio.hpp>

#include "dbglog/dbglog.hpp"

#include "./runninguntilsignalled.hpp"

namespace service {

namespace asio = boost::asio;

const char* signalName(int signo)
{
#ifndef WIN32
    return ::strsignal(signo);
#else
    switch (signo) {
    case SIGABRT: return "SIGABRT";
    case SIGFPE: return "SIGFPE";
    case SIGILL: return "SIGILL";
    case SIGINT: return "SIGINT";
    case SIGSEGV: return "SIGSEGV";
    case SIGTERM: return "SIGTERM";
    }
    return "unknown";
#endif
}

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

    auto signame(signalName(signo));

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
