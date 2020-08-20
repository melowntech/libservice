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
#include <pwd.h>
#include <grp.h>
#include <unistd.h>
#include <sys/stat.h>

#include <system_error>

#include <boost/filesystem.hpp>

#include "dbglog/dbglog.hpp"
#include "utility/parse.hpp"

#include "signalhandler.hpp"

namespace service { namespace detail {

::uid_t username2uid(const std::string &username)
{
    if (username.empty()) { return -1; }

    auto pwd(::getpwnam(username.c_str()));
    if (!pwd) {
        LOGTHROW(err3, std::runtime_error)
            << "There is no user <" << username
            << "> present on the system.";
    }

    return pwd->pw_uid;
}

::gid_t group2gid(const std::string &groupname)
{
    if (groupname.empty()) { return -1; }

    auto gr(::getgrnam(groupname.c_str()));
    if (!gr) {
        LOGTHROW(err3, std::runtime_error)
            << "There is no group <" << groupname
            << "> present on the system.";
    }

    return gr->gr_gid;
}

SignalHandler::SignalHandler(dbglog::module &log, Service &owner
                             , pid_t mainPid
                             , const boost::optional<CtrlConfig> &ctrlConfig)
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
    , ctrl_(ios_)
{
    if (ctrlConfig) {
        ctrlPath_ = ctrlConfig->path;
        local::stream_protocol::endpoint e(ctrlPath_->string());
        ctrl_.open(e.protocol());
        ctrl_.set_option(asio::socket_base::reuse_address(true));
        ctrl_.bind(e);
        ctrl_.listen();

        // change owner
        if (-1 == ::chown(ctrlConfig->path.c_str()
                          , username2uid(ctrlConfig->username)
                          , group2gid(ctrlConfig->group)))
        {
            std::system_error e(errno, std::system_category());
            LOG(err3)
                << "Canot change ownership of unix socket "
                << ctrlConfig->path << ": <"
                << e.code() << ", " << e.what() << ">.";
            throw e;
        }

        // update permissions
        if (ctrlConfig->mode) {
            if (-1 == ::chmod(ctrlConfig->path.c_str(), ctrlConfig->mode)) {
                std::system_error e(errno, std::system_category());
                LOG(err3)
                    << "Canot mode of unix socket "
                    << ctrlConfig->path << ": <"
                    << e.code() << ", " << e.what() << ">.";
                throw e;
            }
        }
    }

    signals_.add(SIGUSR1);

    utility::AtFork::add(this, std::bind(&SignalHandler::atFork, this
                                         , std::placeholders::_1));
}

SignalHandler::~SignalHandler()
{
    utility::AtFork::remove(this);

    // remove ctrl path if in main process
    if (ctrlPath_ && (::getpid() == mainPid_)) {
        // try to remove control socket, ignore failure
        boost::system::error_code ec;
        remove_all(*ctrlPath_, ec);
    }
}

void SignalHandler::terminate()
{
    terminated_ = true;
}

void SignalHandler::logRotate()
{
    ++logRotateEvent_;
}

/** Processes events and returns whether we should terminate.
 */
bool SignalHandler::process()
{
    // run the event loop until there is nothing to process
    ios_.poll();

    // TODO: last event should be (process-local) atomic to handle multiple
    // threads calling this function

    // check for logrotate request
    {
        auto value(logRotateEvent_.load());
        if (value != lastLogRotateEvent_) {
            owner_.logRotate();
            lastLogRotateEvent_ = value;
        }
    }

    // check for statistics request
    {
        auto value(statEvent_.load());
        if ((value != lastStatEvent_) && (::getpid() == mainPid_)) {
            // statistics are processed only in main process
            owner_.processStat();
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

void SignalHandler::startSignals()
{
    signals_.async_wait(lib::bind(&SignalHandler::signal, this
                                  , placeholders::_1
                                  , placeholders::_2));
}

void SignalHandler::start()
{
    startSignals();
    startAccept();
}

void SignalHandler::stop()
{
    signals_.cancel();

    stopAccept();
}

void SignalHandler::signal(const boost::system::error_code &e, int signo)
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

    default:
        // user-registered signal
        owner_.signal(signo);
        break;
    }
    startSignals();
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

class CtrlConnection
    : public lib::enable_shared_from_this<CtrlConnection>
    , boost::noncopyable
{
public:
    typedef lib::shared_ptr<CtrlConnection> pointer;

    CtrlConnection(Service &owner, asio::io_service &ios
                   , SignalHandler &sh, dbglog::module &log)
        : owner_(owner), socket_(ios), strand_(ios), sh_(sh), log_(log)
        , closed_(false)
    {}

    ~CtrlConnection() {}

    local::stream_protocol::socket& socket() { return socket_; }

    void startRead();

    void lineRead(const boost::system::error_code &e
                  , std::size_t bytes);

    void handleWrite(const boost::system::error_code &e
                     , std::size_t bytes);

private:
    Service &owner_;
    local::stream_protocol::socket socket_;
    asio::io_service::strand strand_;
    SignalHandler &sh_;
    dbglog::module &log_;

    boost::asio::streambuf input_;
    boost::asio::streambuf output_;

    bool closed_;
};

void SignalHandler::startAccept()
{
    if (!ctrl_.is_open()) { return; }

    auto con(lib::make_shared<CtrlConnection>(owner_, ios_, *this, log_));
    ctrl_.async_accept
        (con->socket()
         , lib::bind(&SignalHandler::newCtrlConnection, this
                       , placeholders::_1, con));
}

void SignalHandler::stopAccept()
{
    if (ctrl_.is_open()) {
        ctrl_.cancel();
        ctrl_.close();
    }
}

void SignalHandler::newCtrlConnection(const boost::system::error_code &e
                                      , CtrlConnection::pointer con)
{
    if (!e) {
        LOG(info2, log_) << "New control connection.";
        con->startRead();
    }

    if (e.value() != asio::error::operation_aborted) {
        startAccept();
    }
}

void CtrlConnection::startRead()
{
    LOG(debug, log_) << "starting read";

    asio::async_read_until
         (socket_, input_, "\n"
         , lib::bind(&CtrlConnection::lineRead
                     , shared_from_this()
                     , placeholders::_1
                     , placeholders::_2));
}

void CtrlConnection::lineRead(const boost::system::error_code &e
                              , std::size_t bytes)
{
    if (e) {
        if (e.value() != asio::error::eof) {
            LOG(err2, log_) << "Control connection error: " << e;
        } else {
            LOG(info2, log_) << "Control connection closed";
        }
        return;
    }

    LOG(debug, log_) << "Read: " << bytes << " bytes.";

    std::istream is(&input_);
    std::string line;
    std::getline(is, line);

    std::ostream os(&output_);

    auto cmdValue(utility::separated_values::split<std::vector<std::string> >
                  (line, " \t"));

    bool terminateBlock(true);

    if (cmdValue.empty()) {
        os << "empty command received\n";
    } else {
        auto &front(cmdValue.front());
        if (!front.empty() && (front[0] == '!')) {
            // close after command
            front = front.substr(1);
            closed_ = true;
            terminateBlock = false;
        }

        Service::CtrlCommand cmd{
            front
            , std::vector<std::string>(cmdValue.begin() + 1, cmdValue.end())
        };

        try {
            if (cmd.cmd == "logrotate") {
                sh_.logRotate();
                os << "log rotation scheduled\n";
            } else if (cmd.cmd == "terminate") {
                sh_.terminate();
                os << "termination scheduled, bye\n";
            } else if (cmd.cmd == "exit") {
                closed_ = true;
                terminateBlock = false;
            } else if (cmd.cmd == "help") {
                os << "logrotate      schedules log reopen event\n"
                   << "terminate      schedules termination event\n"
                   << "help           shows this help\n"
                    ;

                // let owner to append its own help
                owner_.processCtrl(cmd, os);
            } else {
                owner_.processCtrl(cmd, os);
            }
        } catch (const std::exception &e) {
            LOG(err3, log_)
                << "Error during handling ctrl command: " << e.what();
            os << "error: failed to execute command\n";
        }
    }

    if (terminateBlock) {
        // terminate response block
        os << '\4';
    }

    asio::async_write
        (socket_, output_
         , strand_.wrap(lib::bind(&CtrlConnection::handleWrite
                                  , shared_from_this()
                                  , placeholders::_1
                                  , placeholders::_2)));

    if (!closed_) {
        // ready to read next command
        startRead();
    }
}

void CtrlConnection::handleWrite(const boost::system::error_code &e
                                 , std::size_t bytes)
{
    if (e) {
        if (e.value() != asio::error::broken_pipe) {
            LOG(err2, log_) << "Control connection error: " << e;
        } else {
            LOG(info2, log_) << "Control connection closed";
        }
        return;
    }

    LOG(debug, log_) << "Read: " << bytes << " bytes.";

    if (closed_ && !output_.size()) {
        socket_.close();
    }
}

void SignalHandler::atFork(utility::AtFork::Event event)
{
    switch (event) {
    case utility::AtFork::prepare:
        ios_.notify_fork(asio::io_service::fork_prepare);
        break;

    case utility::AtFork::parent:
        ios_.notify_fork(asio::io_service::fork_parent);
        break;

    case utility::AtFork::child:
        ios_.notify_fork(asio::io_service::fork_child);
        stopAccept();
        break;
    }
}

void SignalHandler::registerSignal(int signo)
{
    ios_.post([this, signo]() { signals_.add(signo); });
}

struct ModeParser {
    ::mode_t mode;
};

template<typename CharT, typename Traits>
inline std::basic_istream<CharT, Traits>&
operator>>(std::basic_istream<CharT, Traits> &is, ModeParser &m)
{
    is >> std::oct >> m.mode;
    return is;
}

void CtrlConfig::configuration(po::options_description &cmdline
                               , po::options_description &config)
{
    cmdline.add_options()
        ("ctrl", po::value(&path)
         , "Path to ctrl socket (honored only when pid file is used).")
        ;

    config.add_options()
        ("ctrl.user", po::value(&username)
         , "Change owner of ctrl socket if set.")
        ("ctrl.group", po::value(&group)
         , "Change group of ctrl socket if set.")
        ("ctrl.mode", po::value<ModeParser>()
         , "Change permissions of control socket if set.")
        ;
}

void CtrlConfig::configure(const po::variables_map &vars)
{
    if (vars.count("ctrl.mode")) {
        mode = vars["ctrl.mode"].as<ModeParser>().mode;
    }
}

} } // namespace service::detail
