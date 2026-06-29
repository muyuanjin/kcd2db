#pragma once

#include <string>
#include <string_view>

namespace kcd2db
{
std::string WideToUtf8(const wchar_t* value);
std::string ToLowerAscii(std::string_view value);
bool EqualsIgnoreCaseAscii(std::string_view lhs, std::string_view rhs);
}
