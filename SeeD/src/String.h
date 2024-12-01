#pragma once
#include <iostream>

std::wstring CharToWString(const char* ptr)
{
	std::string str(ptr);
	std::wstring temp(str.begin(), str.end());
	return temp;
}

class String : public std::wstring
{
public:
	String() : std::wstring()
	{

	}
	String(_In_z_ const WCHAR* const _Ptr) : std::wstring(_Ptr)
	{

	}
	String(std::wstring& str) : std::wstring(str)
	{

	}
	String(const std::wstring& str) : std::wstring(str)
	{

	}

	std::vector<String> Split(const String& i_delim)
	{
		std::vector<String> result;
		size_t startIndex = 0;

		for (size_t found = find(i_delim); found != String::npos; found = find(i_delim, startIndex))
		{
			result.emplace_back(std::wstring(begin() + startIndex, begin() + found));
			startIndex = found + i_delim.size();
		}
		if (startIndex != size())
			result.emplace_back(std::wstring(begin() + startIndex, end()));

		return result;
	}

	inline std::string ToString()
	{
		return std::string(begin(), end());
	}

	inline const char* ToConstChar()
	{
		return std::string(begin(), end()).c_str();
	}

};