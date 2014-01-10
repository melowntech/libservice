#include <boost/filesystem.hpp>

#include "dbglog/dbglog.hpp"
#include "utility/parse.hpp"

#include "signalhandler.hpp"

namespace service { namespace detail {

SignalHandler::SignalHandler(dbglog::module &log, Service &owner
                             , pid_t mainPid
                             , const boost::optional<fs::path> &ctrlPath)
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
    , ctrlPath_(ctrlPath), ctrl_(ios_)
{
    if (ctrlPath_) {
        local::stream_protocol::endpoint e(ctrlPath_->string());
        ctrl_.open(e.protocol());
        ctrl_.set_option(asio::socket_base::reuse_address(true));
        ctrl_.bind(e);
        ctrl_.listen();
    };

    signals_.add(SIGUSR1);

    utility::AtFork::add(this, std::bind(&SignalHandler::atFork, this
                                         , std::placeholders::_1));
}

SignalHandler::~SignalHandler()
{
    utility::AtFork::remove(this);

    // remove ctrl path if in main process
    if (ctrlPath_ && (::getpid() == mainPid_)) {
        remove_all(*ctrlPath_);
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
    signals_.async_wait(boost::bind(&SignalHandler::signal, this
                                    , asio::placeholders::error
                                    , asio::placeholders::signal_number));
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
    : public std::enable_shared_from_this<CtrlConnection>
    , boost::noncopyable
{
public:
    typedef std::shared_ptr<CtrlConnection> pointer;

    CtrlConnection(Service &owner, asio::io_service &ios
                   , SignalHandler &sh)
        : owner_(owner), socket_(ios), sh_(sh)
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
    SignalHandler &sh_;

    boost::asio::streambuf input_;
    boost::asio::streambuf output_;
};

void SignalHandler::startAccept()
{
    if (!ctrl_.is_open()) { return; }

    auto con(std::make_shared<CtrlConnection>(owner_, ios_, *this));
    ctrl_.async_accept
        (con->socket()
         , boost::bind(&SignalHandler::newCtrlConnection, this
                       , asio::placeholders::error, con));
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
        LOG(info2) << "New control connection.";
        con->startRead();
    }

    if (e.value() != asio::error::operation_aborted) {
        startAccept();
    }
}

void CtrlConnection::startRead()
{
    LOG(debug) << "starting read";

    asio::async_read_until
         (socket_, input_, "\n"
         , boost::bind(&CtrlConnection::lineRead
                       , shared_from_this()
                       , asio::placeholders::error
                       , asio::placeholders::bytes_transferred));
}

void CtrlConnection::lineRead(const boost::system::error_code &e
                              , std::size_t bytes)
{
    if (e) {
        if (e.value() != asio::error::eof) {
            LOG(err2) << "Control connection error: " << e;
        } else {
            LOG(info3) << "Control connection closed";
        }
        return;
    }

    LOG(debug) << "Read: " << bytes << " bytes.";

    std::istream is(&input_);
    std::string line;
    std::getline(is, line);

    std::ostream os(&output_);

    auto cmdValue(utility::separated_values::split<std::vector<std::string> >
                  (line, " \t"));

    if (cmdValue.empty()) {
        os << "empty command received\n";
    } else {
        Service::CtrlCommand cmd{
            cmdValue.front()
            , std::vector<std::string>
                (cmdValue.begin() + 1, cmdValue.end())
        };

        if (cmd.cmd == "logrotate") {
            sh_.logRotate();
            os << "log rotation scheduled\n";
        } else {
            owner_.processCtrl(cmd, os);
        }
    }

    asio::async_write
        (socket_
         , output_
         , boost::bind(&CtrlConnection::handleWrite
                       , shared_from_this()
                       , asio::placeholders::error
                       , asio::placeholders::bytes_transferred));
}

void CtrlConnection::handleWrite(const boost::system::error_code &e
                                 , std::size_t bytes)
{
    if (e) {
        if (e.value() != asio::error::broken_pipe) {
            LOG(err2) << "Control connection error: " << e;
        } else {
            LOG(info2) << "Control connection closed";
        }
        return;
    }

    LOG(debug) << "Written: " << bytes << ".";
    output_.consume(bytes);

    // ready to read next command
    startRead();
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

} } // namespace service::detai

