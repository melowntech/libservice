#include <boost/filesystem/path.hpp>

#include <readline/readline.h>
#include <readline/history.h>

#include "utility/buildsys.hpp"
#include "utility/gccversion.hpp"
#include "utility/format.hpp"

#include "service/cmdline.hpp"
#include "service/ctrlclient.hpp"

namespace fs = boost::filesystem;
namespace po = boost::program_options;

namespace {

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

    int runInteractive();

    int runCommand();

    fs::path connect_;
    fs::path history_;

    boost::optional<std::string> command_;
};

void CtrlClient::configuration(po::options_description &cmdline
                               , po::options_description &config
                               , po::positional_options_description &pd)
{
    cmdline.add_options()
        ("connect", po::value(&connect_)->required()
         , "Path to UNIX socket to connect to.")
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
        out << ("Service control interface cmdline client\n"
                );

        return true;
    }

    return false;
}

int CtrlClient::run()
{
    if (command_) { return runCommand(); }

    return runInteractive();
}

int CtrlClient::runInteractive()
{
    ::using_history();

    if (!history_.empty()) {
        ::read_history(history_.c_str());
    }

    const auto name(fs::path(service::Program::argv0()).filename().string());
    const auto prompt(utility::format("ctrl:%s>", connect_.string()));

    try {
        service::CtrlClient client(connect_, name);

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
        std::cerr << name << ": " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << std::endl;
    return EXIT_SUCCESS;
}

int CtrlClient::runCommand()
{
    try {
        service::CtrlClient client(connect_, name);

        for (const auto &line : client.command(*command_)) {
            std::cout << line << "\n";
        }

        std::cout << std::flush;
    } catch (const std::exception &e) {
        std::cerr << name << ": " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char *argv[])
{
    return CtrlClient()(argc, argv);
}
