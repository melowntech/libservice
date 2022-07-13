#include <boost/filesystem/path.hpp>

#include <readline/readline.h>
#include <readline/history.h>

#include "utility/buildsys.hpp"
#include "utility/gccversion.hpp"
#include "utility/format.hpp"
#include "utility/tcpendpoint-io.hpp"

#include "service/cmdline.hpp"
#include "service/netctrlclient.hpp"

namespace fs = boost::filesystem;
namespace po = boost::program_options;

namespace {

struct ResolvableTcpEndpoint {
    utility::TcpEndpoint endpoint;
};

class CtrlClient : public service::Cmdline {
public:
    CtrlClient()
        : service::Cmdline("service-ctrl-client", BUILD_TARGET_VERSION
                           , service::DISABLE_EXCESSIVE_LOGGING)
    {}

private:
    void configuration(po::options_description &cmdline
                       , po::options_description &config
                       , po::positional_options_description &pd)
        override;

    void configure(const po::variables_map &vars) override;

    bool help(std::ostream &out, const std::string &what) const
        override;

    int run() override;

    int runInteractive(const service::NetCtrlClient::Params &params);

    int runCommand(const service::NetCtrlClient::Params &params);

    std::string connect_;
    fs::path history_;

    boost::optional<std::string> command_;
};

void CtrlClient::configuration(po::options_description &cmdline
                               , po::options_description &config
                               , po::positional_options_description &pd)
{
    cmdline.add_options()
        ("connect", po::value(&connect_)->required()
         , "TCP endpoint to connect to.")
        ("history", po::value(&history_)
         , "Path to a history file.")
        ("command,c", po::value(&command_)
         , "Executes command from input string.")
        ;

    pd.add("connect", 1)
        ;

    (void) config;
}

void CtrlClient::configure(const po::variables_map &vars)
{
    (void) vars;
}

bool CtrlClient::help(std::ostream &out, const std::string &what) const
{
    if (what.empty()) {
        // program help
        out << ("Network-enabled service control interface cmdline client\n"
                );

        return true;
    }

    return false;
}

int CtrlClient::run()
{
    service::NetCtrlClient::Params params;

    try {
        params = { connect_ };
    } catch (const std::exception &e) {
        std::cerr << name << ": " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return (command_
            ? runCommand(params)
            : runInteractive(params));
}

int CtrlClient::runInteractive(const service::NetCtrlClient::Params &params)
{
    ::using_history();

    if (!history_.empty()) {
        ::read_history(history_.c_str());
    }

    try {
        service::NetCtrlClient client(params);

        const auto prompt(utility::format("ctrl:%s>", params.endpoint));

        for (;;) {
            auto buf(::readline(prompt.c_str()));
            if (!buf) {
                std::cout << std::endl;
                return EXIT_SUCCESS;
            }
            std::string cmdline(buf);
            ::free(buf);

            if (cmdline.empty()) { continue; }
            ::add_history(cmdline.c_str());
            if (!history_.empty()) {
                ::write_history(history_.c_str());
            }

            try {
                auto response(client.command(cmdline));
                for (const auto &line : response) {
                    std::cout << line << "\n";
                }
                std::cout << std::flush;
            } catch (const utility::CtrlCommandError &e) {
                std::cerr << e.what() << std::endl;
            }
        }
    } catch (const std::exception &e) {
        std::cerr << params.component << ": " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << std::endl;
    return EXIT_SUCCESS;
}

int CtrlClient::runCommand(const service::NetCtrlClient::Params &params)
{
    try {
        service::NetCtrlClient client(params);

        for (const auto &line : client.command(*command_)) {
            std::cout << line << "\n";
        }

        std::cout << std::flush;
    } catch (const std::exception &e) {
        std::cerr << params.component << ": " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char *argv[])
{
    return CtrlClient()(argc, argv);
}
