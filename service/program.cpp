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

#include <string.h>

#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <sstream>
#include <set>
#include <locale>

#include <boost/format.hpp>
#include <boost/filesystem.hpp>
#include <boost/utility/in_place_factory.hpp>
#include <boost/tokenizer.hpp>
#include <boost/token_functions.hpp>

#include "utility/buildsys.hpp"
#include "utility/path.hpp"

#include "githash.hpp"

#include "program.hpp"

#ifdef BUILDSYS_CUSTOMER_BUILD
#define LOCAL_BUILDSYS_CUSTOMER_INFO " for " BUILDSYS_CUSTOMER
#else
#define LOCAL_BUILDSYS_CUSTOMER_INFO ""
#endif

namespace service {

namespace {

bool unrecognized(int flags)
{
    return (flags & (ENABLE_UNRECOGNIZED_OPTIONS
                     | ENABLE_CONFIG_UNRECOGNIZED_OPTIONS));
}

struct Env {};

#ifdef _WIN32
// windows

inline std::ostream& operator<<(std::ostream &os, const Env &)
{
    return os;
}

void setCLocale()
{
    std::locale::global(std::locale("C"));
}

#else
// posix

inline std::ostream& operator<<(std::ostream &os, const Env &)
{
    bool first(true);
    for (const auto var : {
            "LANG", "LC_ALL", "LC_COLLATE"
            , "LC_CTYPE", "LC_MONETARY", "LC_NUMERIC", "LC_TIME"})
    {
        auto value(::getenv(var));
        if (!value) { continue; }
        if (!first) { os << ", "; } else { first = false; }
        os << var << "=" << value;
    }
    return os;
}

void setCLocale()
{
    // unset all env settings
    ::unsetenv("LANG");
    ::unsetenv("LC_ALL");
    ::unsetenv("LC_COLLATE");
    ::unsetenv("LC_CTYPE");
    ::unsetenv("LC_MONETARY");
    ::unsetenv("LC_NUMERIC");
    ::unsetenv("LC_TIME");

    std::locale::global(std::locale("C"));
    std::setlocale(LC_ALL, "C");
}

#endif

void add(UnrecognizedOptions &un
         , const po::basic_parsed_options<char> &parsed)
{
    UnrecognizedOptions::ConfigOptions opts;
    for (const auto &opt : parsed.options) {
        if (!opt.unregistered) { continue; }
        for (auto iot(opt.original_tokens.begin())
                 , eot(opt.original_tokens.end());
             iot != eot; ++iot)
        {
            auto key(*iot++);
            opts[key].push_back(*iot);
            un.seenConfigKeys.push_back(key);
        }
    }

    // add only if non-empty
    if (!opts.empty()) {
        un.config.push_back(opts);
    }
}

} // namespace

Program::Program(const std::string &name, const std::string &version
                 , int flags)
    : name(name), version(version)
    , log_(dbglog::make_module(name)), flags_(flags)
    , upSince_(std::time(nullptr))
    , argv0_()
{
    try {
        std::locale("");
    } catch (const std::exception&) {
        LOG(warn3)
            << "Invalid locale settings in the environment (" << Env{} << "). "
            << "Falling back to \"C\" locale.";
        setCLocale();
    }
}

Program::~Program()
{
}

std::string Program::identity() const {
    return name + "/" + version + "/" + detail::gitHash;
}

std::string Program::versionInfo() const
{
    std::ostringstream os;
    os << name << ' ' << version
       << (" (built on " __DATE__ " " __TIME__ " at ")
       << utility::buildsys::Hostname;

    if (*detail::gitHash) {
        os << " from git commit " << detail::gitHash;
    }

    os << (LOCAL_BUILDSYS_CUSTOMER_INFO ")");
    return os.str();
}

po::variables_map
Program::configure(int argc, char *argv[]
                   , const po::options_description &genericConcig)
{
    return configure(argc, argv
                     , po::options_description("command line options")
                     , genericConcig);

}

po::variables_map
Program::configure(int argc, char *argv[]
                   , const po::options_description &genericCmdline
                   , const po::options_description &genericConcig)
{
    argv0_ = argv[0];
    try {
        return configureImpl(argc, argv, genericCmdline, genericConcig);
    } catch (const po::error &e) {
        std::cerr << name << ": " << e.what() << std::endl;
        immediateExit(EXIT_FAILURE);
    } catch (const std::exception &e) {
        LOG(fatal, log_) << "Configure failed: " << e.what();
        immediateExit(EXIT_FAILURE);
    }
    throw;
}

utility::Duration Program::uptime()
{
    return uptime_.duration();
}

std::time_t Program::upSince() const
{
    return upSince_;
}

void Program::defaultConfigFile(const boost::filesystem::path
                                &defaultConfigFile)
{
    defaultConfigFile_ = defaultConfigFile;
}

std::string Program::argv0() const
{
    if (!argv0_) {
        LOGTHROW(err3, std::logic_error)
            << "Pointer to argv[0] has not been bound yet.";
    }
    return argv0_;
}

namespace {

std::pair<std::string, std::string> specialParser(const std::string& s)
{
    if (s.find("--help-") == 0) {
        auto value(s.substr(7));
        return {"#help", value.empty() ? " " : value};
    } else if (s.front() == '@') {
        return { "response-file", s.substr(1) };
    } else {
        return {"", ""};
    }
}

const char *EXTRA_OPTIONS = "\n";

typedef std::vector<std::string> Strings;
typedef std::vector<boost::filesystem::path> Files;

Strings parseResponseFiles(const Files &files, std::vector<std::stringstream> & dumpOutput)
{
    Strings args;

    for (const auto &file: files) {
        std::ifstream is(file.c_str());
        if (!is) {
            std::cerr
                << "Unable to read response file " << file << "."
                << std::endl;
            immediateExit(EXIT_FAILURE);
        }

        typedef boost::char_separator<char> Separator;
        typedef std::istreambuf_iterator<char> Iterator;
        typedef boost::tokenizer<Separator, Iterator> Tokenizer;

        Separator separator(" \n\r");
        Tokenizer tokenizer(Iterator(is), Iterator(), separator);

        std::copy(tokenizer.begin(), tokenizer.end()
                  , std::back_inserter(args));

        is.clear(); // clear fail and eof bits
        is.seekg(0, std::ios_base::beg); // seek to begin
        dumpOutput.emplace_back();
        dumpOutput.back() << "Loaded response file from " << file << ", contents:" << std::endl << is.rdbuf() << std::endl;
    }

    return args;
}

template <typename ...Args>
po::command_line_parser
createParser(const po::options_description &od
             , const po::positional_options_description &po
             , int flags, const po::ext_parser &extra
             , Args &&...args)
{
    auto parser(po::command_line_parser(std::forward<Args>(args)...)
                .style(po::command_line_style::default_style
                       & ~po::command_line_style::allow_guessing)
                .options(od)
                .positional(po));
    if (flags & ENABLE_UNRECOGNIZED_OPTIONS) {
        parser.allow_unregistered();
    }

    // add extra parser if valid
    if (extra) {
        parser.extra_parser
            ([=](const std::string &s) -> std::pair<std::string, std::string>
             {
                 auto res(specialParser(s));
                 if (!res.first.empty()) { return res; }
                 return extra(s);
             });
    } else {
        parser.extra_parser(specialParser);
    }

    return parser;
}

} // namespace

// nothing to do here
void Program::preNotifyHook(const po::variables_map &) {}

// nothing to do here
void Program::preConfigHook(const po::variables_map &) {}

struct DefaultHelper : HelpPrinter {
    DefaultHelper(const Program &self) : self(self) {}

