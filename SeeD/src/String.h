#pragma once
#include <iostream>
#include <algorithm>

std::wstring CharToWString(const char* ptr)
{
	std::string str(ptr);
	std::wstring temp(str.begin(), str.end());
	return temp;
}

std::string WCharToString(const WCHAR* ptr)
{
	std::wstring str(ptr);
#pragma warning( push )
#pragma warning(disable : 4244)
	std::string temp(str.begin(), str.end());
#pragma warning( pop )
	return temp;
}

class String : public std::string
{
public:
	String() : std::string()
	{

	}
	String(_In_z_ const char* const _Ptr) : std::string(_Ptr)
	{

	}
	String(std::string& str) : std::string(str)
	{

	}
	String(const std::string& str) : std::string(str)
	{

	}

	std::vector<String> Split(const String& i_delim)
	{
		std::vector<String> result;
		size_t startIndex = 0;

		for (size_t found = find(i_delim); found != String::npos; found = find(i_delim, startIndex))
		{
			String token = std::string(begin() + startIndex, begin() + found);
			if(!token.empty() && token != i_delim)
			{
				result.emplace_back(token);
			}
			startIndex = found + i_delim.size();
		}
		if (startIndex != size())
			result.emplace_back(std::string(begin() + startIndex, end()));

		return result;
	}

	String ToLower()
	{
		String low(*this);
		std::transform(low.begin(), low.end(), low.begin(), ::tolower);
		return low;
	}

	inline const std::wstring ToWString() const
	{
		return std::wstring(begin(), end());
	}
};

template<typename... Args>
static String StringFormat(std::string_view rt_fmt_str, Args&&... args)
{
	String message = std::vformat(rt_fmt_str, std::make_format_args(args...));
	return message;
}