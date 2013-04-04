#include <errno.h>
#include <unistd.h>

#include <iostream>
#include <fstream>
#include <set>

#include "program.hpp"

namespace service {

program::program(const std::string &name, const std::string &version
                 , int flags)
    : name(name), version(version)
    , log_(dbglog::make_module(name)), flags_(flags)
{
}

program::~program()
{
}

void program::configure(int argc, char *argv[]
                        , const po::options_description &generic)
{
    try {
        configureImpl(argc, argv, generic);
    } catch (const po::error &e) {
        std::cerr << name << ": " << e.what() << std::endl;
        throw immediate_exit(EXIT_FAILURE);
    } catch (const std::exception &e) {
        LOG(fatal, log_) << "Configure failed: " << e.what();
        throw immediate_exit(EXIT_FAILURE);
    }
}

namespace {

std::pair<std::string, std::string> helpParser(const std::string& s)
{
    if (s.find("--help-") == 0) {
        auto value(s.substr(7));
        return {"@help", value.empty() ? " " : value};
    } else {
        return {"", ""};
    }
}

} // namespace

void program::configureImpl(int argc, char *argv[]
                            , po::options_description genericConfig)
{
    po::options_description cmdline("");
    po::options_description config("");
    po::positional_options_description positionals;
    configuration(cmdline, config);
    configuration(positionals);

    po::options_description genericCmdline("command line options");
    genericCmdline.add_options()
        ("help", "produce help message")
        ("version,v", "display version and terminate")
        ("config,f", po::value<std::string>()
         , "path to configuration file")
        ("help-all", "show help for both command line and config file")
        ;

    genericConfig.add_options()
        ("log.mask", po::value<dbglog::mask>()
         ->default_value(dbglog::mask(dbglog::get_mask()))
         , "set dbglog logging mask")
        ("log.file", po::value<std::string>()
         , "set dbglog output file (non by default)")
        ("log.console", po::value<bool>()->default_value(true)
         , "enable console logging")
        ;

    po::options_description hiddenCmdline("hidden command line options");
    hiddenCmdline.add_options()
        ("@help", po::value<std::vector<std::string>>(), "extra help")
        ;

    // all config options
    po::options_description all(name);
    all.add(genericCmdline).add(cmdline)
        .add(genericConfig).add(config)
        .add(hiddenCmdline);

    // parse cmdline
    auto parser(po::command_line_parser(argc, argv).options(all)
                 .positional(positionals).extra_parser(helpParser));
    if (flags_ & ENABLE_UNRECOGNIZED_OPTIONS) {
        parser = parser.allow_unregistered();
    }

    auto parsed(parser.run());

    po::variables_map vm;
    po::store(parsed, vm);

    if (vm.count("config")) {
        const std::string cfg(vm["config"].as<std::string>());
        po::options_description configs(name);
        configs.add(genericConfig).add(config);

        std::ifstream f;
        f.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        try {
            f.open(cfg.c_str());
            f.exceptions(std::ifstream::badbit);
            store(po::parse_config_file(f, configs), vm);
            f.close();

            LOG(info3) << "Loaded configuration from <" << cfg << ">.";
        } catch(const std::ios_base::failure &e) {
            LOG(fatal) << "Cannot read config file <" << cfg << ">: "
                       << e.what();
            throw immediate_exit(EXIT_FAILURE);
        }
    }

    if (vm.count("version")) {
        std::cout << name << ' ' << version << std::endl;
        throw immediate_exit(EXIT_SUCCESS);
    }

    std::set<std::string> helps;
    if (vm.count("@help")) {
        auto what(vm["@help"].as<std::vector<std::string>>());
        helps.insert(what.begin(), what.end());
    }

    // check for help
    if (helps.find("all") != helps.end()) {
        std::cout << name << ": ";

        // print app help help
        help(std::cout, std::string());

        std::cout << "\n" << genericCmdline << cmdline
                  << '\n' << genericConfig;
        if (!(flags_ & DISABLE_CONFIG_HELP)) {
            // only when allowed
            std::cout << config;
        }

        helps.erase("all");
        if (helps.empty()) {
            throw immediate_exit(EXIT_SUCCESS);
        }
        std::cout << '\n';
    } else if (vm.count("help")) {
        std::cout << name << ": ";

        // print app help help
        help(std::cout, std::string());

        std::cout << "\n" << genericCmdline << cmdline;
        if (helps.empty()) {
            throw immediate_exit(EXIT_SUCCESS);
        }
        std::cout << '\n';
    }

    if (!helps.empty()) {
        for (const auto &what : helps) {
            if (!help(std::cout, what)) {
                throw po::unknown_option("--help-" + what);
            }
            std::cout << '\n';
        }

        throw immediate_exit(EXIT_SUCCESS);
    }

    // update log mask if set
    if (vm.count("log.mask")) {
        dbglog::set_mask(vm["log.mask"].as<dbglog::mask>());
    }

    // set log file if set
    if (vm.count("log.file")) {
        dbglog::log_file(vm["log.file"].as<std::string>());
    }

    // enable/disable log console if set
    if (vm.count("log.console")) {
        dbglog::log_console(vm["log.console"].as<bool>());
    }

    po::notify(vm);

    configure(vm);

    if (flags_ & ENABLE_UNRECOGNIZED_OPTIONS) {
        // process unrecognized options
        configure(collect_unrecognized
                  (parsed.options, po::include_positional));
    }
}

} // namespace service
