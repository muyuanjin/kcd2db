#include "StringUtils.h"

#include <windows.h>

#include <cctype>

namespace kcd2db
{
std::string WideToUtf8(const wchar_t* value)
{
    if (!value || value[0] == L'\0')
    {
        return {};
    }

    const int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1)
    {
        return {};
    }

    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), size, nullptr, nullptr);
    result.resize(size - 1);
    return result;
}

std::string ToLowerAscii(std::string_view value)
{
    std::string result;
    result.reserve(value.size());
    for (const unsigned char ch : value)
    {
        result.push_back(static_cast<char>(std::tolower(ch)));
    }
    return result;
}

bool EqualsIgnoreCaseAscii(std::string_view lhs, std::string_view rhs)
{
    if (lhs.size() != rhs.size())
    {
        return false;
    }

    for (std::size_t i = 0; i < lhs.size(); ++i)
    {
        const auto left = static_cast<unsigned char>(lhs[i]);
        const auto right = static_cast<unsigned char>(rhs[i]);
        if (std::tolower(left) != std::tolower(right))
        {
            return false;
        }
    }
    return true;
}
}
