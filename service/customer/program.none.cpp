#include "../program.hpp"

namespace service {

// Default copyright.
std::string Program::copyright() const
{
    return R"RAW(Copyright (C) 2011-2016 Melown Technologies SE
Strakonicka 1199/2d, 150 00 Praha 5, Czech Republic)RAW";
}

// Default licence.
std::string Program::licence() const
{
    return R"RAW(
This is a proprietary software. For internal purposes only.
Not to be redistributed.)RAW";
}

// Default licence.
std::string Program::licensee() const
{
    return "Melown Technologies SE";
}

// Default licence check.
void Program::licenceCheck() const {}

} // namespace service
