#pragma once
#include <iostream>

std::wstring CharToWString(const char* ptr)
{
	std::string str(ptr);
	std::wstring temp(str.begin(), str.end());
	return temp;
}

std::string WCharToString(const WCHAR* ptr)
{
	std::wstring str(ptr);
	std::string temp(str.begin(), str.end());
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
			result.emplace_back(std::string(begin() + startIndex, begin() + found));
			startIndex = found + i_delim.size();
		}
		if (startIndex != size())
			result.emplace_back(std::string(begin() + startIndex, end()));

		return result;
	}

	inline std::wstring ToWString()
	{
		return std::wstring(begin(), end());
	}

	inline const WCHAR* ToConstWChar()
	{
		return std::wstring(begin(), end()).c_str();
	}

};