    virtual bool help(std::ostream &out, const std::string &what) const {
        return self.help(out, what);
    }

    virtual Strings list() const { return self.listHelps(); }

    const Program &self;

    static std::shared_ptr<HelpPrinter> defaultHelper(const Program &self) {
        return std::make_shared<DefaultHelper>(self);
    }
};

HelpPrinter::pointer Program::help(const po::variables_map&) const
{
    return DefaultHelper::defaultHelper(*this);
}

po::variables_map
Program::configureImpl(int argc, char *argv[]
                       , po::options_description genericCmdline
                       , po::options_description genericConfig)
{
    po::options_description cmdline("");
    po::options_description config("");
    po::positional_options_description positionals;
    configuration(cmdline, config, positionals);

    genericCmdline.add_options()
        ("help", "produce help message")
        ("version,v", "display version and terminate")
        ("licence", "display terms of licence")
        ("license", "display terms of license")
        ("config,f", po::value<Files>()
         , "path to configuration file; when using multiple config files "
         "first occurrence of option wins")
        ("help-all", "show help for both command line and config file; "
         "if program provides detailed help information it is shown as well")
        ;

    for (const auto &h : listHelps()) {
        genericCmdline.add_options()
            (("help-" + h).c_str(), ("help for " + h).c_str());
    }

    genericConfig.add_options()
        ("log.mask", po::value<dbglog::mask>()
         ->default_value(dbglog::mask(dbglog::get_mask()))
         , "set dbglog logging mask")
        ("log.file", po::value<boost::filesystem::path>()
         , "set dbglog output file (none by default)")
        ("log.console", po::value<bool>()->default_value(true)
         , "enable console logging")
        ("log.dumpConfig", po::value<bool>()->default_value(false)->implicit_value(true)
         , "enable dumping of command line, response files and config files")
        ("log.timePrecision", po::value<unsigned short>()->default_value(0)
         , "set logged time sub-second precision (0-6 decimals)")
        ("log.file.truncate", "truncate log file on startup")
        ("log.file.archive"
         , "archive existing log file (adds last modified as an extension) "
         "and start with new one; overrides log.file.truncate")
        ;

    po::options_description hiddenCmdline("hidden command line options");
    hiddenCmdline.add_options()
        ("#help", po::value<Strings>(), "extra help")
        ;

    po::options_description responseFile;
    responseFile.add_options()
        ("response-file", po::value<Files>()
         , "Windows-style response files. Can be specified as @filename.")
        ;

    // all config options
    po::options_description all(name);
    all.add(genericCmdline).add(cmdline)
        .add(genericConfig).add(config)
        .add(hiddenCmdline);

    po::options_description extra;
    if (flags_ & ENABLE_UNRECOGNIZED_OPTIONS) {
        extra.add_options()
            (EXTRA_OPTIONS, po::value<Strings>());
        all.add(extra);
        positionals.add(EXTRA_OPTIONS, -1);
    }

    po::variables_map vm;
    std::vector<po::option> parsedOptions;

    const auto &parse([&](po::command_line_parser &&parser)
    {
        auto parsed(parser.run());
        po::store(parsed, vm);
        if (unrecognized(flags_)) {
            parsedOptions.insert(parsedOptions.end(), parsed.options.begin()
                                 , parsed.options.end());
        }
    });

    // parse cmdline (support response file)
    po::options_description full;
    full.add(all).add(responseFile);
    parse(createParser(full, positionals, flags_, extraParser(), argc, argv));

    std::vector<std::stringstream> dumpOutput;
    {
        std::stringstream cmdline;
        for (int i = 0; i < argc; i++) {
            if (i > 0) {
                cmdline << " ";    
            }
            cmdline << argv[i];
        }
        dumpOutput.emplace_back();
        dumpOutput.back() << "Command line: " << std::endl << cmdline.rdbuf() << std::endl;
    }

    if (vm.count("response-file")) {
        // parse response file as a cmdline, ignore response file
        const auto args(parseResponseFiles(vm["response-file"].as<Files>(), dumpOutput));
        parse(createParser(all, positionals, flags_, {}, args));
    }

    if (vm.count("version")) {
        std::cout << versionInfo() << std::endl
                  << copyright() << std::endl;
            ;
        immediateExit(EXIT_SUCCESS);
    }

    if (vm.count("licence") || vm.count("license")) {
        std::cout << copyright() << std::endl
                  << std::endl;
        const auto licensedTo(licensee());
        if (!licensedTo.empty()) {
            std::cout << "Licensed to " << licensedTo << std::endl;
        }
        std::cout << licence() << std::endl;
        immediateExit(EXIT_SUCCESS);
    }

    std::set<std::string> helps;
    if (vm.count("#help")) {
        auto what(vm["#help"].as<Strings>());
        helps.insert(what.begin(), what.end());
    }

    const bool hasHelp(vm.count("help"));

    if (hasHelp || !helps.empty()) {
        auto helper(help(vm));
        if (!helper) { helper = DefaultHelper::defaultHelper(*this); }

        // check for help
        if (helps.find("all") != helps.end()) {
            std::cout << name << ": ";

            // print app help help
            helper->help(std::cout, std::string());

            std::cout << "\n" << genericCmdline << responseFile << cmdline;
            helper->help(std::cout, "@cmdline");
            std::cout << '\n' << genericConfig;
            if (!(flags_ & DISABLE_CONFIG_HELP)) {
                // only when allowed
                std::cout << config;
                helper->help(std::cout, "@config");
            }

            helps.erase("all");
            if (helps.empty()) {
                for (const auto &h : helper->list()) {
                    std::cout << '\n';
                    helper->help(std::cout, h);
                }

                immediateExit(EXIT_SUCCESS);
            }
            std::cout << '\n';
        } else if (hasHelp) {
            std::cout << name << ": ";

            // print app help help
            helper->help(std::cout, std::string());

            std::cout << "\n" << genericCmdline << responseFile << cmdline;
            helper->help(std::cout, "@cmdline");

            if (helps.empty()) {
                immediateExit(EXIT_SUCCESS);
            }
            std::cout << '\n';
        }

        if (!helps.empty()) {
            for (const auto &what : helps) {
                if (!helper->help(std::cout, what)) {
                    throw po::unknown_option("--help-" + what);
                }
                std::cout << '\n';
            }

            immediateExit(EXIT_SUCCESS);
        }
    }

    licenceCheck();

    // allow derived class to hook here before calling notify and configure.
    preConfigHook(vm);

    UnrecognizedOptions un;

    // get list of config files or use default (if given)
    auto &cfgs(configFiles_);
    if (vm.count("config")) {
        cfgs = vm["config"].as<Files>();
    } else if (defaultConfigFile_) {
        cfgs.push_back(*defaultConfigFile_);
    }
    // and absolutize them
    for (auto &cfg : cfgs) { cfg = absolute(cfg); }

    bool dumpConfig(vm.count("log.dumpConfig") && vm["log.dumpConfig"].as<bool>());

    if (!cfgs.empty()) {
        po::options_description configs(name);
        configs.add(genericConfig).add(config);

        for (const auto &cfg : cfgs) {
            std::ifstream f;
            f.exceptions(std::ifstream::failbit | std::ifstream::badbit);
            try {
                f.open(cfg.c_str());
                f.exceptions(std::ifstream::badbit);
                auto parsed(po::parse_config_file(f, configs
                            , flags_ & ENABLE_CONFIG_UNRECOGNIZED_OPTIONS));
                store(parsed, vm);

                if (flags_ & ENABLE_CONFIG_UNRECOGNIZED_OPTIONS) {
                    add(un, parsed);
                }

                f.clear(); // clear fail and eof bits
                f.seekg(0, std::ios_base::beg); // seek to begin
                if (dumpConfig) {
                    dumpOutput.emplace_back();
                    dumpOutput.back() << "Loaded configuration from " << cfg << ", contents:" << std::endl << f.rdbuf() << std::endl;
                } else {
                    // Warning, logging before log.mask, log.file, etc. is set!
                    LOG(info3) << "Loaded configuration from " << cfg << ".";
                }
                f.close();
            } catch(const std::ios_base::failure &e) {
                LOG(fatal) << "Cannot read config file " << cfg << ": "
                           << e.what();
                immediateExit(EXIT_FAILURE);
            }
        }
    }

    // update log mask if set
    if (vm.count("log.mask")) {
        dbglog::set_mask(vm["log.mask"].as<dbglog::mask>());
    }

    // set log file if set
    if (vm.count("log.file")) {
        bool archive(vm.count("log.file.archive"));
        bool truncate(vm.count("log.file.truncate"));

        // NB: notify(vm) not called yet => logFile_ is not set!
        logFile_ = absolute(vm["log.file"].as<boost::filesystem::path>());

        if (archive) {
            boost::system::error_code ec;
            auto lastModified
                (boost::filesystem::last_write_time(logFile_, ec));
            if (!ec) {
                // file exists and we have last modification time -> rename
                boost::filesystem::rename
                    (logFile_
                     , utility::addExtension
                     (logFile_, str(boost::format(".%d") % lastModified))
                     , ec);
            }

            // force truncate (we do not know who is writing to the file as
            // well)
            truncate = true;
        }

        dbglog::log_file(logFile_.string());
        if (truncate) {
            dbglog::log_file_truncate();
        }
    }

    // enable/disable log console if set
    if (vm.count("log.console")) {
        dbglog::log_console(vm["log.console"].as<bool>());
    }

    if (vm.count("log.timePrecision")) {
        dbglog::log_time_precision
            (vm["log.timePrecision"].as<unsigned short>());
    }

    if (dumpConfig) {
        // dump command line, response files and config files
        for (const auto & d : dumpOutput) {
            LOG(info3) << d.rdbuf();
        }
    }

    boost::optional<UnrecognizedParser> unrParser;

    if (unrecognized(flags_)) {
        /* same as collect_unrecognized(parsed.options,
         * po::include_positional) except only unknown positionals are
         * collected
         */
        for (const auto &opt : parsedOptions) {
            if (opt.unregistered
                || ((opt.position_key >= 0)
                    && (positionals.name_for_position(opt.position_key)
                        == EXTRA_OPTIONS)))
            {
                un.cmdline.insert(un.cmdline.end()
                                  , opt.original_tokens.begin()
                                  , opt.original_tokens.end());
            }
        }

        // let the implementation to work with the unrecognized cmdline/config
        // options
        auto p(configure(vm, un));
        if (p) {
            // parse command line
            auto parser(po::command_line_parser(un.cmdline)
                        .options(p->options)
                        .positional(p->positional));

            // add extra parser if valid
            if (p->extraParser) {
                parser.extra_parser(p->extraParser);
            }

            auto parsed(parser.run());
            po::store(parsed, vm);

            // process config
            for (const auto &config : un.config) {
                UnrecognizedOptions::OptionList opts;
                for (const auto &item : config) {
                    for (const auto &opt : item.second) {
                        // NB: we are parsing config file like a cmdline -> we
                        // need to add -- prefix to make it look like it comes
                        // from cmdline
                        opts.push_back("--" + item.first);
                        opts.push_back(opt);
                    }
                }
                auto parser(po::command_line_parser(opts)
                            .options(p->options));

                // add extra parser if valid
                if (p->extraParser) {
                    parser.extra_parser(p->extraParser);
                }

                auto parsed(parser.run());
                po::store(parsed, vm);
            }

            unrParser = boost::in_place(*p);
        }
    }

    // allow derived class to hook here before calling notify and configure.
    preNotifyHook(vm);
    po::notify(vm);
    configure(vm);

    // and configure unrecognized parser if used
    if (unrParser && (unrParser->configure)) { unrParser->configure(vm); }

    if (flags() & SHOW_LICENCE_INFO) {
        LOG(info4)
            << "This build of " << Program::name << " is licensed to "
            << licensee() << ", subject to license agreement.\n"
            << copyright() << '\n';
    }

    return vm;
}

UnrecognizedOptions::Keys UnrecognizedOptions::configKeys() const
{
    Keys k;
    for (const auto &co : config) {
        for (const auto &item : co) {
            k.insert(item.first);
        }
    }
    return k;
}

const std::string&
UnrecognizedOptions::singleConfigOption(const std::string &key) const
{
    for (const auto &co : config) {
        for (const auto &item : co) {
            if (item.first != key) { continue; }
            if (item.second.empty()) { continue; }
            if (item.second.size() != 1) {
                po::multiple_values e;
                e.set_option_name(key);
                throw e;
            }
            return item.second.front();
        }
    }

    throw po::required_option(key);
}

UnrecognizedOptions::OptionList
UnrecognizedOptions::multiConfigOption(const std::string &key) const
{
    for (const auto &co : config) {
        for (const auto &item : co) {
            if (item.first != key) { continue; }
            if (item.second.empty()) { continue; }
            return item.second;
        }
    }

    throw po::required_option(key);
}

bool HelpPrinter::help(std::ostream &, const std::string &) const {
    return false;
}

Strings HelpPrinter::list() const { return {}; }

} // namespace service
